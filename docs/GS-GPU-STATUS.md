# GS acelerado — primeiro caminho Vulkan e Direct3D 12

## Residência incremental — revisão `4fdd7dd`, 05/11/2026

`da485fc`, `580d381` e `4fdd7dd` implementam coerência por páginas de 8 KiB,
reuso dos buffers e readback seletivo/fundido, com valores e máscaras de bits
preservados. Os 25 testes focados passaram na revisão final. A medição
intermediária completa reduziu readback de 16 MiB para 320 KiB por janela;
a certificação de desempenho e a CI finais ainda têm pendências.

Resultados por revisão, execução comercial equivalente sem aceleração GPU
nessa janela, regressão CPU intermediária, amostragem final incompleta e
estado da CI estão em [GS-GPU-RESIDENCY](GS-GPU-RESIDENCY.md).
As seções históricas abaixo descrevem a primeira entrega, anterior à residência.

Evidência dos commits `92dc21c` e `e626526`, em 05/11/2026. As medições abaixo
se referem ao primeiro caminho; os incrementos residentes `da485fc`/`580d381`,
produzidos em paralelo, têm evidência própria e foram preservados.

## Contrato implementado

Implementação original BSD-3-Clause, integrada a `GsRegisters`/`GsVram` e ao
GIF existente. O AOT R5900/VU1, os clocks, DMA, IRQ e os efeitos do caminho CPU
continuam compartilhados. Não há segundo GS, runtime ou estado de hardware.
As referências e suas limitações estão em [GS-GPU-REFERENCE-AUDIT](GS-GPU-REFERENCE-AUDIT.md).

O setup e a validação comuns selecionam compute inteiro para pontos, sprites
e triângulos flat CT32/CT24, incluindo strip/fan já montados pelo GS. O shader
aplica a regra de cobertura, SCANMSK, FBMSK, FBA, alpha test/AFAIL, blending
inteiro com PABE/COLCLAMP, teste Z e escrita Z32/Z24. Triângulos com Z utilizado
precisam de profundidade constante. Não se usa raster/blending implícito do host.

Texturas UV/STQ, CLUT, filtros, Gouraud, AA1 e profundidade triangular variável
continuam no renderer CPU existente, com suas provas e recusas preservadas.
Layouts com FBW zero, alias/wrap ou sobreposição de páginas framebuffer/Z
também usam CPU. Não há patch por título nem hipótese nova habilitada.
Uma prova conservadora de páginas conhecidas precede leituras de destino na
GPU: pixels não provados retornam à validação síncrona CPU. Bits desconhecidos
nunca viram pixels inventados.

`GsVram` mantém os valores e máscaras canônicos. Até 256 comandos capturados
formam um lote; cada dispatch corresponde a uma primitiva original, com
barreira de memória antes da seguinte. CPU reads/writes, fallback, CLUT,
TEXFLUSH, SIGNAL/FINISH, scanout, cópia e checkpoint materializam a fila.
Os contadores de revisão/escrita e joins de conhecimento são produzidos pela
GPU e incorporados ao mesmo estado lógico. Cópias e restaurações não herdam
handles, filas ou contadores de host; o backend é vinculado explicitamente.

Falha de dispositivo ou consumo indevido de conhecimento encerra o backend
com causa e sem publicação do lote. Vulkan tem prazo de fence de 10 segundos;
D3D12, 30 segundos e remoção do dispositivo em falha de conclusão. Falta de
loader/capacidade é distinguida de falha durante uma execução selecionada.

## Seleção e dependências

CPU continua padrão. `ps2n-run` e `ps2n-boot` aceitam
`--gs-backend=cpu|vulkan|directx`; indisponibilidade na seleção é reportada e
conserva o CPU. O launcher experimental aceita o campo opcional
`"gs_backend": "vulkan"` ou `"directx"` no manifesto local revisado; um backend
explicitamente pedido e indisponível recusa essa sessão com a razão.
Os frames seguem o scanout original e o apresentador existente do launcher.
Nenhum manifesto/checkpoint original foi alterado.

`PS2N_ENABLE_VULKAN=OFF` e `PS2N_ENABLE_DIRECTX=OFF` desabilitam os backends.
Vulkan precisa apenas de headers no build, com loader dinâmico no runtime;
`PS2N_VULKAN_INCLUDE_DIR` pode selecionar um SDK existente. O SPIR-V original
está embutido; `ps2n-gs-shader` o regenera offline quando `glslc` está disponível.
D3D12 é Windows opcional, compilado quando DXC existe; o HLSL gera DXIL no
build. D3D12/DXGI e Vulkan são carregados das bibliotecas do sistema, sem DXC
no runtime. Ambos exigem aritmética inteira de 64 bits. D3D12 verifica SM6.0
e a API de ciclo de vida do dispositivo. MoltenVK é descoberto opcionalmente,
com extensões de portabilidade, e pode ser recusado por falta de capacidade.

