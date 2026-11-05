# GS acelerado — primeiro caminho Vulkan e Direct3D 12

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

## Equivalência local

Windows x64, MSVC 19.44 Release, NVIDIA GeForce RTX 4070. As suítes Vulkan e
D3D12 executaram **487 primitivas / 288 lotes** cada. Comparam toda a VRAM de
4 MiB e as máscaras de 4 MiB, RGBA, revisões e escritas, em vez de screenshots.
Incluem CT32/24, winding/subpixel/scissor, alpha/AFAIL, blending/PABE, máscaras,
Z, dependências dentro de um lote, recusa de dados desconhecidos, fallback de
textura/AA1, transferências, TEXFLUSH, FINISH/SIGNAL, cópias e save/load.

Uma ROM sintética própria executa comandos reais EE→DMA→GIF→GS e produz o
golden RGBA `43564b222dee0155325d1db289518d63f6a02af60690dd6da83ad69d002e3f54`.
Estado completo, trace/barramento, ciclos/IRQ, eventos de campo e continuação
após checkpoint são iguais CPU/GPU. O IOP executa instruções próprias válidas,
sem usar um estado que já recusou execução como checkpoint.

Build separado com ambos os backends desabilitados: quatro testes M2.9
passaram; os dois testes GPU comprovaram raster/scanout CPU antes de skip 77.
Build desses alvos: 50,03 s, sob carga de compilação concorrente; essa medida
não demonstra melhoria de compilação. Não requer GPU, AVX ou OpenGL global.

## Benchmark sintético inicial

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

## Limites

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
