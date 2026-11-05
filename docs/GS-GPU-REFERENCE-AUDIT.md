# GS GPU — auditoria de referências

Consulta de 05/11/2026. Esta análise descreve arquitetura e limites observados;
não incorpora código, shaders, tabelas nem dependências das referências.
O backend do ps2-native continua sendo implementação própria BSD-3-Clause.

## Revisões e licenças

- O [`.gitmodules` de sotc-vibe-pc](https://github.com/LightVelox/sotc-vibe-pc/blob/main/.gitmodules)
  seleciona `LightVelox/PS2Recomp`, branch `main`. O gitlink inspecionado em
  `External/PS2Recomp` fixa **`d06a4ab4a6902e0ae324f184c22581c3aa387c36`**.
  As referências PS2Recomp abaixo estão fixadas nessa revisão.
- [sotc-vibe-pc](https://github.com/LightVelox/sotc-vibe-pc/blob/main/LICENSE)
  e [PS2Recomp](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/LICENSE)
  declaram GPL-3.0. São referências de comportamento e problemas de engenharia;
  não há autorização do mantenedor para copiar, traduzir, adaptar ou vincular
  sua implementação ao projeto BSD.
- [ReXGlue](https://github.com/rexglue/rexglue-sdk/blob/main/LICENSE) declara
  BSD-3-Clause e identifica trechos derivados de Xenia. Também foi consultado
  somente como referência arquitetural, sem importação de implementação.

## GS do fork usado por sotc-vibe-pc

O [frontend](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/src/lib/gs/gs_frontend.cpp)
decodifica registradores, monta primitivas e captura estado por desenho.
A execução é delegada a backends CPU/GPU; um
[adaptador com thread](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/src/lib/gs/gs_threaded_backend.cpp)
transporta desenhos, CLUT, transferências, sincronização e apresentação em ordem.
Leituras e snapshots são executados no trabalhador com conclusão observável.

O [backend GPU](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/src/lib/gs/gpu/gs_gpu_backend.cpp)
usa **OpenGL**, não Vulkan. Mantém VRAM swizzled em storage buffer, estados e
primitivas em buffers separados, uploads agrupados e CLUT versionada. Há dois
caminhos de rasterização: compute com bins de 16×16 e lista ordenada de
primitivas por tile; hardware com shaders especializados e interlock ordenado
por pixel. Quando a variante hardware ainda não está pronta, utiliza compute.
Buffers circulares têm fences antes de reutilizar regiões em uso.

Os [shaders](https://github.com/LightVelox/PS2Recomp/blob/d06a4ab4a6902e0ae324f184c22581c3aa387c36/ps2xRuntime/src/lib/gs/gpu/gs_gpu_shaders.cpp)
calculam cobertura GS e operações de destino explicitamente, em vez de confiar
no blending padrão do host. Incluem alpha test/AFAIL, DATE, comparação/escrita
Z, máscaras, FBA e combinação inteira de cor. Texturas são lidas da VRAM pelo
endereçamento do formato, passam por CLUT/TEXA e nearest/bilinear, e depois por
TFX. A interpolação usa float/double em partes do caminho; essa opção não é
prova de equivalência à aritmética exata e às recusas do ps2-native.

O backend detecta dependências entre páginas lidas/escritas por framebuffer,
Z, textura, CLUT e transferências. Mudanças de alvo e hazards encerram lotes;
barreiras tornam escritas anteriores visíveis. Uploads diferidos podem conservar
páginas antigas para leituras já enfileiradas. A apresentação compõe os
circuitos CRTC na GPU e publica recursos GL compartilhados quando disponíveis;
o caminho alternativo usa readback síncrono ou slots com fences.
Snapshots e exportação drenam lotes e materializam VRAM/CLUT/transferências.

Limites relevantes da referência: `TextureFlush()` GPU está vazio; há saídas
silenciosas quando a inicialização falha e algumas leituras retornam zero.
Essas políticas não satisfazem os contratos locais. A rotina de triângulo
inspecionada comprova apenas a cobertura que implementa: não estabelece AA1
com cobertura parcial/alfa e supressão de escrita Z. Não usar a existência de
um renderer GPU ou screenshots como prova de precisão física ou de AA1.

## ReXGlue: contratos úteis, hardware diferente

A [implementação Vulkan](https://github.com/rexglue/rexglue-sdk/tree/main/src/graphics/vulkan)
separa processador de comandos, memória compartilhada, cache de texturas,
cache de alvos, cache de pipelines e gravação diferida. O
[processador](https://github.com/rexglue/rexglue-sdk/blob/main/src/graphics/vulkan/command_processor.cpp)
controla submissões, barreiras e entrega do framebuffer ao apresentador; o
[cache de alvos](https://github.com/rexglue/rexglue-sdk/blob/main/src/graphics/vulkan/render_target_cache.cpp)
trata interlock como garantia por pixel, mantendo barreiras explícitas para
acessos/endereçamentos que não recebem essa garantia. Essa separação orienta
responsabilidades, não a semântica GS: EDRAM/Xenos não substitui VRAM/GS.

## Comparação com o ps2-native e proposta independente

`src/gif.cpp` já entrega escritas ordenadas ao GS comum. `src/gs_renderer.cpp`
contém montagem, validação, interpolação, textura, blending, testes e efeitos;
`src/gs_vram.cpp` contém endereçamento e **máscaras de bits conhecidos**.
As fixtures `gs_m29.cpp`, `gs_triangles.cpp`, `gs_filter.cpp`, `gs_ct16.cpp` e
`gs_events.cpp` verificam esses contratos, inclusive recusas, feedback de
textura, TEXFLUSH, AA1 experimental e apresentação. Local→host ainda é recusado
no runtime CPU: um backend novo não pode anunciar suporte que não existe.

O primeiro caminho independente deve aceitar somente primitivas/estados cuja
equivalência esteja demonstrada e manter a validação/montagem comuns. Compute
inteiro evita depender de regras de cobertura/blending implícitas do host.
Cada endereço físico deve ter escritor determinístico; largura zero, wrap,
alias framebuffer/Z e feedback exigem prova ou fallback antes da submissão.
O proprietário lógico continua sendo `GsVram`, incluindo valores, máscaras e
contadores. Um espelho GPU é recurso de execução, nunca um segundo dispositivo
GS ou uma segunda fonte de estado convidado.

As fronteiras CPU/GPU precisam materializar escritas pendentes antes de
fallback, transferência/CLUT CPU, leitura, checkpoint e restauração.
Primitivas dependentes permanecem em ordem. Um backend inicialmente síncrono
pode comprovar correção, mas o custo de submit/readback precisa ser medido e
declarado; eliminar essas cópias exige rastreamento de propriedade e scanout
GPU comprovados, não somente uma interface abstrata.
O [modelo de sincronização Vulkan](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)
exige dependências de execução **e** visibilidade de memória; ordem de gravação
do command buffer sozinha não prova visibilidade entre shaders/host.

Vulkan deve continuar opcional em build e runtime. No macOS,
[MoltenVK](https://github.com/KhronosGroup/MoltenVK/blob/main/README.md)
traduz Vulkan/SPIR-V para Metal; sua presença não garante as capacidades
necessárias. Descobrir recursos do dispositivo e suportar enumeradores de
portabilidade quando necessários; ausência ou recusa preserva o CPU. Não
introduzir OpenGL 4.6, interlock ou AVX2 como requisito global.

Nenhuma execução do renderer de referência foi feita nesta auditoria.
Resultados de equivalência e desempenho do backend próprio pertencem ao
relatório de implementação, com os dados realmente medidos.

## Direct3D 12 adicional

O pedido adicional de DirectX foi comparado por leitura com o projeto local
`x360` (sem modificá-lo): sua seleção Windows distingue Direct3D 12 e Vulkan.
O ps2-native utiliza seu próprio contrato `GsGpu` e shader original HLSL,
com a mesma semântica inteira do shader Vulkan. Não importa implementação de
Xenos, runtime do Xbox ou patches por título.

O backend exige hardware com
[`Int64ShaderOps`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options1)
para cobertura exata e Shader Model 6.0. Constantes de raiz e UAVs seguem o
[contrato Direct3D](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures-overview).
Falha/timeout de submissão encerra o dispositivo com
[`ID3D12Device5::RemoveDevice`](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-removedevice)
antes de liberar os recursos; runtime sem essa API conserva o caminho CPU.
DXC compila HLSL durante o build, sem dependência do compilador no runtime.
