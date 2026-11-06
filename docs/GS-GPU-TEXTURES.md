# GS — sprites texturizados em Vulkan e Direct3D 12

## Captura que orientou a implementação

Super Bust-A-Move, checkpoint local do menu revisado, mesma identidade de
ROM0/ROM1, console, NVM, disco e cenário. A captura de 30 milhões de
instruções usou o observador GIF existente, sem outro profiler. Os artefatos
comerciais permanecem locais em `out/`, ignorados pelo Git.

Foram observados 282 sprites PRIM `0x156`: 222 PSMT4 e 60 PSMT8. Todos
usaram UV/FST, nearest em minificação e magnificação, MODULATE, TCC RGBA,
blending e CLUT CT32/CSM1/CSA=0. PSMT4 foi a primeira cobertura escolhida
por corresponder a 78,7% dos sprites capturados. Dos sprites T4, 204 usaram
CLD=1/repeat, 12 CLD=0/repeat e seis clamp; os T8 usaram CLD=1/repeat.
As outras 12 primitivas da janela são limpezas sem textura. Todos os 294
comandos usam ZBUF `0x0A00008C`, PSMZ16S, sem ZMSK; os sprites usam TEST
`0x5000F`, com teste GEQUAL e escrita Z. Isso exigiu ampliar a profundidade
do backend existente para completar o caminho comercial. O backend anterior executou zero
primitivas nesta captura. Evidência: `out/gs-texture-capture-menu.log`.

## Contrato compartilhado

Commit de código `659ab18`, sobre `4fdd7dd`. Implementação própria
BSD-3-Clause, estendendo o setup e os backends compute
existentes. Não há outro runtime, GS, backend, estado de registradores ou
mudança no AOT R5900/VU1. Os sprites aceitam T4/T8 com CLUT e CT32/CT24,
com UV ou endpoints STQ convertidos por prova exata comum para 10.4.
Recorte e inversão preservam os endpoints originais da interpolação.

Cada comando contém o estado de textura, clamp/repeat, filtro, TEXA e a
paleta com bitmap de entradas carregadas no momento do kick. T4 remapeia
somente seu banco selecionado. O shader lê índices/texels diretamente da
VRAM residente; uma troca posterior de TEX0/TEX2 ou de CLUT não altera os
comandos já enfileirados. O carregamento de CLUT continua usando o acesso
coerente comum no momento da escrita do registrador.

Vulkan e D3D12 usam aritmética inteira de 64 bits para interpolação racional,
floor negativo, endereçamento swizzled, taps e combinação de canais. Nenhum
sampler ou rasterizador do host define arredondamento GS. MODULATE/DECAL,
TCC/TEXA, alfa/AFAIL, PABE/blending, FBMSK/FBA e profundidade mantêm o pipeline
existente. A prova de leitura do destino considera o alfa da textura.

PSMZ16S conserva o swizzle de páginas 64x64 e a permutação de profundidade.
Duas profundidades podem ocupar metades distintas da mesma palavra. Leituras
e atualizações de valores/conhecimento são atômicas nessas palavras; máscaras
e joins condicionais afetam somente a metade do pixel. Isso preserva o
vizinho sem depender da ordem dos workgroups. Z32/Z24 conservam seu caminho
anterior, e Z16 com framebuffer CT32/24 mantém a recusa CPU existente.

Nearest não introduz uma hipótese nova. Bilinear conserva o deslocamento
de meio texel e ignora taps com peso zero. Sem hipótese de truncamento,
um preflight CPU prova os canais exatamente representáveis antes de
publicar o dispatch. O bit experimental UV racional e o bit experimental
de truncamento horizontal/depois vertical selecionam a mesma aritmética nos
dois caminhos. LOD e precisão STQ continuam validados pelo setup comum;
casos sem prova seguem a recusa/fallback existente. Equivalência com o CPU
não certifica a precisão física dessas hipóteses.