O esquema de checkpoint permanece
`c649550347ab1c003a8b8e441e3241a8917c893acb09a3a5262c2ad2a6fc2ee6`:
nenhum campo convidado foi acrescentado. A identidade de runtime inclui os
shaders, SPIR-V, configuração dos backends e identidade do DXC/dependências.
As migrações antigas continuam exigindo opt-in e verificação de payload.

## Disponibilidade Direct3D 12 comprovada antes da seleção

`e9bbc21` acrescenta uma prova de execução ao backend, integrada à interface
residente sem alterar suas políticas de propriedade/sincronização. Antes de
anunciar disponibilidade, o dispositivo executa um sprite e um triângulo
inteiro de 64 bits em armazenamento de teste isolado. A prova exige 100
escritas/revisões e comparação integral de valores e máscaras, incluindo as
regiões não tocadas. Estatísticas do trabalho GS começam depois dessa prova.
Nenhum GS, ciclo, IRQ, firmware ou dado do usuário é criado/modificado.
Os quatro vetores temporários da prova ocupam 16 MiB durante a inicialização;
esse custo posterior não está incluído nos picos medidos do commit `e626526`.

A CI inicial de Windows ARM64 registrou `DXGI_ERROR_DEVICE_REMOVED` durante
o mapeamento de timestamps; isso não identifica a origem da remoção. O erro
agora conserva adaptador e `GetDeviceRemovedReason` antes da limpeza.
`PS2N_GS_GPU_DIAGNOSTICS=1` habilita DRED antes da criação do dispositivo e
inclui até quatro breadcrumbs, identificação do driver e endereço de falha,
quando o runtime os oferece. É opcional e não exige debug layer. A CI Windows
habilita essa instrumentação; o modo normal a mantém desabilitada.

Falha da prova inicial reporta indisponibilidade com causa; falhas posteriores
à seleção encerram a execução e preservam a exceção original. A política é
genérica, sem regras por arquitetura, fabricante, modelo ou título. Na RTX
4070, os dois testes completos residentes passaram com a prova e DRED ativo
(14,56 s). Isso não certifica a GPU do runner ARM64 nem explica sozinho sua
remoção anterior.

## Equivalência local

Windows x64, MSVC 19.44 Release, NVIDIA GeForce RTX 4070. As suítes Vulkan e
D3D12 executaram **487 primitivas / 288 lotes** cada. Comparam toda a VRAM de
4 MiB e as máscaras de 4 MiB, RGBA, revisões e escritas, em vez de screenshots.
Incluem CT32/24, winding/subpixel/scissor, alpha/AFAIL, blending/PABE, máscaras,
Z, dependências dentro de um lote, recusa de dados desconhecidos, fallback de
textura/AA1, transferências, TEXFLUSH, FINISH/SIGNAL, cópias e save/load.

Uma ROM sintética própria executa comandos reais EE→FIFO GIF→GS e produz o
golden RGBA `43564b222dee0155325d1db289518d63f6a02af60690dd6da83ad69d002e3f54`.
Estado completo (incluindo DMA ocioso), trace/barramento, ciclos/IRQ, eventos de campo e continuação
após checkpoint são iguais CPU/GPU. O IOP executa instruções próprias válidas,
sem usar um estado que já recusou execução como checkpoint.

Build separado com ambos os backends desabilitados: quatro testes M2.9
passaram; os dois testes GPU comprovaram raster/scanout CPU antes de skip 77.
Build desses alvos: 50,03 s, sob carga de compilação concorrente; essa medida
não demonstra melhoria de compilação. Não requer GPU, AVX ou OpenGL global.

## Benchmark sintético inicial

CI final do código `e9bbc21`: [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/42132234795)
e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/42132234724)
aprovados nos seis alvos, incluindo VU1 AOT e CLI. Vulkan executou e passou
nos dois Linux; DirectX passou no Windows x64. No Windows ARM64, o backend
compilou com DXC, mas ficou indisponível antes da seleção; raster/scanout CPU
passaram. O motivo específico/DRED do skip foi suprimido pelo CTest, portanto
a GPU ARM64 permanece não certificada e a causa da remoção anterior não
está isolada. macOS compilou Vulkan e manteve CPU quando não havia runtime.

