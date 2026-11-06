# STATUS — ps2-native

**Registro vivo de progresso e evidências.** Atualizar este arquivo quando houver avanço **verificado**; não reescrever os READMEs, `AGENTS.md` ou `CLAUDE.md` a cada marco. Relatórios detalhados continuam em `docs/M*-STATUS.md`; o trabalho seguinte fica em `docs/NEXT-TASK.md`.

## Runtime: eventos e tradução TLB — 05/11/2026

- `c23188e` evita sincronização sem evento possível e reutiliza somente
  índices TLB comprovados por página/ASID. Mantém bytes, permissões,
  efeitos, relógios, recusas, isolamento e esquema de checkpoint.
- Cinco trios do menu: 30 milhões em **11,341572 → 7,776684 s**;
  campo original **1,838974 → 1,254745 s**. Ganho mediano de throughput
  45,84%; CPU total do processo −28,18%. Houve interferência externa;
  a combinação venceu todos os pares. Não demonstra tempo real.
- Estado completo, seis campos RGBA, VRAM/máscaras, 9766 amostras PCM,
  entrada e ciclos iguais, inclusive interpretação pura, 10 + 20 milhões
  e Vulkan/D3D12. Vinte testes focados passaram; launcher local recompilado.
- CI do novo commit impedida: revisão automática recusou o push por faltar
  autorização específica de publicação. Métodos, amostras e limites em
  [RUNTIME-PERFORMANCE](docs/RUNTIME-PERFORMANCE.md). Nenhum ensaio ativo.

## Sprites comerciais na GPU — 05/11/2026

- `659ab18` estende o GS compute existente com sprites T4/T8/CT32/24,
  CLUT por comando, UV/STQ comprovado, filtros inteiros e Z16S atômico.
  TEXA/TFX, alfa/blending, máscaras e conhecimento conservam o caminho
  comum; AOT R5900/VU1, firmware e clocks não foram alterados.
- RTX 4070: Vulkan e D3D12 passaram 1209 primitivas/231 lotes cada,
  com pixels esperados independentes, VRAM/máscaras completas, RGBA,
  estado/ciclos/IRQ e save/load. Treze testes CPU focados passaram;
  build sem backends passou cinco testes e dois skips GPU esperados.
- Super Bust-A-Move, menu, 30 milhões: **282 sprites texturizados GPU
  (222 T4 + 60 T8), 12 limpezas e zero fallback na janela** nos dois
  backends. Estado completo, seis campos, 9766 amostras PCM e 12 polls
  SIO2 coincidiram com CPU e GPU anterior, inclusive em 10 + 20 milhões.
  O segundo campo já contém 47 sprites texturizados GPU. O menu também
  foi observado na janela experimental Vulkan, encerrada com segurança.
- Cinco rodadas alternadas com aquecimento: Vulkan 10,555834 s/30 milhões
  e 1,696387 s/campo; D3D12 10,651491 s e 1,736655 s/campo. Controles
  sofreram carga externa transitória; **sem ganho total robusto nem
  tempo real demonstrados**. Bytes, sincronizações, amostras e limites
  estão em [GS-GPU-TEXTURES](docs/GS-GPU-TEXTURES.md).
- O profiler existente aponta sincronização, EE, memória e IOP como limite;
  GS ficou em cerca de 1% do tempo instrumentado nos caminhos GPU.
- Triângulos texturizados, AA1/Gouraud e casos sem prova mantêm CPU;
  apresentação direta e gameplay completo continuam não demonstrados.

## Primeiro caminho GS compute Vulkan e Direct3D 12 — 05/11/2026

Medições de `92dc21c`/`e626526` e seleção verificada em `e9bbc21`.
Incrementos posteriores do trabalho paralelo têm relatórios próprios.

- `92dc21c` integra rasterização GPU real de primitivas flat CT32/24 no GS
  compartilhado, com blending/testes/Z, máscaras conhecidas, fila ordenada,
  fallback CPU e checkpoints. Texturas, CLUT, filtros e AA1 conservam o
  renderer CPU e suas recusas. AOT R5900/VU1 e firmware não foram alterados.
- RTX 4070: 487 primitivas GPU/288 lotes por backend, VRAM/máscaras/RGBA e
  estado completo equivalentes. Doze testes focados passaram, incluindo
  launcher, checkpoints e VU1 AOT; CPU sem dependências GPU também passou.
  `e626526` reduz readback Vulkan medido em 98,44%; lote fill de 157,723
  para 80,406 ms. Caso pequeno teve variação e não sustenta ganho único.
- 29 execuções comerciais preservaram estado, RGBA, PCM, ciclos e cobertura,
  inclusive 10 + 20 milhões. As janelas históricas executaram **zero
  primitivas GPU**, com fallback verificado; **sem ganho comercial GPU
  demonstrado**. O controle contemporâneo mediu 3,296 milhões/s e 1,457 s
  por campo, abaixo da referência histórica de 3,449/1,423.