Os pacotes de 1244 bytes ficam num buffer readonly de até 256 comandos.
Push/root constants contêm apenas o índice de 32 bits; não se aumenta o
requisito mínimo de push constants Vulkan. A paleta fica no offset 188 e
seu bitmap no offset 1212. Bytes de comandos são contados separadamente dos
bytes de VRAM. O SPIR-V embutido foi regenerado do GLSL próprio com glslang
16.6.0, `-V --target-env vulkan1.1 -Os`; HLSL compilado com DXC do SDK
10.0.26100.0, `cs_6_0 -O3`. Proveniência local:
`out/gs-texture-shader-provenance.json`.

## Dependências e conhecimento

Páginas de leitura de textura participam do upload, separadas das páginas
escritas pelo framebuffer/Z. Uma fonte GPU conhecida pode ser amostrada
por outro comando sem readback. Fontes somente lidas passam a residência
igual CPU/GPU após upload, evitando reenviá-las em lotes posteriores.

A prova por página é conservadora; quando insuficiente, a prova de bits do
texel preserva bytes/nibbles vizinhos desconhecidos. Paletas parcialmente
carregadas exigem somente as entradas realmente usadas. Fontes desconhecidas
ou precisões não provadas não são publicadas para execução parcial GPU.
As páginas efetivamente amostradas atualizam a validade de textura; o
superset usado para recusar feedback não inventa acessos ao cache convidado.

Uma escrita CPU em fonte/destino com leitores pendentes completa esses
leitores antes de alterar bytes. Alterações parciais materializam apenas as
páginas necessárias. Uma leitura CLUT de página sem escritor pendente não
drena sprites disjuntos: o snapshot preserva a versão anterior da paleta.
Uma limpeza flat incondicional de página inteira pode estabelecer uma prova
de conhecimento sem readback; máscaras, FBA e testes limitam essa prova.
CLUT, TEXFLUSH, transferências, fallback, scanout e
checkpoint conservam a ordem e os acessos coerentes existentes. Sobreposição
de textura/destino sem comportamento estabelecido continua recusada pelo
caminho CPU, antes de executar parcialmente o comando GPU. O esquema de
checkpoint convidado permanece inalterado; metadados de host não são salvos.

## Equivalência verificada

Windows x64, MSVC 19.44 Release, Ryzen 7 5700X3D, NVIDIA RTX 4070 física.
As suítes Vulkan e D3D12 passaram, cada uma com **1209 primitivas / 231 lotes**
(14,12 s e 24,76 s, sob carga de builds paralelos; tempos de testes não são
medida de desempenho). Treze testes GS CPU focados passaram. Build separado
com ambos os backends desabilitados: cinco testes passaram e os dois testes
de seleção GPU terminaram com skip 77 esperado, após seus controles CPU.
O launcher GPU foi recompilado com os mesmos AOTs e sua sessão própria
passou; builds finais incrementais confirmaram ausência de trabalho pendente.

As regressões comparam VRAM e conhecimento completos, RGBA e contadores,
incluindo T4/T8/CT32/24, páginas e bases desalinhadas, nibbles/bytes vizinhos,
paletas parcialmente carregadas, troca de CLUT/TEX2 com desenhos pendentes,
UV negativos/invertidos, scissor, quatro clamps, TEXA/TFX/TCC, STQ e filtros.
Incluem alfa/AFAIL/PABE, máscaras, FBA desconhecido, GEQUAL/GREATER/ZMSK,
Z16S nas duas metades da mesma palavra e na borda de páginas de 64 linhas,
joins condicionais, fonte GPU consumida sem readback/upload, alteração CPU
de fonte com leitores pendentes e alternância textura GPU → AA1 CPU → GPU.

A ROM sintética própria produz sprites T4 por EE→GIF→GS. Seu RGBA esperado
é calculado independentemente; estado completo, traces/barramento, ciclos,
interrupções e campos coincidem com o CPU. Um checkpoint salvo após o
primeiro kick texturizado ainda pendente continua até o mesmo resultado.
O comparador informa o primeiro byte de VRAM/conhecimento ou pixel/canal
RGBA diferente. Não houve divergência que exigisse alterar um golden.