`ps2n-gs-bench 5 512 small|fill vulkan|directx`: warmup igual, cinco rodadas
alternando CPU/GPU, comparando VRAM/knowledge/contadores/RGBA a cada rodada.
Medianas por lote, rasterização + sincronização; scanout medido separadamente.
Primeira coleta sem afinidade fixa e com outras compilações no host: serve
para identificar custos, não para comparar absolutamente Vulkan contra D3D12.

| Backend/carga | CPU (ms) | GPU + sincronização (ms) | Razão CPU/GPU |
|---|---:|---:|---:|
| Vulkan, pequenas | 23,674 | 87,092 | 0,272×; regressão |
| Vulkan, preenchimento | 1050,952 | 154,034 | 6,823× |
| D3D12, pequenas | 23,300 | 8,771 | 2,657× |
| D3D12, preenchimento | 1253,688 | 18,620 | 67,329× |

Cada caso submeteu 2560 primitivas GPU em dez lotes medidos, zero fallback.
Timestamps GPU e tempos CPU de upload, submissão, espera e readback estão
separados nos CSV locais `out/gs-{vulkan,directx}-{small,fill}.csv`. No Vulkan
pequeno, somente o memcpy de readback consumiu 369,649 ms nas cinco rodadas;
o tempo GPU total foi 21,719 ms. D3D12 pequeno teve 3,888 ms GPU total.
Houve variação relevante entre amostras; estes ganhos não são FPS do jogo.

## Readback Vulkan medido e corrigido

`e626526` prefere memória `HOST_VISIBLE | HOST_COHERENT | HOST_CACHED`, quando
disponível, conservando o tipo coerente original como alternativa. Nenhuma
regra GS ou shader mudou. O diagnóstico encontrou memória coerente sem cache
selecionada antes de um tipo coerente com cache no mesmo dispositivo.

Comparação com os dois executáveis estáticos preservados, mesmo compilador,
afinidade 16384, prioridade AboveNormal, warmup e cinco rodadas alternadas
CPU/GPU, 512 primitivas. Todos os dados produzidos permaneceram equivalentes.

| Vulkan fill | Antes | Depois |
|---|---:|---:|
| Rasterização + sincronização, mediana/lote | 157,723 ms | 80,406 ms |
| Controle CPU, mediana/lote | 962,228 ms | 948,142 ms |
| Readback CPU, total das cinco rodadas | 371,687 ms | 5,792 ms |
| Timestamps GPU, total das cinco rodadas | 353,647 ms | 380,207 ms |

Readback caiu **98,44%**; o lote completo caiu **49,02%**. O trabalho GPU
isolado ficou **7,51% mais lento**, portanto não atribuir o ganho ao shader.
O caso pequeno foi repetido em ordem inversa: apresentou variação importante
de CPU/espera e não sustenta uma razão única de ganho total. A redução de
readback repetiu-se: 365,276–466,541 ms antes, 5,275–7,878 ms depois.
Não houve controle integral de carga externa no host compartilhado.

D3D12 contemporâneo mediu 8,526 ms no caso pequeno e 17,136 ms no fill;
seus controles CPU foram 22,673 e 1886,701 ms. A variação impede usar esses
números para uma comparação absoluta entre APIs. Resultados e identidades
dos executáveis: `out/gs-controlled-results.json` e
`out/gs-controlled-repeat-small-results.json`; CSVs preservam cada amostra.
Após a mudança, ambas as suítes GPU passaram novamente, inclusive assertivas
que comprovam um lote ainda pendente antes do save e sua execução durante
a materialização do checkpoint: Vulkan 6,56 s, D3D12 6,88 s.

## Super Bust-A-Move: equivalência e ausência de ganho comercial

Os checkpoints históricos de um e 30 milhões foram restaurados com as mesmas
identidades de firmware, NVM, disco e cenário. O AOT R5900 e seus módulos
originais foram recompilados juntos; não houve alteração de conteúdo, raízes
ou refinamento de hipóteses. Afinidade 16384 e prioridade AboveNormal iguais
à referência anterior. Cinco rodadas de 30 milhões alternaram CPU/Vulkan/D3D12;
validações separadas verificaram RGBA, PCM, VRAM, máscaras, ciclos EE/IOP,
cobertura e hash do estado completo. Roundtrip de 10 + 20 milhões também
coincidiu com a execução contínua nos três motores.