- `e9bbc21` verifica execução D3D12 real antes de disponibilizar o backend e
  conserva diagnóstico nativo/DRED opcional. CI do
  [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/42132234795) e
  [launcher](https://github.com/fabegalo/ps2-native/actions/runs/42132234724)
  aprovada nos seis alvos, incluindo VU1 AOT/CLI. DirectX ARM64 compilou e
  recusou disponibilidade antes da seleção; CPU validado, GPU não certificada.
- Apresentação usa o framebuffer original via scanout/Qt; apresentação GPU
  direta e texturas/AA1 GPU permanecem pendentes. Os incrementos residentes
  `da485fc`/`580d381` do trabalho paralelo foram preservados. Método, custos
  e limites em [GS-GPU-STATUS](docs/GS-GPU-STATUS.md).

## Estado funcional verificado — 05/11/2026

**Protótipo experimental com launcher local e sessão de checkpoint ao vivo. Não executa o Browser original até o fim nem declara jogos comerciais jogáveis.**

| Componente | Evidência até este ponto | Limite |
|---|---|---|
| AOT MIPS/R5900 | ELF32 sintéticos, CFG conservador, relocations como evidência, geração C++ e testes end-to-end | Cobertura incompleta de jogos e código dinâmico |
| Boot EE | ROM0 reais, COP0/TLB, memória, RDRAM, início do kernel em RAM e AOT híbrido com trace | CPU e dispositivos ainda parciais |
| Direct ELF | BOOT2 de ISO pelo loader comum, ambiente bare-metal, Count/Compare, SYSCALL/BREAK para handler convidado, usuário/supervisor/ERL e overlays AOT verificados | Não instala kernel/serviços; ELF comercial executado, sem imagem ou jogabilidade |
| IOP | Inicialização R3000A, NVM externa, MECHACON, timers/VBLANK, SIF e EE→IOP SIF1 com bytes reais | Sem linker IRX, SIF0, RPC ou serviços completos; DMA/timing ainda parciais |
| GS/GIF | VRAM 4 MiB, CT32/24 e índices T4/T8, sprites UV/STQ, CLUT/blending, PATH2/3 e scanout; sprites texturizados e Z16S nos backends Vulkan/D3D12, com menu comercial equivalente; AA1 experimental CPU | Primeira partida não demonstrada; boot original não concluído; hipóteses explícitas e formatos/efeitos/timing parciais |
| SPU2 | Síntese ADPCM/Gaussiana, pitch, ADSR, KON/KOFF, loop/ENDX/NAX, IRQ de RAM e mix estéreo; PCM ao vivo no Windows ouvido pelo mantenedor; checkpoints completos | Noise, sweeps e fontes audíveis de efeitos/ADMA ainda recusados; timing físico não certificado; som entrecortado pela lentidão |
| Launcher desktop | `ps2-native` (Qt 6 opcional): biblioteca, diagnóstico e Modo de Teste Experimental com checkpoint verificado, thread própria, GS, teclado/XInput e PCM ao vivo | Jogar e Browser originais bloqueados; 0–1 FPS observados na sessão; gamepad físico e sequência manual completa ainda não validados; sem release |
| Distribuição | Builds/testes sintéticos nas seis arquiteturas de CI | Builds não comprovam funcionamento do firmware/jogo em cada host; sem release |

## VRAM residente — 05/11/2026

- `da485fc`/`580d381`/`4fdd7dd`: coerência por páginas, uploads/readbacks
  seletivos, preservação de máscaras e conclusão ordenada de fallback,
  scanout e checkpoint. 25 testes focados passaram, incluindo as duas APIs
  na RTX 4070 e sessão Qt com PCM/entrada equivalentes.
- Cinco pares intermediários demonstraram redução de 98,05% no readback e
  ganho D3D12 sintético; também revelaram regressão CPU, tratada em `4fdd7dd`.
  A amostragem final está incompleta. CI do núcleo aprovada nos seis alvos;
  launcher aprovado em cinco, com Linux x64 pendente.
  As janelas comerciais verificadas nessa revisão usaram zero primitivas
  GPU; sem ganho comercial ou gameplay novo demonstrado.
  Evidência e limites em [GS-GPU-RESIDENCY](docs/GS-GPU-RESIDENCY.md).

## Particionamento estável AOT — 05/11/2026

- `3c3a5b5` integra unidades nativas por faixas estáveis de 64 KiB,
  nomes por SHA-256 e manifesto verificado pelo CMake/`ps2n-dev`.
  Corpos, guardas, semântica e cobertura são preservados; não há código
  de referência reutilizado nem mudança de gameplay/firmware.
- Cinco rodadas com afinidade igual: 30 milhões em 10,372600 → 8,699110 s,
  2,892 → **3,449 milhões de instruções/s** (**+19,24%** sobre a referência
  anterior ao fetch/eventos, **+5,39%** sobre a TU única já otimizada).
  Campo real: 1,696340 → **1,422985 s**. Sem alegação de tempo real.
- Trinta execuções finais e oito validações adicionais preservaram estado
  completo, RGBA, PCM, ciclos e interrupções, inclusive interpretação pura
  e checkpoint de 10 + 20 milhões. Cobertura 99,0868%/85,979867%; raízes
  explícitas e fallback permanecem. Quatro testes locais pertinentes passaram.
- Executável **11,23 MiB**, 512 bytes menor que a TU única contemporânea.
  Build completo: 124,015 → 131,721 s, sem ganho (+6,21%). Incremental:
  125,877 → 26,867 s (**−78,66%**), com `.text` idêntico; no-op 3,378 s.
  Cabeçalhos alterados continuam exigindo rebuild das unidades dependentes.
  Método, amostras, referências ReXGlue/PS2Recomp e limites em
  [AOT-STRUCTURAL-PERFORMANCE](docs/AOT-STRUCTURAL-PERFORMANCE.md).
- CI do [núcleo `3bf35c9`](https://github.com/fabegalo/ps2-native/actions/runs/42112647260)
  e do [launcher `3c3a5b5`](https://github.com/fabegalo/ps2-native/actions/runs/42110207761)
  aprovada nos seis alvos. Nenhum ensaio comercial permanece ativo.

## Memória e eventos AOT medidos — 05/11/2026

- `6f3f968` especializa fetch com tradução, conhecimento e guardas de
  bytes preservados; `ac7e405` evita trabalho de VU/VIF/DMA antes de
  eventos possíveis, conservando Q pendente, timestamps e recusas.
  Técnicas selecionadas após comparação com PS2Recomp, sem código GPL.
- Cinco rodadas alternadas: fetch isolado **+3,93%**, eventos **+16,53%**
  na janela longa; combinação **+17,62%**, de 2,859 para **3,363 milhões
  de instruções/s**. Campo real mediano: 1,714135 → **1,455465 s**.
  Janela curta: 2,744 → 3,186 milhões/s (**+16,10%**). Controle atual
  reensaiado; cobertura AOT inalterada. Não demonstra tempo real.
- Estado completo, sete/seis campos RGBA e PCM idênticos ao baseline e
  à interpretação em duas janelas, inclusive após salvar/carregar em
  10 + 20 milhões. 20 testes focados passaram. Executável: +512 bytes,
  11,2266 MiB; recompilação AOT integral 99,205 s, sem ganho de build
  demonstrado. Nenhuma mudança no gerador, aritmética ou firmware.
- CI do código `ac7e405`: [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/42105484315)
  e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/42105484103)
  aprovados nos seis alvos Windows/Linux/macOS, x64 e ARM64.
  Nenhum ensaio comercial permanece ativo.
- Comparação, amostras sem ganho adicional e limites em
  [AOT-STRUCTURAL-PERFORMANCE](docs/AOT-STRUCTURAL-PERFORMANCE.md).

## Otimização AOT medida — 05/11/2026

- `4b113b0` corrige a expectativa antiga de STQ em `native-m29-drawing`;
  [CI](https://github.com/fabegalo/ps2-native/actions/runs/41993658530)
  passou nos seis alvos. `4651b36`/`d0fdc39` especializam operandos/GPR,
  encadeiam delay slots, reutilizam despacho e permitem retomada nativa
  no interior de blocos, preservando guardas por instrução e semântica única.
- Cinco pares comerciais de 30 milhões: throughput **+9,50%**, de 2,607
  para 2,854 milhões de instruções/s; campo real mediano de 1,882 para
  1,720 s. Na janela curta, **+12,44%**. Cobertura AOT 99,0868%/85,979867%;
  raízes explícitas anteriores continuam necessárias nesse comparativo.
- Estado completo, RGBA e PCM idênticos à referência, inclusive após
  salvar/carregar em 10 + 20 milhões; 27 testes locais passaram. Custo:
  build AOT 3,63× e executável local 4,60 → 11,23 MiB. Sem alegação de
  tempo real. [Relatório e limites](docs/AOT-PERFORMANCE-STATUS.md).
- CI do código `d0fdc39`: [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/41999528499)
  e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/41999528308)
  aprovados nos seis alvos. Trabalho paralelo do jogo preservado;
  nenhum ensaio comercial permanece ativo.

## Reensaio curto de Simpsons — 05/11/2026

- Diagnóstico híbrido com RTC explícito superou ReadRTC: janela de
  120.000.000 instruções do jogo em 43,2943 s. Novo bloqueio no IOP
  `0x267DC` / 17.733.070: escrita em CDVD HOWTO `0x1F402006` solicita
  comportamento fora do modo ocioso de dados 2048/`0x80` suportado.
- Zero GIF/pixels/frames do jogo; PMODE não escrito. Continua antes da
  abertura/menu. Injeção experimental após firmware parcial, não boot normal.
  Sem mudança no núcleo, benchmark, testes adicionais ou CI. Ensaio encerrado;
  [evidência](docs/FIRST-GAME-STATUS.md#reensaio-curto-de-simpsons--05112026).

## SPU2 audível e menu original — 05/11/2026

- `446f61c` implementa síntese no SPU2 existente, compartilhada entre
  interpretação/AOT e todos os hosts. O KON `0x001F` em `0x1F9005A0`
  foi superado com as cinco vozes reais; decoder, envelope, NAX e PCM
  evoluem sem zerar ganhos ou ignorar vozes. WAV local de 48 kHz/16 bits:
  `out/game-first/superbust-spu2-game9.wav`, 106.516 quadros estéreo,
  104.306 com sinal. Testes cobrem filtros, pitch, ADSR, loop, registros,
  DMA/IRQ e retomada com PCM idêntico.
- A continuação corrigiu genericamente o bitmap alto de vozes SPU2,
  endereçamento GS e flags de underflow/overflow de VMUL sem ACC.
  Precisão racional de UV de sprites e uploads desalinhados são hipóteses
  adicionais explícitas, desativadas no modo original e com cenário próprio.
- Frames reais mostram TAITO, título e GAME MENU. Start abre o menu;
  Down seleciona CPU BATTLE e Up retorna a 1P PUZZLE. Cross inicia a
  transição; **primeira partida e gameplay ainda não demonstrados**.
  Novo bloqueio: GS triangle fan `PRIM=0xC5`, AA1=1, EE `0x1CE458`,
  12.334.693.440 instruções totais / 12.302.276.764 do jogo; IOP ativo.
- Checkpoint completo seguro: `superbust-before-game-shading.ps2ncp`,
  EE `0x1CE45C`, 12.334.692.440 totais / 12.302.275.764 do jogo,
  somente 1.000 instruções antes da recusa. Pontos anteriores preservados.
- Sem profiling: 99,0864% AOT na janela de um milhão e 85,9795% na
  de 30 milhões foram preservados, com estado completo equivalente e
  sete campos iguais. A integração custou 3,15% a mais no tempo mediano
  da janela curta contra o binário anterior; o ganho histórico de 7,17%
  não pode ser declarado preservado integralmente no novo build.
  Na janela longa pareada, tempo mediano aumentou 4,40%, com dispersão.
  No trecho ativo até o menu: 96,1243% AOT e estado igual à interpretação.
- Validação local: 17 testes focados passaram; `native-mecha-rtc` passou
  após `2816288` corrigir sua fixture antiga para o formato sem a extensão
  de áudio. Controle `ps2n-dev` com BIOS original continua em
  `0x80007D20` / 17.690.079 instruções, recusa do encoder `0x7E`.
  Código final `2816288`: [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/41978015826)
  e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/41978015684)
  aprovados nos seis alvos Windows/Linux/macOS, x64 e ARM64.
  Nenhum ensaio permanece ativo. Medições detalhadas no
  [relatório](docs/FIRST-GAME-STATUS.md#spu2-audível-e-menu-original--05112026).

## Realinhamento técnico — 05/11/2026 (histórico anterior ao SPU2)

- Super Bust-A-Move superou a recusa XYZ do DMA VIF1: SQC2/LQC2 e
  QMTC2 preservam limites numéricos já provados, incluindo cache e RAM.
  A subtração VU conserva bits esparsos na cancelagem; SQRT enumera esse
  domínio pequeno sem ampliar para milhões de codificações intermediárias.
  Nenhum bit desconhecido foi escolhido. Os 87 campos comparáveis
  conservaram seus hashes; 38 campos adicionais passaram da fronteira.
- A execução continuou pelos checkpoints, até uma recusa real de áudio:
  IOP `0x00062140`, contador 1.367.184.044, KON com ganho de voz audível
  ainda sem síntese SPU2. Start, Cross, Up e Down chegaram pelo SIO2,
  mas só o indicador de carregamento foi observado. Menu, resposta
  visual dos controles e gameplay **não foram demonstrados**.
- O analisador prova três jump tables no segmento executável do ELF,
  com guarda de limites e exclusão de instruções descobertas. O AOT
  local cresceu de 13.168 para 17.336 instruções estáticas; uma raiz
  explícita verificada pelo mecanismo existente acrescenta 28. Em uma
  janela de um milhão de instruções no build final: 99,0864% AOT,
  mediana 0,392803 s contra 0,413145 s de interpretação, cinco amostras
  por motor, estado completo idêntico. Isso não mede FPS nem a cobertura
  do jogo inteiro. Em outra janela de 30 milhões: 85,9795% AOT e sete
  campos originais consecutivos idênticos aos do interpretador.
- Profiling opcional no runtime/CLI separa EE, IOP, VU, GS, memória,
  dispositivos, verificações, provas numéricas e sincronização. Compila
  desativado por padrão. A amostragem repetida de interrupções na mesma
  fronteira foi eliminada: 249.987 para 215.161 chamadas de sincronização
  na janela de 100 mil instruções, preservando o estado completo. Sem
  profiling, throughput AOT aumentou 7,17% na janela de um milhão.
- Checkpoints recusam publicação e retomada de estado com IOP recusado;
  o sentinela de IOP dormente do Direct ELF continua permitido.
  Ponto seguro recuperado: `superbust-before-audible-spu2.ps2ncp`,
  `0x1C5A6C`, 10.904.946.798 instruções do jogo, IOP ainda ativo.
  Evidências, condições das medições e limites no
  [relatório](docs/FIRST-GAME-STATUS.md#realinhamento-técnico--05112026).
- Validação final: 12 testes focados locais, equivalência completa em
  15 amostras de desempenho e nos 30 milhões de instruções com imagens.
  CI do código `8f5bb8d`: [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/41968835701)
  e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/41968835789)
  aprovados em Windows/Linux/macOS, x64 e ARM64. Nenhum ensaio ficou ativo.

## Continuação experimental do jogo — 04/11/2026 (histórico)

- **Execução encerrada a pedido do mantenedor.** Últimos ajustes preservam
  identidade entre cópias numéricas e proveniência por via de cache,
  writeback e refill. Quatro testes focados passaram. Checkpoint final
  `superbust-final-safe.ps2ncp`: `0x80002ACC`, 7.759.586.676 instruções
  totais / 7.727.170.000 do jogo, 7.759.588.054 ciclos EE. Restauração
  em 0,859 s; apenas 2.060 instruções até a mesma recusa `0x1CE460` /
  7.727.172.060 do jogo. Não houve avanço na fronteira. O experimento
  escalar do 126 também terminou na mesma recusa; menu e partida pendentes.
  CI `cad88c0`: núcleo e launcher aprovados nos seis alvos; CI dos últimos
  ajustes ainda pendente. Nenhum ensaio deve permanecer em execução.
- **Diagnóstico sem repetir o prefixo:** o 125 retomou de 6,325 bilhões,
  aplicou Cross pelo SIO2 por 35 campos e voltou à mesma recusa DMA em
  7.727.172.060 do jogo. Frames comparáveis mantiveram hashes idênticos.
  Busca binária isolou a primeira escrita incerta em `0x1CD120`,
  7.727.167.542 do jogo; checkpoint salvo uma instrução antes. Há também
  prefixo completo em 7,125 bilhões, com zero origens numéricas incertas.
  Refinamento escalar opcional aceita apenas histórico certificado,
  preserva os resultados anteriores e altera somente sua flag experimental;
  não é política automática de carregamento. Testes de recusa, persistência
  do histórico e equivalência passaram; 126 concluído com hipótese de
  carry escalar explícita e cenário separado, sem avanço. Menu e gameplay pendentes.
  CI dos commits `78973bb` e `052ae60` aprovada nos seis alvos aplicáveis.
- **Checkpoints preservados após a atualização de FPU `e0cfc2f`:** migração
  explícita dos esquemas anteriores, sem reinterpretar layout C++ nem
  repetir o boot. O leitor reserializa todos os campos legados e exige
  o SHA-256 original antes de publicar a restauração. O novo experimento
  escalar fica desativado e sua ativação tardia é proibida. Sete testes
  focados passaram; checkpoint real de 7,2 bilhões carregou em 1,13 s,
  com duas continuações de 1.000 instruções idênticas. A fronteira de
  7,727 bilhões carregou em 1,09 s e reproduziu a mesma recusa DMA em
  2.060 instruções/0,0384 s. Menu e partida continuam pendentes.
- **Retomada na fronteira em menos de um segundo:** checkpoint completo
  em `0x80002ACC`, 7.759.586.676 instruções totais (7.727.170.000 do
  jogo). Carrega em 0,826 s e reproduz a recusa gráfica após só 2.060
  instruções, em 0,0395 s. DIV/CVT/comparações preservam domínios provados;
  cache mantém certificados sem confundir dados residentes com RAM/DMA.
  Sete testes focados e BIOS original passaram. Nova fronteira: DMA VIF1
  em `0x1CE460` / 7.727.172.060 do jogo, com coordenadas XYZ incertas.
  O 122 aplicou Start por 31 campos; 57 frames comparáveis mantiveram
  os mesmos hashes, sem resposta visual comprovada. Animação parcial
  comprovada; menu, navegação e partida pendentes.
  CI `9113b7a`: núcleo e launcher aprovados nos seis alvos.
  [Evidência](docs/FIRST-GAME-STATUS.md#continuações-por-checkpoint--113-a-121).
- **Checkpoint completo funcionando no jogo real:** o ensaio 111 foi
  capturado em 6.032.416.676 e 7.232.416.676 instruções totais, sem
  reiniciar o processo nem alterar estado convidado. O segundo carregou
  em 0,857 s no PC `0x1D2F0C`; duas continuações de 1.000 instruções
  produziram estado completo idêntico. CPU, caches, bits desconhecidos,
  domínios simbólicos e dispositivos pendentes são serializados;
  ROM0/ROM1, console, NVM, disco e cenário são vinculados por identidade.
  `ps2n-boot/run --resume` e salvamento periódico disponíveis. O 111
  superou UV e parou em CVT.W.S `0x186DFC` / 7.323.722.395 do jogo.
  A prova genérica do inteiro único superou CVT no 112: nova recusa
  DIV.S `0x186E60` / 7.323.722.420, só 60,72 s desde o checkpoint.
  Menu e gameplay continuam pendentes. Detalhes e arquivos locais no
  [relatório](docs/FIRST-GAME-STATUS.md#checkpoints-completos--retomada-do-ensaio-111).
- **110 superou as recusas VU do 108/109:** EE `0x1CE45C` /
  7.284.393.384 instruções, IOP ativo `0xAE98` / 914.601.294.
  Nova fronteira: precisão da interpolação UV de triângulos no GS.
  Só aviso, créditos e Acclaim comprovados; scanout final preto.
  **111 concluído acima**, com as novas ferramentas de análise/diagnóstico,
  fontes verificadas e hipótese UV racional explicitamente selecionada.
  A precisão física dessa hipótese não está demonstrada; a referência GS
  continua recusando os casos fracionários. Seis testes focados e BIOS
  original passaram. Menu, controles visuais e partida ainda pendentes.
  [Evidência](docs/FIRST-GAME-STATUS.md#triângulos-uv-no-gs--ensaio-110-e-continuação-111).
- **109 passou da SQRT do 108 e chegou a 7.210.587.806 instruções:**
  VMADDA `0x1CD1DC` recusou um produto com fração incerta; IOP ativo
  `0xC750` / 905.375.574. Ainda apenas aviso, copyright e Acclaim.
  O limite inferior do produto VU agora aproveita extremos sem perda nas
  colunas baixas, inclusive com multiplicador incerto, sem escolher bits.
  Replay da mesma matriz passa preservando incerteza; 13 testes focados
  e regressão BIOS original passaram. **110 concluído abaixo**, com
  núcleo/AOT/harness compilados da mesma fonte fixa e logs reduzidos.
  Menu, navegação e gameplay continuam sem comprovação.
  [Evidência](docs/FIRST-GAME-STATUS.md#extremo-exato-do-produto-vu--ensaio-109-e-continuação-110).
- **108 confirmou SQRT `0x1CD558` / 7.181.062.182**, IOP ativo
  `0xB464`. VF07.w tinha vinte bits de fração incertos. Limites numéricos
  agora são preservados por VF/ACC/Q; replay da mesma entrada passa e
  retorna VF04.x/y conhecidos. A revisão anterior reproduz a recusa.
  Corrigida a validação antiga de VF05, que verificava um temporário
  zerado. Só aviso, copyright e Acclaim observados, sem menu/controle.
  Preparar 109 com fonte fixa e continuar a execução real.
  [Evidência](docs/FIRST-GAME-STATUS.md#limites-numéricos-certificados-na-vu--ensaio-108-e-preparação-do-109).
- **VU preserva incertezas de fração:** intervalos conservadores de MUL,
  ADD e SQRT limitada propagam máscaras conhecidas/desconhecidas por VF,
  ACC e Q; só eliminam incerteza quando provam irrelevância. Sem escolher
  bits arbitrários. Oito testes focados, seis regressões VU/VIF/PATH1 e BIOS
  original passaram. Replay de 158 entradas retornou; a checagem de VF05.x era de um
  temporário, corrigida acima. Não comprova saída trigonométrica ou menu. Evidências no
  [relatório](docs/FIRST-GAME-STATUS.md#aritmética-vu-com-incerteza-preservada).
- **Continuação do menu em execução:** limite conservador do multiplicador
  refinado por conservação de peso das colunas baixas. Quatro milhões de
  pares sintéticos: 3.953.261 aceitos, 46.739 recusados, zero divergências
  contra modelo externo separado. Sete testes focados e regressão BIOS
  original passaram. Rotina matemática isolada agora retorna; isso não
  comprova avanço completo. 105 confirmou VMULbc `0x1CD530` /
  7.176.168.811, IOP ativo; 106 parou voluntariamente para ampliar SQRT
  SRT, validada por um milhão de entradas e sete testes. 107 em execução;
  ainda sem menu ou navegação visível demonstrados. Detalhes no relatório
  [primeiro jogo](docs/FIRST-GAME-STATUS.md#certificação-adicional-do-multiplicador--continuação-do-menu).
- **Super Bust-A-Move passou do aviso para a abertura real:** ensaio 104
  mostrou créditos e logotipo Acclaim por comandos do jogo após ATE/AFAIL
  do GS. Menu ainda não demonstrado. Novo bloqueio EE `0x1CD584` /
  7.176.168.746, C.LT.S; IOP ativo `0xAE94`. Comparações/branches COP1
  implementados em semântica compartilhada; sete testes focados passaram,
  incluindo equivalência AOT/interpretador. SIO2 entrega botões aos buffers
  EE; navegação visível ainda precisa ser comprovada. Rodada seguinte 105,
  com pulsos mais longos medidos em VBLANKs. Evidência no
  [relatório](docs/FIRST-GAME-STATUS.md#abertura-real-e-próxima-fronteira-fpu--ensaio-104).
- **Simpsons avançou para 93.117.072 instruções:** MUL.S da classe potência
  de dois/dígito Booth inicial negativo, leitura NCMD 06 de dados em DVD
  e descarte simbólico da FIFO MECHACON vazia passaram. IOP `0x25E34` /
  15.691.717 bloqueia agora no SCMD 08 ReadRTC; EE `0x80005F00`.
  Setores reais transferidos, SIF1/SIF0 576/448 bytes; ainda zero gráficos.
  Quatro testes focados passaram e modo BIOS original conservou PCs,
  contagens e hash canônicos. Evidência, referências e limites no
  [relatório](docs/FIRST-GAME-STATUS.md#continuação-do-simpsons--fpu-dvd-e-fifo-mechacon).
  CI: `79bb6d6` aprovado nos seis alvos do núcleo; código `6328bb7`
  aprovado nos seis do launcher. Híbrido e interpretador reproduziram
  a parada ReadRTC e a mesma RAM; nenhum ganho de desempenho demonstrado.
- **Reensaio de The Simpsons: Hit & Run (SLUS-20624):** com o núcleo que
  produziu as capturas do Super Bust-A-Move, híbrido e interpretador puro
  chegaram às mesmas 849.776 instruções e RAM, parando em `0x001681B0`:
  `MUL.S` com arredondamento Booth ainda ambíguo. IOP ativo em `0x2F28`,
  4.158.306 instruções. Zero pacotes GIF, pixels ou frames. Ensaio por
  injeção de ELF, ISO própria montada como DVD e hipóteses explícitas;
  nenhum subsistema ou modo BIOS original alterado. Detalhes no relatório
  [primeiro jogo](docs/FIRST-GAME-STATUS.md#reensaio-de-simpsons--04112026).
- **Primeiros quadros não pretos do jogo, ensaio 101:** três capturas no
  VBLANK, campos nativos 640x224 com 64.478 pixels não pretos cada,
  mostram a tela original de Super Bust-A-Move informando Memory Card ausente.
  Pixels vêm de DMA/GIF, paleta, textura, sprites e blending executados pelo
  jogo; nenhum desenho host. EE `0x81FDC` / 1.300.000.000, IOP `0xAE94` /
  166.552.087 ativo, 326 pacotes novos. Parada voluntária após as capturas,
  sem recusa. Não comprova gameplay ou boot original de disco: usa injeção
  de ELF e hipóteses registradas, incluindo SCANMSK=0 no reset. Slots vazios,
  nenhum save alterado. Capturas e traces permanecem locais/ignorados.
  CI de `d7588b0` e `767fc94` aprovada nos seis alvos de núcleo e launcher.
- Super Bust-A-Move transferiu setores reais do CD, incluindo os LBAs
  15637–15691 de IOPRP202, por ReadCd/DMA3. Após a tag SIF1 NEXT,
  alcançou IOP `0x1F458` / 31.828.183 instruções, consulta MECHACON `03:00`;
  1.920 bytes SIF1 e 1.696 SIF0. A consulta passou usando a versão medida do
  perfil; o CDVD depois chegou ao LBA 15824, final de RSPU2DRV.IRX.
  CAV nominal é hipótese explícita de diagnóstico; não houve loader host.
- DMA/IRQ SIF nos dois sentidos, pacotes parciais, PLZCW e salvamento simbólico
  de BPC/PCCR exercitados por código convidado. Nenhuma resposta RPC fabricada.
- Modo original repetido após ROM1/CDVD: mesmos PCs `0x80007D20`/`0x80005F00`,
  contagens e RAM SHA-256 `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
- **Primeiros pacotes gráficos reais no ensaio 42:** 16 pacotes GIF PATH3,
  1.048.576 escritas de pixels pela transferência IMAGE comandada pelo jogo.
  É uma limpeza da VRAM, sem frame exibido/menu/gameplay. Novo bloqueio EE
  `0x1CDF74`, leitura GS CSR, após 338.172.827 instruções. O ensaio usa injeção
  de ELF e hipóteses explícitas, incluindo SPU2 inativo no reset; não comprova
  boot original. O modo original mantém PCs/contagens/hash anteriores.
- Continuação até ensaio 84: CSR, IMR, aritmética inteira com overflow,
  FPU, montagem e multiplicação das matrizes VU superados. EE em espera `0x81FD0` /
  360.000.000 instruções, IOP parado `0x47850` / 47.580.787. A fronteira Booth
  de 15 zeros baixos foi resolvida por perda do carry da coluna 15; 16 ou
  mais zeros não atingem colunas descartadas. Demais casos ambíguos recusam.
  VSQRT/VDIV/Q e VMOVE passaram. A hipótese explícita SPU2 7CA permitiu
  investigar as janelas não atribuídas, também isoladas por hipótese explícita. PIO enviou 16 halfwords reais para RAM SPU2; parâmetros, KON/KOFF simbólicos, ENDX, ESA/EEA e configuração master passaram. ENVX zero foi provado por release linear limitado; efeitos de ganho zero preservam RAM desconhecida. ESA inicial foi isolado por hipótese; o ensaio 84 completou 31.584 palavras reais por DMA normal e alcançou uma transferência SIO2. FIELD e os bits de timing RECV2 usados pelo PADMAN avançaram sob hipóteses estreitas e explícitas. O barramento usa throughput explícito não medido; bytes opacos permanecem desconhecidos. Sem novos pixels além da limpeza;
  PMODE ainda não escrito. O modo BIOS original mantém seu hash/contagens.
  Commits `d4d98a1` e `8410973` aprovados na CI dos seis alvos (núcleo e launcher);
  `94230de` e a correção de fixture FPU `dd6ed41` aprovados nos seis do núcleo.
  ADD.S/SUB.S (`1ddd0d1`) também aprovado nos seis do launcher.
  BSD e modo original preservados.
- Ensaio 89: controles digitais completaram polls/timeouts e IOP segue
  executando (`0xAE94` / 48.228.327). D-cache funcional opcional superou
  DXLTG com tags/dados reais e escrita de volta; sem latência adicional e
  com hipótese separada para hit via alias uncached. EE `0x108AD8` /
  353.409.922: próximo bloqueio é DMA VIF1 source-chain/TTE. Seis testes
  focados passaram, incluindo coerência observável pelo DMA/GIF. Ainda
  16 pacotes de limpeza. No ensaio 90 o DMA VIF passou e o jogo escreveu PMODE/
  circuito 2; EE `0x1024C0` / 358.037.955 para em escrita de profundidade
  PSMZ16S. Profundidade e scanout INT+FFMD implementados: ensaio 91 no mesmo PC, agora SCANMSK não escrito; scanout 640x224 preto da limpeza real, sem menu/gameplay. Bits SCANMSK/FBA desconhecidos passam a propagar incerteza, sem valor de reset inventado. Seis testes GS/VIF/frames AOT focados passaram; CI d033f16 e 0570023 aprovada nos seis alvos de núcleo/launcher. Sete testes
  focados DMA/cache/GS/AOT passaram. CI dos controles `938ed35` e
  `833004c` aprovada nos seis alvos de núcleo e launcher.
- Ensaios 92–97: uploads PSMT8/PSMT4, DMA GIF source-chain pelo motor
  compartilhado do VIF e PSUBW passaram. O ensaio 97 alcançou
  1.260.000.000 instruções EE (`0x81FDC`), 246 pacotes GIF e
  1.819.120 escritas comprovadas; scanout 640x224 ainda inteiramente preto.
  IOP `0x47850` / 159.520.606 recusou uma sondagem de Memory Card cuja
  presença não estava configurada. O ensaio seguinte usa explicitamente
  slots vazios na API diagnóstica existente; nenhum save é montado.
  Captura no VBLANK está ativa, mas ainda sem imagem não preta.
  CI de `f6344e4`, `950cfd8`, `df1203c`, `b6ea81f` e `259346e`
  aprovada nos seis alvos de núcleo e launcher. Regressão BIOS original
  mantém PCs, contagens e hash canônicos após essas mudanças.
- Ensaios 98–100: slots de Memory Card explicitamente vazios e
  VIF FLUSH/DIRECTHL passaram; sprites UV, nearest, CLUT CSM1 CT32,
  PSMT4/8 e alpha blending implementados com aritmética inteira.
  Ensaio 100: EE `0x1CE468` / 1.268.129.288, IOP ativo, 265 pacotes GIF,
  2.077.200 escritas comprovadas por transferências. SCANMSK não escrito
  propaga incerteza aos desenhos; profundidade e scanout recusam bits
  indeterminados. Ainda nenhum frame não preto. O ensaio 101 configura
  SCANMSK=0 como hipótese isolada de reset, desligada no modo original.
  Oito testes gráficos focados e regressão BIOS original aprovados.
- Sequência, hipóteses, testes e referências:
  [primeiro jogo](docs/FIRST-GAME-STATUS.md#continuação-experimental--subsistemas-exercitados-pelo-jogo).

## Última evidência: sincronização GS/IOP — 04/11/2026

- Corrigida a observação de mudanças do timing GS antes da próxima decisão de interrupção
  do IOP. Regressão própria falha antes da correção e passa depois; uma parada anterior
  continua preservada. Não altera valores de reset do GS nem respostas do encoder.
- **Super Bust-A-Move repetido no interpretador e no híbrido:** 4.486.087 instruções,
  PC `0x80005F00`, IOP `0x000040B0`, RAM idêntica; **zero pacotes GIF do jogo e zero pixels**.
  O trace confirma `CSR.RESET` → `SMODE1` → encoder desconhecido, sem escrita de `SYNCV`.
  `SetGsCrt` Sony repetido também para em `0x80007D20`; não há inicialização comprovada.
- Cinco testes focados de IOP/SIF aprovados. A correção não demonstrou avanço do jogo
  nem ganho de desempenho. Evidências e dependência de dados reais de hardware:
  [relatório do primeiro jogo](docs/FIRST-GAME-STATUS.md#sincronização-gsiop-e-reexecução-do-checkpoint).

## Evidência anterior: primeiro ELF comercial

- **The Simpsons: Hit & Run (USA), SLUS-20624:** escolhido entre os novos discos pelo ELF monolítico.
  Até a primeira syscall: 214.945 instruções, 214.944 AOT/1 interpretada, PC `0x80000180`, zero pixels GS.
  Tetris Plus local é PS1 e não é candidato ao runtime EE. **Nenhum menu, controle ou gameplay.**
- **Correções genéricas executadas:** contexto SA/HI-LO/FCR31, packing e PSUBB, cargas/escritas
  desalinhadas, MOV/conversões FPU, divisão SRT e multiplicação esparsa. Casos restantes falham;
  a cobertura exclui a instrução que não aposentou. Nenhum serviço HLE substitui a BIOS.
- **Diagnóstico separado, sem boot concluído:** ELF injetado no estado parcial do firmware executou
  handlers Sony reais até 627.894 instruções; interpretador/híbrido conferem RAM. Parou em MUL.S
  geral (`0x002DEAF4`). Isso não comprova inicialização normal. Boot ROM0/NVM original repetido mantém
  o bloqueio externo no ID não medido do codificador (`0x80007D20`), sem respostas assumidas.
- **Regressões:** contexto/FPU, falhas e equivalência AOT/interpretador com fixtures próprios;
  nenhuma alteração no launcher. Identidades, logs, testes/CI e limites: [primeiro jogo](docs/FIRST-GAME-STATUS.md).

## Evidência anterior: incremento M3.0

- **Três ELF próprios pelo mesmo runtime:** chamadas/memória/interrupt de Count, comunicação SIF1 e frame
  VIF1 DIRECT→GIF PATH2→GS. Interpretador e AOT conferem memória, registradores, eventos e framebuffer.
- **SIF1 real dentro do modelo:** 256 bytes transferidos para a RAM IOP, checksum calculado no R3000A,
  backpressure e flags/IRQ conferidos; não é RPC/HLE nem prova de timing físico.
- **AOT genérico:** descoberta de funções por JALR constante, `--hybrid` para ELF e snapshot SHA-256 de
  região dinâmica para regeneração offline; overlay compilado confere bytes e invalida código modificado.
  No teste de CPU, interpretação caiu de 30.017 para 17 instruções e trocas de motor de 20.009 para 9.
- **Frame Direct ELF:** 64×32, digest RGBA8 `40231620…b6f436a3`, igual nos dois motores. Modo original
  de boot permanece separado; nenhum firmware ou jogo real foi reexecutado nesta etapa.
- **Escopo M3.0 ainda parcial:** IRX/linking, execução VU0/VU1 e INTC/serviços completos não foram
  implementados. O frame atual não atravessa VU. Evidências, medições, comandos e CI: [M3.0](docs/M3.0-STATUS.md).
- **Validação:** 36 verificações locais e CI do núcleo nos seis alvos aprovadas para `e80c6f2`;
  a workflow separada do launcher tem falhas de instalação Qt no Windows e de teste QML no macOS ARM64,
  registradas no relatório, sem alteração da GUI neste incremento.

## Evidência anterior: M2.9

- **Primeiro frame GS sintético:** 256×160, gerado por instruções R5900/GIF/GS; digest RGBA8 `43564b22…2e3f54`, igual no interpretador e no híbrido. API de framebuffer/eventos compartilhada e exportação BMP disponíveis, sem dependência gráfica obrigatória.
- **BIOS real repetida nos dois perfis SCPH-70004:** mantém 17.690.079 instruções EE, parada em `0x80007D20`; diagnóstico com IDs assumidos mantém 17.690.770 e parada em `0x8000E628`. Zero escritas de pixels em ambas as rotas; não houve avanço do Browser. As máscaras de IMR após reset foram estabelecidas pelo manual; fase vertical e INTC seguem pendentes.
- Relatório, comandos, contratos, testes e limites: [M2.9](docs/M2.9-STATUS.md). A evidência anterior de identificação de firmware permanece abaixo.

## Evidência anterior: M2.8

- **Firmware com NVM fornecidos localmente:** arquivo ROM0 SCPH-70004 (ROMVER `0200EC20040614`) e NVM Dragon de 1.024 bytes, compatíveis por identidade verificada; ambos **fora do Git**. Foram exercitados dois perfis possíveis de placa GH-032 que a NVM não permite distinguir.
- **Boot interpretado e híbrido:** 17.690.079 instruções EE e 2.210.789 IOP, traces equivalentes (desconsiderando marcadores do motor), com mensagens `Initialize Start.` e `Initialize GS ...`. A parada do EE é a leitura do ID `0x7E` do codificador de vídeo (I²C endereço `0x21`, controlador em `0x1A000000` na ROMVER 0200); o valor real não foi medido.
- **Diagnóstico separado, não boot comprovado:** assumir explicitamente o ID do codificador via opção de diagnóstico permitiu prosseguir até a leitura de `I_STAT` do EE após o envio de pacote GIF PATH3. **Não** utilizar esses resultados como confirmação de hardware.
- **IOP após reset do GS:** VBLANK fica não determinado até ser esclarecido o estado do `SYNCV`/PCRT; não inventar eventos nem interrupções.
- **Outras ROM0 reais** (SCPH-70012, SCPH-77008 e firmware japonês de 2006) continuam sem NVM correspondente validada; os resultados anteriores devem ser consultados em `docs/M2.7-STATUS.md`.
- **Testes:** a documentação M2.8 relata 25 testes CTest locais aprovados no Windows x86-64. CI do commit M2.8 deve ser conferida em [GitHub Actions](https://github.com/fabegalo/ps2-native/actions); a suíte remota só é aprovada quando todos os seis jobs terminarem com sucesso.

Detalhamento de endereços, perfis, SHA-256, suposições, componentes e bloqueios: [M2.8](docs/M2.8-STATUS.md).

## Launcher desktop (fundação da interface)

- **Executável `ps2-native`** (Qt 6 Quick, LGPL-3.0 dinâmico; `PS2N_LAUNCHER=AUTO|ON|OFF`): Início, Biblioteca, BIOS, Configurações e Sobre. Gerencia o conteúdo do usuário e **não substitui** Browser, System Configuration nem boot de disco; ações sem runtime aparecem bloqueadas com o motivo.
- **Núcleo compartilhado `ps2n_frontend`**, sem GUI: BIOS por `load_bios_rom0`, NVM por `load_nvm_image` contra o perfil da mesma ROMVER, discos ISO 9660 (2048/2352 bytes, Mode 1/2), CUE multifaixa, leitura por faixa acima de 4 GiB; biblioteca versionada com escrita atômica, sem copiar ISOs ou firmware; contratos futuros do runtime (`RuntimeSession`, renderizadores, controle, áudio) declarados e indisponíveis.
- **Testes:** `launcher-core` (headless, também na CI do núcleo) e `launcher-ui-import`/`launcher-ui-restore` (janela real offscreen: importação, páginas, diagnóstico, persistência e restauração) aprovados no Windows x86-64 com Qt 6.8.3, sem avisos de QML. Novo workflow `launcher.yml` para os seis alvos (Qt 6.8.3 oficial no Windows, Qt 6.4 do Ubuntu, Qt do Homebrew); conferir o resultado por commit no GitHub Actions.
- **Validação gráfica local** (plataforma `windows`): todas as páginas capturadas em 1360×860, 1600×1000 e 1000×660; pasta local com 60 ROM0 reais validadas, uma ISO de DVD de PS2 e um CD de PlayStation em CUE/BIN identificados; diagnóstico numa ROM0 real até o limite de instruções. Conteúdo local fora do Git.
- Limites: jogos, Browser, controle, áudio e renderização dependem do runtime; imagens comprimidas não são lidas; diálogos nativos e arrastar e soltar não foram exercitados automaticamente. Contratos e detalhes: [LAUNCHER.md](docs/LAUNCHER.md).

## I/O do host: caminhos longos e Unicode

- Leitura de BIOS/NVM/ELF, escrita de AOT/traces/fixtures, diretórios e verificações centralizados em `src/io.cpp`. Argumentos UTF-16 preservados desde `wmain`; sem mudanças no Registro ou cópias para contornar limites de caminho.
- Regressão permanente `io-long-paths`: 260, 500 e 1.000 unidades nativas, caminhos relativos/absolutos/estendidos, Unicode, parâmetros FILEPATH do CMake, escrita idempotente e recusas explícitas.
- Windows x86-64/MSVC 19.44: os 25 testes existentes e o novo teste de I/O passaram. UNC real aprovado num compartilhamento local já existente, com caminhos de 277, 511 e 1.019 unidades UTF-16. A CI sem compartilhamento informa explicitamente que UNC real não foi validado.
- Compilação AOT profunda corrigida: o CMake referencia o diretório original por uma junction local ao build, verifica identidade do arquivo e conserva as dependências do Ninja, sem copiar o fonte ou alterar Registro. O runner com fonte Unicode de 983 unidades compilou; a regressão `aot-long-path-build` aprovou compilação/execução, no-op e rebuilds por mudanças no fonte/header em 260/500/1.000 unidades no Windows x86-64. A captura exata do prefixo localizado de `/showIncludes` também foi corrigida. O adaptador requer alvo local e filesystem com junctions; detalhes e limites no relatório de caminhos.

Auditoria, reprodução e limites: [caminhos do host](docs/WINDOWS-PATHS.md). A validação dos seis alvos é acompanhada por commit no [GitHub Actions](https://github.com/fabegalo/ps2-native/actions).

## Pendências principais

1. Obter evidência real dos IDs do codificador de vídeo e do estado GS/IMR/CSR após reset, sem respostas inventadas.
2. Reproduzir VBLANK/INTC EE e manter o sincronismo EE↔IOP após reconfiguração do GS.
3. Validar e implementar a comunicação SIF e DMA com transferência de bytes real e tags/FIFO corretos.
4. Avançar o boot do firmware original até o Browser, sem falsificar dados NVM, periféricos ou compatibilidade.
5. Ampliar o caminho AOT genérico e, depois, os modelos GS/VU/VIF/SPU2, medindo correção e desempenho.

Ver [próxima tarefa](docs/NEXT-TASK.md). Esta lista é dinâmica; não deve ser duplicada no README.

## Histórico de marcos (detalhes nos relatórios)

| Marco | Principal evidência |
|---|---|
| M0–M1.3 | Fundação ELF32, código nativo sintético, indiretos, relocations e semântica R5900 parcial |
| M2.0–M2.2 | Loader ROM0, execução real até a configuração GS/RDRAM, temporização inicial e SIO |
| M2.3–M2.5 | Protocolo MCH/RDRAM, perfis de placa e progresso da BIOS até a memória do IOP |
| M2.6 | Carregamento da NVM do MECHACON, boot IOP, timers e exceções |
| M2.7 | VBLANK, registradores SIF e sincronização dos acessos EE/IOP |
| M2.8 | Primeira ROM0/NVM real pareada; codificador de vídeo e caminho GIF PATH3 |

Revisar a evidência original e os limites de cada etapa nos relatórios [docs](docs/README.md). Não interpretar o número de instruções retiradas como certificação de boot, vídeo ou jogos.

## Launcher — ícone Windows e idiomas

Ícone Windows embutido no executável; seleção Automático/English/Português (Brasil), persistente e com troca imediata. Catálogo com 356 textos da interface (incluindo capacidades do runtime e avisos de importação) e página Início ajustada (Hero compacto/responsivo, cartão Sistema com estado vazio explícito, contadores da Biblioteca sem corte de texto e descrições de capacidades alinhadas ao estado experimental atual); saída técnica original preservada. Quatro testes focados do launcher e capturas em 1360×860 e 1000×660 aprovados no Windows x64. Detalhes e limites em [LAUNCHER](docs/LAUNCHER.md).

## CI do launcher corrigida

`bfa70f2`/`67aed98`: leitura textual do checksum oficial Qt, extração em prefixos separados x64/ARM64 e atalho QML com todas as combinações padrão. **Launcher e núcleo aprovados nos seis alvos para `67aed98`**. [Execução do launcher](https://github.com/fabegalo/ps2-native/actions/runs/41758760249); causas e evidência em [LAUNCHER](docs/LAUNCHER.md). Sem alteração nas capacidades de Browser/jogos.

- **Super Bust-A-Move (SLUS-20115):** ELF em diagnóstico com kernel Sony parcial até 4.486.087 passos (4.483.572 AOT), PC `0x80005F00`; SIF0/IRQ IP3 implementados e regressão com transferência real. Zero frames/menu/gameplay; IOP sem timing GS conhecido impede inicialização. Evidência em [FIRST-GAME-STATUS](docs/FIRST-GAME-STATUS.md).

- Continuação GS, ensaio 92: EE `0x1CECA0` / 387.565.264, IOP `0xD628` /
  52.497.745 ativo. 24 pacotes GIF novos, 1.048.832 escritas comprovadas;
  scanout ainda preto. PSMT8 208x128 é o próximo upload; transferência e
  swizzle de 8 bits implementados com seis testes focados aprovados.
  Modo original repetido após GS conserva PCs/contagens/hash canônicos.

## Developer toolchain — 04/11/2026

- `6a65581`: `ps2n-dev` executa perfis reproduzíveis de ELF/disco/ROM0,
  análise e AOT em cache verificado, build incremental único de núcleo/AOT/
  runner, replay com identidades conferidas e manifestos de configuração,
  commit, hashes, flags e tempos. Artefatos e conteúdo original ficam locais.
- `ps2n-debug` usa observadores compartilhados AOT/interpretador, histórico
  circular limitado de instruções/barramento/dispositivos, estado inspecionado,
  causa explícita, scanout real e comparação de eventos; streams truncados ou
  coberturas incompatíveis são recusados. `ps2n-analyze` exporta CFG, cobertura
  estática, símbolos, relocations, destinos e limites conhecidos; Ghidra é
  opcional e seus mapas permanecem indicações validadas. IRX: só metadados
  de contêiner/seções, sem linker ou decodificação R5900.
- `392acc8`: orçamento/anel/stream não invalidam análise/AOT; conteúdo C++
  idêntico preserva caminho/timestamp mesmo após repetir a geração.
  Três testes da toolchain e 17 regressões focadas aprovados localmente.
  Validação isolada em árvore limpa: primeira rodada 64,49 s, replay 1,18 s;
  build de runtime 4,152→0,181 s, mesmo executável e eventos, caches reutilizados.
  Log de um milhão de linhas resumido em 0,511 s; sem ganho contra busca
  simples por `rg`. Firmware real testado só até o orçamento de 100 mil
  instruções; fixture gráfica própria capturada com hash esperado.
- Sem save-state completo ou checkpoint de RAM; sem nova interface visual.
  O trabalho paralelo do jogo e as hipóteses originais permanecem separados.
  Comandos, medições, referências e limites em [DEV-TOOLS](docs/DEV-TOOLS.md).

## VU1 AOT nativo — 05/11/2026

- `d6f0bd3` integra microprogramas VU1 ao `ps2n-aot`, particionamento,
  manifesto e build C++20 existentes. Captura opcional após MPG conserva
  valores e máscaras, raízes reais e modo numérico; seleção verifica a
  imagem integral por SHA-256. Escritas, restauração e PCs sem prova
  invalidam a seleção ou seguem explicitamente pelo interpretador.
- Semântica de pares upper/lower compartilhada com o interpretador,
  especializada por constantes no código nativo. Sem compilação durante
  gameplay, fast-math, dependência AVX, hacks por jogo ou alterações no launcher.
- Quatorze testes focados passaram; a versão em cinco partições mais
  dispatcher também compilou e passou a equivalência. Incluem pipelines,
  ACC, Q, flags, branches/delays/wrap, memória, MPG, XGKICK/GIF, sprite RGBA,
  IRQ e checkpoint completo. P é preservado, mas EFU/P continua sem execução
  modelada; instruções não suportadas mantêm as recusas existentes.
- Fixture própria: **5,828 → 6,841 milhões de pares LIW/s (+17,40%)**,
  1,373 → 1,170 ms por microprograma. No cenário integrado curto,
  **1,941 → 2,238 ms (+15,28% de tempo; −13,25% de throughput)**: não houve
  ganho integrado. Geração CLI 0,097 s; compilação da unidade nativa 3,065 s.
- O checkpoint comercial disponível não executa VU1: conserva apenas os
  pares sentinela de reset. A janela EE de 30 milhões manteve estado completo,
  seis campos RGBA e PCM idênticos. **Microprograma comercial VU1 e seu ganho
  integrado continuam pendentes**; nenhuma alegação de FPS comercial.
- Método, cobertura, medições comerciais finais e CI em
  [VU1-AOT-STATUS](docs/VU1-AOT-STATUS.md).