Na janela comercial do menu, CPU `4fdd7dd`, Vulkan/D3D12 anteriores e as
três seleções de `659ab18` produziram o mesmo estado completo:
`80c2d0d3f6635d1b24934ca1853b006f592888bd7a9293daa4126c10cf4da9a5`.
São 30 milhões de instruções (25.446.236 AOT, 4.553.764 interpretadas), seis
campos 640x224, 9766 amostras PCM e 12 polls SIO2 com botões `FFFF`.
Continuar de um checkpoint intermediário de 10 + 20 milhões também coincidiu
em CPU, Vulkan e D3D12. VRAM e máscaras completas, revisões/escritas, ciclos
EE/IOP, IRQ e áudio permanecem no estado comparado; não se usa somente imagem.

Os dois backends novos executaram **282 sprites texturizados (222 T4 + 60 T8)
e 12 limpezas flat**, 294 primitivas em 108 lotes, com **zero fallback nessa
janela**. A versão anterior tinha 282 fallbacks de textura e 12 de profundidade.
Um decorador privado somente na validação conta comandos concluídos por
formato; encaminha a execução sem alterar pacotes e não participa da medição.

O segundo campo já contém **47 sprites texturizados rasterizados pela GPU**,
além das duas limpezas. Seu RGBA tem SHA-256
`c55faed76dd367e5b65786702b8faf89177b60dc3afba8e566eb652846bfba1e`.
Os BMPs CPU/Vulkan/D3D12 são idênticos. Assim, a imagem principal não é
apenas o scanout restaurado do checkpoint ou uma limpeza acelerada.
Arquivos locais: `out/gs-texture-validation-{newcpu,newvk,newdx}-frame.bmp`.

Após autorização explícita de Computer Use, o manifesto local separado
`out/game-first/interactive-gpu-textures.json` selecionou Vulkan na janela
experimental existente. O GAME MENU original foi observado, com polls SIO2
e PCM com sinal/enviado pelo sink Windows. A sessão foi fechada antes de
60 segundos de execução; o processo terminou com código 0. Essa observação
visual não mede desempenho nem certifica continuidade de áudio em tempo real.
Manifestos/checkpoints originais e dados do usuário não foram substituídos.

Evidência local: `out/gs-texture-capture-summary.json`,
`out/gs-texture-validation-results.json`,
`out/gs-texture-split-validation-results.json`,
`out/gs-texture-code-provenance.json` e
`out/gs-texture-cpu-launcher-validation.json`.

## Desempenho comercial

Baseline exato `4fdd7dd` e renderização `659ab18`, com núcleo e todos os AOTs
recompilados juntos em builds separados. Mesma máquina, Release MSVC 19.44,
afinidade `16384`, prioridade AboveNormal, checkpoint, cenário e entrada.
Cada uma das seis seleções recebeu aquecimento e cinco rodadas em ordem
rotativa/alternada: 36 execuções limitadas a 90 segundos por processo,
incluindo aquecimento. Nenhuma compilação ou sessão de launcher permaneceu
ativa durante a coleta. Os estados finais coincidiram em todas as execuções.

O timer cobre as 30 milhões de instruções e o scanout dos campos originais.
No modo de desempenho, o callback registra somente timestamps e contadores;
hashes de estado/VRAM são calculados depois do timer. A validação separada
acima verifica RGBA, áudio e entrada. O tempo de campo é a mediana dos
intervalos entre VBLANKs distintos do convidado, não o FPS da janela.

| Seleção | Mediana / 30 milhões (s) | Campo original (s) | Faixa das cinco rodadas (s) |
|---|---:|---:|---:|
| CPU `4fdd7dd` | 10,760350 | 1,702535 | 10,162206–12,235603 |
| Vulkan anterior | 10,605092 | 1,659876 | 10,227996–23,283718 |
| D3D12 anterior | 11,345907 | 1,898671 | 10,918066–23,129886 |
| CPU `659ab18` | 10,833811 | 1,911404 | 10,468019–22,013907 |
| Vulkan com texturas | 10,555834 | 1,696387 | 9,862439–10,810998 |
| D3D12 com texturas | 10,651491 | 1,736655 | 10,277157–22,806942 |