| Seleção | Tempo/30 milhões | Milhões de instruções/s | Campo real, mediana |
|---|---:|---:|---:|
| CPU contemporâneo | 9,101849 s | 3,2960 | 1,457251 s |
| Vulkan selecionado | 9,061455 s | 3,3107 | 1,474698 s |
| D3D12 selecionado | 9,161842 s | 3,2745 | 1,485833 s |

**Os backends GPU executaram zero primitivas nessas janelas.** Houve quatro
fallbacks na janela curta e 35 na longa. Logo, esta coleta comprova fallback
e equivalência, não rasterização nem aceleração comercial. As diferenças
pequenas entre seleções não constituem ganho GPU. A sequência rasterizada na
GPU é a fixture própria de comandos GIF/GS descrita acima.

Comparado aos 3,449 milhões/s e 1,422985 s/campo históricos, o controle CPU
contemporâneo ficou 4,44% abaixo em throughput e 2,41% mais lento por campo.
Não foi reensaiado o executável histórico pareado, portanto a causa dessa
diferença não está isolada. Não atribuí-la à GPU, a FPS de apresentação ou
ao VU1: nenhuma instrução VU1 comercial nova foi fabricada para esta prova.

O checkpoint atual do menu foi executado por um milhão nos três motores:
estado completo idêntico, 100% AOT R5900 nessa janela, **zero comandos GS**.
Isso também não comprova raster comercial nem avanço visual do menu.
Todos os ensaios foram limitados a 90 segundos por processo e encerrados.
Evidência local: `out/gs-real-evidence.json`, `out/gs-real-summary.json` e
os resultados `validation`, `performance`, `split` e `menu`.

O pico de working set foi amostrado com o processo vivo a cada 50 ms. Na
janela histórica, aproximadamente 289,4 MB nos três modos; o carregamento
do checkpoint domina esse pico. No menu: CPU 197,69 MB, Vulkan 247,44 MB,
D3D12 272,49 MB. Não confundir esses picos do processo, que incluem driver
e restauração, com a soma dos buffers GS ou com memória exclusiva da GPU.

## Custo de compilação do primeiro caminho

Medição de compilação isolada do commit `e626526`: dois builds novos, fonte
congelada, mesmo MSVC 19.44, Release, profiling desabilitado e quatro workers;
CPU sem backends seguido de GPU Vulkan/D3D12, somente `ps2n_core`.

| Custo | CPU sem backends | Vulkan + D3D12 |
|---|---:|---:|
| Build do núcleo | 33,811 s | 34,033 s |
| Objetos | 17.553.313 bytes | 18.123.762 bytes |
| Biblioteca estática | 20.627.830 bytes | 21.283.440 bytes |

Uma amostra por configuração: +0,66% no tempo, +3,25% em objetos e +3,18%
na biblioteca; não há ganho de compilação demonstrado. DXC levou 0,164 s.
O auxiliar comercial, com os AOTs locais recompilados, tinha 11.838.976 bytes;
seu conteúdo inclui mudanças do harness e não isola sozinho o custo GPU.
Após congelar fontes para preservar trabalho paralelo, Vulkan e D3D12 foram
revalidados por um milhão contra o controle original: estado, 324 amostras
PCM, VRAM, máscaras, ciclos e cobertura idênticos. Tempos dessa última
validação com compilação concorrente não são evidência de desempenho.
Proveniência: `out/gs-first-path-e626526-final-evidence.json` e
`out/gs-first-path-e626526-build-cost.json`.

## Limites do primeiro caminho medido

Esta primeira versão mantém buffers GPU persistentes, mas faz upload/readback
integral de valores e máscaras em cada materialização: 8 MiB por direção.
O estado canônico ocupa 8 MiB; Vulkan mantém aproximadamente 8 MiB de buffers,
D3D12 aproximadamente 24 MiB entre default/upload/readback, além de alinhamento
e recursos do driver. A fila máxima ocupa 30 KiB. Não há cache de shaders por
título nem cópia de firmware/conteúdo protegido no repositório.

**Apresentação direta por textura/swapchain GPU ainda não está implementada.**
O framebuffer produzido pela GPU é apresentado pelo scanout/Qt existente após
readback. Reduzir cópias, stalls e materializações conservadoras exige outro
incremento comprovado de propriedade de páginas e apresentação; não declarar
esse custo eliminado. Texturas/AA1 GPU, gráficos físicos de console e GPU nas
outras arquiteturas não estão certificados por testes sintéticos locais.
O download convidado local→host (`TRXDIR=1`) continua recusado pelo núcleo.
Readback neste relatório significa sincronização interna entre CPU e GPU.