As medianas do lote caíram 0,46% no Vulkan e 6,12% no D3D12 contra as
respectivas seleções anteriores; contra o CPU contemporâneo, 2,57% e 1,68%.
**Não há ganho total robusto demonstrado por essa coleta.** Os controles
também sofreram desaceleração transitória para 22–23 segundos, sem dobrar
o tempo de CPU total do processo (que inclui carga/hash fora do timer).
Uma consulta read-only do host encontrou carga de navegador/helper, sem
build ou launcher ativo. Desagendamento externo é uma inferência sustentada
por esses tempos, não um custo atribuído ao shader. A coleta conserva todas
as amostras; não descarta outliers para produzir uma razão favorável.

Durante a janela, ambos os backends enviaram 13.172.736 bytes de VRAM/máscaras
e baixaram 7.536.640 bytes, em 74 sincronizações de upload e cinco readbacks.
Foram 108 submissões/esperas e 365.736 bytes de comandos, com 1296 bytes por
direção para resultados de lotes. As inspeções/hash posteriores não entram
nesses contadores. Os buffers próprios ocuparam 8.707.088 bytes no Vulkan e
25.755.648 no D3D12; a fila máxima ocupa 318.464 bytes no host.

Os timestamps GPU medianos somaram 51,35 ms (Vulkan) e 6,70 ms (D3D12) por
janela de 30 milhões. Upload/submissão/espera/readback no host consumiram
aproximadamente 1,01%/0,88% da janela em Vulkan/D3D12. Isso localiza a maior
parte do tempo fora dessas operações do backend. O perfil separado abaixo
atribui o custo interno do pipeline CPU; seus tempos instrumentados não entram
na tabela acima. A cobertura comercial aumentou, mas a sessão permanece muito
abaixo do tempo real e a apresentação ainda exige scanout/readback CPU.

Amostras, identidade dos executáveis e contadores:
`out/gs-texture-performance-results.json`,
`out/gs-texture-performance-summary.json` e
`out/gs-texture-final-evidence.json`.

## Gargalo diagnosticado com o profiler existente

Após encerrar a medição normal, um build separado ativou
`PS2N_ENABLE_PROFILING=ON`, recompilando o mesmo núcleo e todos os AOTs com
a definição propagada também aos cabeçalhos inline. Os binários e a
biblioteca de `659ab18` ficaram congelados antes do diagnóstico CI posterior.
Três execuções limitadas a 180 segundos conservaram scanout, entrada e o
mesmo estado completo/campos; não calcularam hashes durante a janela.
Nenhum profiler novo ou instrumentação permanente de pixels foi adicionado.

| Fase exclusiva instrumentada | CPU | Vulkan | D3D12 |
|---|---:|---:|---:|
| Sincronização | 23,85% | 24,92% | 24,08% |
| Execução EE | 23,64% | 23,60% | 23,92% |
| Memória | 18,97% | 18,42% | 18,95% |
| IOP | 11,54% | 11,46% | 11,57% |
| Dispositivos | 9,18% | 9,62% | 9,59% |
| Guardas de código | 6,61% | 6,89% | 6,76% |
| Apresentação/observação | 3,04% | 3,07% | 3,09% |
| GS | 2,19% | 1,03% | 1,03% |

A janela teve 65.205.240 chamadas de sincronização/IOP, 35.093.967 de memória
e 25.446.236 guardas AOT. O limite identificado fica na execução/sincronização
do sistema na CPU, com GS já pequeno nessa sequência. Esses dados orientam
o próximo perfil; não autorizam retirar guardas, polling, áudio ou efeitos.
Os timers por acesso têm overhead e medem tempo de parede, incluindo
desagendamento: as execuções instrumentadas levaram aproximadamente
29,8–30,6 segundos, **fora da tabela de desempenho**. As porcentagens não
são uma estimativa exata do custo sem instrumentação, nem um ganho de FPS.

Evidência: `out/gs-texture-profile-results.json`, logs `gs-texture-profile-*`
e `out/gs-texture-profile-frozen-provenance.json`. O rebuild posterior do
diagnóstico alterou a biblioteca compartilhada de `out/gs-gpu-build`,
sem alterar os executáveis estáticos medidos ou a biblioteca do perfil.

## Disponibilidade Direct3D 12

`bc819a3` acrescenta fases opcionais de inicialização com flush em stderr:
`PS2N_GS_GPU_DIAGNOSTICS=trace` registra somente diagnóstico; `1` também
solicita DRED, como antes. O modo normal não emite esses logs. As duas
configurações passaram na RTX 4070 com 1209 primitivas/231 lotes.

Os runners Windows anteriores expiraram em 90 segundos dentro do preflight,
após criar pipeline e buffers. O adaptador anunciado era Microsoft Basic
Render Driver, com Flags=0, Int64 e SM6.0; DRED estava desligado no reensaio.
Esses dados não sustentam atribuir a falha a DRED ou à criação do pipeline.

`9ce2fbd` reforça a seleção de hardware já existente: consulta
[`EnumWarpAdapter`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgifactory4-enumwarpadapter)
e compara o
[`AdapterLuid`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc)
verificado com cada candidato, além da flag SOFTWARE. Uma coincidência
identifica WARP pela API do sistema antes de criar seu dispositivo. Não há
filtro por nome, fabricante, jogo ou revisão. Se a consulta opcional não
estabelecer a identidade, nenhum LUID zero vira prova: as verificações
anteriores de flags/capacidades permanecem e o diagnóstico informa a ausência.

O motivo de indisponibilidade distingue WARP explicitamente quando ele foi
identificado. A RTX 4070 apresentou identidade diferente e continuou
executando todas as regressões, também com DRED. `7e16ecc` separa o teste
D3D12 no workflow para registrar sua saída verbose em sucesso/skip,
mantendo uma única execução e o mesmo deadline. Os resultados da matriz
final registram essa classificação, sem transformar skip em teste GPU.

O código final `9ce2fbd` foi recompilado em build exclusivo com todos os AOTs
e o launcher. Três novas continuações comerciais de 30 milhões (CPU, Vulkan,
D3D12) repetiram estado `80c2d0d3…`, seis campos, PCM, entrada e contadores
iguais às referências; ambos os backends mantiveram 282 sprites texturizados
e 12 limpezas, 108 lotes e zero fallback. Os BMPs coincidiram com a captura
original. `launcher-experimental-session` passou em 13,09 s; a UI não foi
reaberta. São validações do código final, sem substituir a coleta de
desempenho congelada em `659ab18`, cujo shader/pipeline de execução não mudou.
Evidência: `out/gs-texture-9ce2fbd-final-validation.json`.

## Referências e limites

Foi consultado o backend do fork LightVelox/PS2Recomp fixado em
`d06a4ab4a6902e0ae324f184c22581c3aa387c36`, usado pelo sotc-vibe-pc:
[dependências e versões de CLUT](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/src/lib/gs/gpu/gs_gpu_backend.cpp)
e [amostragem nos shaders](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/src/lib/gs/gpu/gs_gpu_shaders.cpp).
A consulta foi arquitetural; não houve cópia, tradução ou vínculo de código
GPL. Contratos de sincronização continuam na
[auditoria existente](GS-GPU-REFERENCE-AUDIT.md).

Triângulos texturizados, AA1/Gouraud, formatos não modelados, mipmaps e
profundidade fora do subconjunto compute mantêm o renderer CPU. Não há
apresentação por swapchain, XFF, alteração de launcher ou alegação de
fidelidade física/tempo real nesta descrição de implementação.
