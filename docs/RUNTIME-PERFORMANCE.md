# Runtime: fronteiras de evento e tradução de memória — 05/11/2026

## Super Bust: IMAGE, traduções observadas e lote integrado — 07/11/2026

Incrementos `45d740e` e `740d56f`, posteriores à seção histórica abaixo.
O checkpoint do menu, entradas imutáveis, opções Release e GPU são os mesmos;
nenhuma hipótese de hardware ou clock foi relaxada. Os fontes congelados são
v5 (IMAGE), v6 (tabelas GS e separação fria da busca IOP) e v7 (índice IOP
aberto para colisões/halfwords e divisão de timer BUSCLK especializada).
As tabelas GS são geradas em compilação pela álgebra já existente, mantendo
validação e caminhos gerais; não incorporam implementação externa.
O índice mantém busca geral para listas que excedam seus 4.095 elementos;
crescimento/reset/restore continuam invalidando posições, nunca valores.

Uma captura bounded de seis milhões EE (`menu-coverage/full-events.ndjson`)
foi validada com o comparador existente. Palavras observadas foram conferidas
contra o ELF antes de alimentar raízes explícitas ao `ps2n-aot`. A tradução
local `menu-aot-79439d486ee153fc/module.cpp` contém 84.333 instruções e 3.590
entradas indiretas verificadas. Todas as 84.333 entradas foram verificadas
contra o RAM restaurado pelo mesmo critério eager da janela. Game + loader
somente: 99.501.816 AOT / 498.184 interpretadas no trecho de 100 milhões,
contra 94.975.340 / 5.024.660 antes. Não foi ligado AOT do vetor KSEG ou VU1.
Os artefatos derivados permanecem somente no `out/` ignorado.

Comparações primárias alternadas no menu, sem hashes/timers intrusivos,
com chunks de 100 mil, prioridade/afinidade normais. Esta tarefa não compilou
nem testou durante cada série; outras tarefas do usuário não foram encerradas.
Todos os tempos são segundos, preservados nos logs locais:

| Lote | Controle | Candidata | Medianas |
| --- | --- | --- | --- |
| IMAGE, 30 M EE, cinco pares | 1,09232; 1,13115; 1,13919; 1,11932; 1,09830 | 1,03260; 1,00416; 1,00218; 1,04563; 1,03819 | 1,11932 → 1,03260 |
| AOT ampliada + GS/IOP v6, 100 M, três pares | 4,13030; 4,41613; 4,19354 | 3,48093; 3,49047; 3,50318 | 4,19354 → 3,49047 |
| IOP/timers v7, 100 M, três pares | 2,87019; 2,98676; 3,62720 | 2,79917; 2,62287; 2,85788 | 2,98676 → 2,79917 |

Última série tem variação grande: não extrapolar os 6,28% da mediana, nem
somar percentuais entre séries com cargas externas diferentes. Os 100 M
representam aproximadamente 0,339 s convidado e 20 campos originais: o alvo
de velocidade original continua ausente. Logs `menu-transfer-timing-*`,
`menu-batch-100m-timing-*`, `menu-collision-100m-*`.

Validação comercial separada, 30 M EE, v5/v6/v7 contra o controle histórico:
mesmos ciclos 21.194.531.422, PC 0x81FC0 e 58.903.258 bytes de payload,
SHA-256 `64cb95d8b7487dd0a909152868ad6b78844a33b6e3e426fa1c0ba599ad1dfc74`.
Seis campos RGBA, 9.766 amostras PCM e 12 polls têm os hashes da seção abaixo.
RTX 4070: 335 primitivas / 63 lotes, zero fallback de desenho em todos os motivos.
Esquema `11a100b3f0fda71733f6772b12b4d4748ef162dc10fd182c2b64cf6fe2dd1e0b`;
runtime v6 `3fbb70d1f509b65428b93a61857cddd5392c81ecf98607c3166a8ec3d6f67a85`,
v7 `90510c2ce867f4acba6d402fe5d1b6aca0dc7c2067a3c2b7f6f9dc9da54ba4ad`.
Headers de AOT conferidos byte a byte antes de reutilizar objetos no runner;
a janela é compilada normalmente pelo CMake com os fontes v7 e módulos atuais.

Verificações do lote: IOP, VRAM, GIF e GS (4/4); após revisão do índice,
IOP e EE timers (2/2). Os testes cobrem crescimento/reset, colisão ausente,
halfwords vizinhos com valores distintos, padding e transferências inteiras.
IMAGE também passou checkpoint completo, GS físico Vulkan/DirectX e CT16.
Três testes GS de expectativas antigas falham igualmente no controle v4:
`native-m29-drawing`, `native-gs-filter`, `native-gs-triangles`; nenhuma asserção
foi relaxada. Esses snapshots não contêm correções concorrentes posteriores.
CI das seis plataformas não foi executada para estes fontes locais.

Perfil separado de v6: 6.469 amostras no worker, busca fria IOP 10,56%, espera
9,72%, timer EE 5,04%, canais gráficos 4,00%, endereço VRAM 3,54%. Isso motivou
v7. O perfil antigo de IMAGE com somente 161 amostras é inconclusivo e foi
preservado. A execução amostrada terminou seu orçamento de dois bilhões EE
com sucesso; seu tempo não é benchmark. Logs `menu-batch-sampling.log` e
`menu-batch-sampled.log`. Próximo perfil deve usar a versão integrada atual.

A sessão humana anterior demonstrou menu/entrada e PCM enviado, com 5–6 FPS
e underruns. O usuário informou que fecha as janelas manualmente. Nenhuma
janela foi fechada pela tarefa. A compilação da janela v7 ainda está em curso;
os resultados acima são do runner com o mesmo núcleo, módulos e chunks,
não uma certificação de partida com som contínuo ou de boot original.

## Menu real: índice de latches e GPU física — 07/11/2026

Incremento `008d2d1`. A janela foi acompanhada até a intro animada e o menu,
com Start/Cross enviados pelo GameView/SIO2. Checkpoints novos, sem substituir
os anteriores: `out/reconstruction/superbust/live-scene-original-speed.ps2ncp`
e `interactive-menu-restored.ps2ncp`, cada um com manifesto de identidade.
O cenário continua experimental: firmware parcial seguido de injeção do ELF,
separado do boot original. Nenhum módulo VU1 AOT foi ligado.

Controle: `source-frozen-v3` / runtime `d0eb66a1b794f7d7ef8a43aade4c9ebce7fd5a0dd52d6a4445e6970fb285a52c`.
Candidata: `source-frozen-v4` / `66e57922f479660f7e29a109396ecae1d57c851667c61c91fa69d424d02d10ce`.
Esquema comum `11a100b3f0fda71733f6772b12b4d4748ef162dc10fd182c2b64cf6fe2dd1e0b`.
MSVC 19.44, Ninja Release `/O2 /Ob2 /DNDEBUG /Zi`, profiling OFF; Qt 6.8.3.
Os runners em `live-reference/` e `live-latch/` e a janela `latch-build/`
usam AOT do ELF USA (7.704 instruções estáticas) e do loop carregado (8),
com hashes/bytes conferidos. O vetor KSEG não foi mesclado nesta comparação.
A nova fonte difere do controle apenas pelo índice IOP e invalidação no bind
de checkpoint, além dos testes. Alterações concorrentes da main não recebem
automaticamente estes resultados. Não houve código externo incorporado.

O índice de 4.096 slots guarda posições, nunca valores; é reconstruído no
crescimento e invalidado no reset/restore. Colisões mantêm a busca linear,
também preservada para listas que excedam a capacidade de representação.
A candidata passou `native-m27-iop`, `native-complete-checkpoint` e
`native-checkpoint-cli`; teste adicional cobre colisão de endereço.
O teste de checkpoint inclui destinos com presença/ausência previamente
memorizadas e listas do mesmo tamanho, mas registradores diferentes.

Validação separada do menu, CPU de referência versus índice + Vulkan RTX 4070:
30.000.000 instruções, ciclos finais 21.194.531.422, payload completo
`64cb95d8b7487dd0a909152868ad6b78844a33b6e3e426fa1c0ba599ad1dfc74` igual.
Seis campos RGBA `06b27420574d69529e735fc94c342a58641fbe305862f754f1ee7806df7199b0`,
9.766 amostras PCM `2c6c1f6a3e2afd9f95add849212653150b15f89b81c2ae7d602550096fb410cb`,
12 polls de entrada `f69e62ddcc1762e8738af458ec7ba86350ce78ae0cd1695847d0a07bf5cc8a89`.
GPU executou 335 primitivas em 63 lotes. Logs `menu-*-validation.log`.

Três pares alternados, mesmo menu/Vulkan, blocos de 100 mil como na janela,
sem diagnósticos/hashes na janela primária e sem compilação/teste concorrente
desta tarefa; janela anterior pausada. Outras tarefas do usuário não foram
encerradas, e não houve fixação de afinidade/prioridade:

| Par | Controle (s) | Índice (s) |
| --- | ---: | ---: |
| 1 | 1,21400 | 1,06588 |
| 2 | 1,19295 | 1,07091 |
| 3 | 1,20008 | 1,05287 |

Mediana **1,20008 → 1,06588 s**, −11,18% de tempo / 1,126× throughput.
São aproximadamente 0,1 s convidado: ainda muito abaixo da velocidade original.
Logs `menu-index-timing-*`. A GPU também preservou saídas na intro animada.

Resultados não selecionados, todos preservados: um cache pequeno só de
índices deu 0,882560/0,904682/0,920882 → 0,862993/0,846335/0,848534 s
na intro, com blocos de 20 milhões. Ao adicionar cache de ausências em 64
slots e usar os blocos de 100 mil, os pares foram
0,876156/0,843623/0,853324 → 0,872220/0,879790/0,875311 s: sem ganho consistente.
Essa variante foi substituída. A nova investigação de latches foi motivada
por amostragem atual, não pela repetição sem evidência da tentativa histórica
rejeitada. Medição adicional da intro com Vulkan deu 0,695707 s; não confundir
com os pares do menu. Logs `live-timing-*` e `live-negative-*`.

A janela atualizada (`out/reconstruction/latch-build/ps2-native.exe`, manifesto
`interactive-menu-vulkan.json`) foi inspecionada: imagem, polls, XInput e PCM
não nulo enviados ao WinMM; cerca de **5–6 FPS / 8–10% EE**, com faltas de
buffer. A anterior permaneceu aberta, pausada. Não foi demonstrada partida
controlável na velocidade original nem áudio contínuo/audível certificado.
Sampling da nova janela: 1.839 amostras no worker, espera 10,93%, endereço
VRAM 7,40%, latches 6,74%, transferência GS 5,60%, timer EE 4,84%;
`menu-index-live-sampling.log`. Próxima candidata reduz trabalho repetido
na transferência IMAGE; não há resultado atribuído a ela ainda.

## IRQ do IOP no cenário comercial reconstruído — 07/11/2026

Commits deste incremento: `3d924d8` (restauração), `5399860` (preparação e
aliases offline) e `9aaf0d8` (IRQ). O baseline anterior do tabuleiro foi perdido
quando `out/` foi apagado. Esta medição pertence a outro trecho, reconstruído
com firmware parcial e injeção explícita do ELF de Super Bust-A-Move USA.
Não é o reset original, uma sessão gráfica nem uma partida certificada.

### Identidade e execução

- Disco SHA-256 `232c139dfdd8b6fd9b201ac4c11f9e03b204732ee85adc42fdc06a0350929744`,
  BOOT2 `SLUS_201.15;1`, ELF `eeb913a6eac79a58d8424dc18190310f1ae5d14d762dec5a339f44ad9870d965`.
- ROM0 `d6653f4e…`, NVM `5c3c7a94…`, SCPH-70004:GH-032-11; ROM1
  `b86fbcca…` conserva a ressalva de procedência do chip DVD no manifesto local.
- Cenário `b80a5dba916c81b5352a3001959d26d5fb746f326e7bfb68925c579747797ef3`:
  RTC fixo, hipóteses de encoder/SYNCV/SBUS/IOP/SPU2 e precisão GS 61 explícitas
  em `out/reconstruction/superbust/probe.cpp`. Os originais permanecem somente leitura.
- Fonte de controle `out/reconstruction/source-frozen-v1`; candidata
  `source-frozen-v3`. Comparação dos fontes do núcleo encontrou diferenças
  somente em `src/iop.cpp` e `include/ps2native/iop.hpp`. Manifestos de hashes
  e fonte/diff de origem permanecem locais. As alterações concorrentes da HEAD
  não recebem automaticamente este resultado.
- Ryzen 7 5700X3D, MSVC 19.44.35229, Ninja Release `/O2 /Ob2 /DNDEBUG /Zi`,
  link `/DEBUG /INCREMENTAL:NO`, profiling intrusivo OFF, afinidade 16384 e
  prioridade AboveNormal. GS por software; Vulkan/DirectX compilados, mas
  nenhum GPU backend selecionado neste runner. Nenhum módulo VU1 AOT ligado.
- Os dois executáveis `reconstruction-kernel.exe` em `paired-build/` e
  `candidate-runner/` ligam os mesmos módulos EE de jogo, vetor KSEG e loop
  carregado, com hash/bytes conferidos e guardas por instrução preservados.
  Esses conteúdos derivados ficam exclusivamente em `out/`.

### Gargalo, mudança e equivalência

Sampling separado com símbolos correspondentes, usando o sampler existente de
gow-native-pc já compilado pela investigação local, encontrou `Iop::modeled_irqs`
em 10,82% de 416 amostras da thread de execução. O coletor recebeu somente o
PID criado pela tarefa. Não houve implementação de outro profiler nem código
externo ligado ao produto.

A decisão de interrupção consultava duas vezes a prova de fontes modeladas,
incluindo buscas de latches que não podiam afetar I_MASK. Agora calcula uma vez
e evita consultar DEV9/i.Link quando seus bits estão mascarados. As leituras
completas de I_STAT continuam usando a prova completa. Não há cache persistente,
estado adicional, alteração de clocks ou mudança no formato do checkpoint.
Os 32 bits de máscara, VBLANK conhecido/desconhecido, desanexação do GS, IRQs
e valores parciais são exercitados pelas regressões IOP.

Validação separada desde `kernel-430m.ps2ncp`, total EE inicial 462.416.676:
30 milhões retiradas em cada caminho, 3.902.111 AOT e 26.097.889 interpretadas.
Estado completo final igual, payload
`9e0f3c40889d97bdb6c246c423d89c45fb0b5e50d49d66959f7e30f9b322fc25`,
PC `0x1C598C`, ciclos 492.416.698. Conferidos também seis campos RGBA com
timestamps, 9.764 amostras PCM e 12 polls de entrada, todos idênticos. Os BMPs
inspecionados ainda são pretos; PCM produzido não significa áudio ouvido.
Os ciclos incluem 22 stalls preservados. Contagem EE não é contador de updates.

### Medição primária

Um aquecimento por variante, excluído previamente, depois cinco pares alternados.
Sem compilação, hashing, checkpoint de saída ou diagnóstico da tarefa na janela.
Carga de estado fica fora do intervalo. Todos os resultados foram mantidos:

| Par | Controle (s) | Candidata (s) |
|---|---:|---:|
| 1 | 7,74950 | 6,39163 |
| 2 | 7,18646 | 6,26583 |
| 3 | 7,00968 | 6,09726 |
| 4 | 7,67705 | 6,18758 |
| 5 | 7,30649 | 8,17289 |
| Mediana | **7,30649** | **6,26583** |

Tempo mediano −14,24%, throughput +16,61%; quatro pares favoráveis e um
desfavorável. Faixas 7,00968–7,74950 e 6,09726–8,17289 s. A dispersão não
autoriza excluir o último par nem prometer ganho em toda sessão. Dados em
`out/reconstruction/superbust/irq-primary-results.json`; validação e inspeções
em `irq-*-460m-*`. A repetição do sampling teve 73 amostras; uma janela maior
obteve 133 e apontou timers, interpretação, observação e fetch como custos
restantes. É uma indicação grosseira, insuficiente para quantificar a próxima
otimização sem uma coleta maior da cena alvo.

O incremento anterior de raízes observadas, numa janela inicial sem vídeo/PCM,
teve cinco pares favoráveis, mediana 4,62605 → 0,903548 s por 30 milhões
(faixas 4,60507–4,63669 e 0,895888–0,984331). É ganho nesse prefixo,
não no tabuleiro perdido. A tradução parcial do kernel preservou o estado,
mas sua comparação instrumentada de 100 milhões piorou 32,1299 → 32,8494 s;
esse resultado negativo também foi preservado e não virou medição primária.

### Resultado combinado com as raízes recuperadas

A continuação identificou quatro raízes de jogo adicionais nos PCs reais do
diagnóstico. Foram passadas por `aot_arg=--entry=` a `ps2n-dev prepare`, sem
endereços específicos no runtime. A preparação `1794075695383049` produziu
7.704 instruções estáticas e 87 raízes verificadas. O executável separado em
`expanded-runner/` conserva o núcleo da candidata IRQ. A cobertura na mesma
janela passou de 3.902.111 para 29.795.610 AOT (99,32%), com 204.390
interpretadas. Estado completo e RGBA/PCM/input/ciclos coincidiram novamente.

Cinco novos pares, com o mesmo checkpoint, settings e aquecimento predefinido,
comparam o controle original à combinação IRQ + raízes, sem diagnóstico:

| Par | Controle (s) | Combinação (s) |
|---|---:|---:|
| 1 | 7,14204 | 1,81378 |
| 2 | 7,10424 | 1,97608 |
| 3 | 7,17145 | 1,83249 |
| 4 | 7,24360 | 1,89792 |
| 5 | 7,11990 | 1,82918 |
| Mediana | **7,14204** | **1,83249** |

Tempo −74,34%, throughput **3,90×** nesta janela; cinco pares favoráveis.
Faixas 7,10424–7,24360 e 1,81378–1,97608 s. Não atribuir todo o ganho à
mudança IRQ: sua contribuição isolada está medida acima. Dados completos em
`combined-primary-results.json`. A tradução original foi regenerada em
`measured-aot/` e seu conjunto conferido com a prova anterior
`d33f4fefc763db357bf0ec80b189dd71547da8b819270a83a8ca928676b50000`;
as partições e executáveis medidos permanecem preservados.

A continuação headless alcançou 792.416.676 instruções EE e checkpoint
`coverage-expanded-760m.ps2ncp`, com leitura real do disco, 61 campos e
9.072.784 escritas de pixels adicionais na janela de 300 milhões. Esses
números são progresso e tráfego, não prova de imagem correta ou gameplay.
Uma nova continuação e inspeção alcançaram 1.202.416.676 instruções, checkpoint
`scene-expanded-1170m.ps2ncp`. O scanout aceito ainda produz preto; a janela
inspecionada contém dois campos e 286.720 escritas de pixels, equivalentes
ao tamanho de dois framebuffers. Limpeza e seleção do buffer são hipóteses
a distinguir por captura do produtor GIF, não um diagnóstico estabelecido.
Não foi estabelecido um defeito de scanout nem um deadlock: leituras CDVD/SIF
continuam avançando. O sampling após expandir o AOT (`profile8-samples.log`)
obteve só 76 amostras válidas, com latches/espera do IOP e timers em destaque;
seu tamanho não permite escolher com precisão outra otimização estrutural.

### Restauração, ferramentas e escopo

A restauração de uma transação MCH pendente construía `RdramChain{0}`, rejeitado
antes de ler os dispositivos serializados. O placeholder válido e a validação
final de 1–32 dispositivos corrigem a restauração. O boot original contínuo e
retomado em 17 + 0,1 milhão deram o mesmo payload `e91f28b6…`; ambos também
atingiram a mesma recusa do encoder 0x7E. A ponte de helpers VU aceita somente
o esquema auditado, com opt-in de runtime e revalidação; um esquema arbitrário
continua recusado sem alterar o destino.

Passaram IOP M2.5/M2.6/M2.7, checkpoint completo, análise e o pipeline real de
preparação/replay. O primeiro ensaio da nova fixture IRQ exigia nomes de fontes
que o formatador não possui para bits 26–31; corrigida a expectativa textual,
os 32 bits passaram sem mudar a exigência de recusa. Logs das falhas preservados.
O lote independente de 120 casos ALU também passou na validação congelada.

Ferramentas instaladas e verificadas: Vulkan SDK 1.4.363.0, Python 3.13.17,
aqtinstall 3.3.0, Qt 6.8.3 MSVC2022, WPT do ADK 10.1.26100.9457 e Remote Tools
17.14.42. `glslc`/`spirv-val` validaram o shader real; Vulkan identificou RTX 4070.
Três testes de GPU/DirectX/sessão Qt offscreen passaram no build de verificação,
com 1.455 primitivas reais na GPU no teste correspondente. Isso não mede o jogo.
O coletor VSDiagnostics continuou rejeitando seu agente mesmo após reparo oficial;
o sampler existente resolveu a coleta. Very Sleepy 0.91 foi instalado, mas não
executado por exigir janela de progresso. Nenhuma sessão gráfica foi aberta.

Super Bust permanece em cenário experimental preparado e medido; menu/partida
controlável não foram demonstrados. Simpsons e Shadow conservam seus registros
isolados e não recebem promoção de suporte por este incremento. Não houve push,
PR, Release ou CI remota dos novos commits. O launcher não foi medido.

| Título/revisão | Identificação/preparação | Boot, menu e partida | Sessão/desempenho deste incremento |
|---|---|---|---|
| Super Bust USA, SLUS-20115 | Disco/ELF conferidos; AOT preparado | Boot original recusa encoder; experimento avança; menu/partida não demonstrados | Equivalência headless e pares acima; sem sessão humana |
| Simpsons USA, SLUS-20624 | Identificação histórica; sem novo preparo comercial nesta tarefa | Conservar fronteiras isoladas registradas; nenhum novo menu/partida | Sem novo ensaio ou medição |
| Shadow USA, SCUS-97472 | Identificação histórica; artefatos/trabalho da investigação separada preservados | Nenhuma promoção de suporte por este incremento | Sem nova sessão ou medição nesta tarefa |

## Cache de decodificação do interpretador — 07/11/2026

O dispatcher compartilhado agora reutiliza metadados de até 256 instruções
por chamada de `run_ee_boot`, com chave PC/palavra. A leitura de memória ocorre
antes de consultar o cache: privilégios, TLB, bits desconhecidos e mudanças de
código continuam sendo verificados. O cache é local à execução, não contém
código host gerado e não entra no checkpoint. A API sem cache permanece como
referência. Os blocos AOT e as semânticas das instruções não foram alterados.

A seleção veio de sampling do Release no trecho de boot reconstruído, não do
perfil histórico do tabuleiro: entre 1.404 amostras da thread principal,
`try_decode_r5900`/ `decode_r5900` somaram 14,46%. Foi usado o
[sampler existente de gow-native-pc](https://github.com/Vnnc1/gow-native-pc/blob/fbca76d2981881363c5b7f63eacb880244a8c162/tools/profile/sampler.cpp),
compilado localmente e dirigido ao PID criado pela tarefa. O código do sampler
ficou em `out/`; nenhum profiler foi adicionado ao produto.

### Medição e equivalência

Cenário: 32 milhões de instruções do boot experimental reconstruído, ROM0
`d6653f4e…`, SCPH-70004:GH-032-11 e NVM `5c3c7a94…`. Conserva as hipóteses
já registradas em `out/reconstruction/experiment-stage1.args.json` para
encoder, SYNCV, SBUS, IOP e SPU2. Não é boot original sem hipóteses nem
execução de um jogo. Todas as 32 milhões de instruções foram interpretadas;
zero instruções EE AOT ou VU1 foram usadas nessa janela.

Fonte congelada em `out/investigation-nov07/source`: `b0bbed1` mais as
alterações locais presentes no início da coleta, preservadas em
`baseline-working.diff`, e somente este incremento entre os dois executáveis.
MSVC 19.44, Ninja, Release `/O2 /Ob2 /DNDEBUG /Z7`, link `/DEBUG`,
profiling intrusivo OFF, backends GPU OFF. `/Z7` evita a falha C1902 do
gerenciador PDB encontrada na configuração inicial com `/Zi`, sem encerrar
serviços de compilação de outras tarefas. As duas variantes usam os mesmos
flags; os símbolos correspondentes foram preservados.

Cinco pares com ordem alternada, após um aquecimento por variante, sem build
ou testes desta tarefa durante a coleta. Tempos de execução, excluindo carga
de checkpoint, salvamento, hashing e exportação:

| Par | Antes (s) | Depois (s) |
|---|---:|---:|
| 1 | 3,20387 | 2,79908 |
| 2 | 3,09927 | 2,78142 |
| 3 | 3,18577 | 2,76248 |
| 4 | 3,23499 | 2,83813 |
| 5 | 3,24235 | 2,74888 |
| Mediana | **3,20387** | **2,78142** |

Todos os pares favoreceram o cache. Tempo mediano **−13,19%**, throughput
**+15,19%** nesta janela. Amostras completas, aquecimentos e executáveis:
`out/investigation-nov07/primary-results.json`, `reference/` e `build/`.
O cache remove uma parte do custo de fallback; esses números não medem FPS,
launcher, áudio ouvido nem gameplay comercial.

Validação separada antes/depois e continuação de 10 + 22 milhões produziram
o mesmo checkpoint completo: PC `0x8000E5E8`, 32 milhões de instruções/ciclos,
payload SHA-256
`86eca61f3fd27c3643fb62d9fa0f6d1e1e7bbf7eb73b6ad6958564397745558b`.
O esquema permaneceu `11a100b3…`; só a identidade de runtime muda.
A continuação usa cache frio após a restauração. Resumo e checkpoints privados
em `out/investigation-nov07/validation-summary.json` e `*-validation.ps2ncp`.
A igualdade é do estado modelado, não uma certificação física ou audiovisual.

**5/5 regressões passaram** na fonte congelada: boot M2.0 híbrido, cache EE,
timers, contexto EE e checkpoint completo. O novo caso cobre hit, colisão,
bytes modificados, palavra desconhecida, instrução não suportada seguida de
reparo, remapeamento TLB e privilégio. Log `focused-tests.log` no mesmo diretório.
A repetição separada do sampling obteve 1.416 amostras da thread de execução;
fetch/TLB, execução EE e observação continuam relevantes. Seus tempos não
participam da série primária. Alterações concorrentes posteriores ao snapshot
não recebem esta medição. Sem publicação/CI, sessão gráfica ou ganho no jogo
demonstrado nesta etapa; nenhum ensaio de emulação permaneceu ativo.

## Artefatos comerciais ausentes; incremento de preparação — 07/11/2026

Nesta execução o mantenedor confirmou ter apagado `out/`. Os discos e a
coleção local de firmware existem, mas os checkpoints do tabuleiro, manifestos
experimentais, executáveis pareados e perfis citados nas seções históricas não
existem neste checkout. Não há baseline comercial reproduzido nem sampling
Release novo; não foi escolhida uma otimização do núcleo a partir de custos
históricos. Sem sessão gráfica, push, PR ou CI remota.

O incremento independente implementado é a preparação observada do AOT e o
primeiro subconjunto executável de ps2autotests. No fluxo CMake/ps2n-dev real,
um callback carregado de dados passa de 5/3 instruções AOT/interpretadas para
8/0, com sequência de eventos e estado inspecionado iguais. O replay conserva
o SHA-256 do executável e reutiliza as partições. Isso valida descoberta,
preparação e equivalência dessa fixture; não mede FPS, áudio ou gameplay.
Detalhes, identidade e comandos em [DEV-TOOLS](DEV-TOOLS.md).

Não existem novos pares temporais comerciais nem dispersão a reportar. Os
tempos de build/execução dos testes incluem diagnóstico e não substituem a
janela primária. O lote ALU confere 120 resultados independentes em ambos os
motores; não promove compatibilidade dos títulos. Super Bust-A-Move conserva
apenas a evidência comercial histórica já descrita; Simpsons permanece sem
vídeo jogável demonstrado, e Shadow sem cena controlável certificada. Seus
checkpoints apagados precisam ser reconstruídos antes da próxima continuação.

Verificação adicional de checkpoint completo falhou por esquema na integração
e no controle sem este incremento, com o mesmo hash. Falha preexistente no
conjunto local preservado; não foi admitida migração presumida. Detalhes em
[DEV-TOOLS](DEV-TOOLS.md).

## Histórico — eventos/TLB de 05/11/2026

Código: `c23188e`. Implementação própria BSD; sem alteração de firmware,
aritmética convidada, clocks, renderer ou código proprietário distribuído.

## Gargalo e referência

O perfil anterior do menu registrou 65.205.240 chamadas de sincronização,
IOP e dispositivos em 30 milhões de instruções EE. GS representava cerca
de 1% do tempo exclusivo instrumentado. Esses tempos incluem overhead de
profiling e não são parcelas dos tempos Release abaixo.

O [scheduler do PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp)
foi referência de organização por próximos eventos, sem copiar código ou
transportar seu kernel HLE. O runtime mantém o firmware convidado e a execução do IOP.
A busca linear nas 48 entradas TLB também continuava em cada acesso traduzido.

## Mudanças e contratos

- A sincronização consulta o estado atual antes de entrar nos motores.
  Conserva a fronteira IOP 8:1, mudanças/desconexão GS mesmo sem aposentadoria,
  Q0/Q1 após E, VIF pendente, recusas e timestamps gráficos de checkpoint.
  DMA ativo continua no parser original, inclusive antes do próximo beat,
  para preservar prioridade e recusa imediatas. Não há deadline persistente
  que possa sobreviver a uma escrita de registrador sem invalidação.
- O cache TLB por instância guarda somente o índice de uma correspondência
  única, por página virtual de 4 KiB e ASID. Toda entrada compara apenas
  bits acima dessa página; o caminho conserva seleção da metade, S, V/D,
  modo de cache e endereço físico. Privilégio e alinhamento continuam no
  barramento comum. Bytes, conhecimento e falhas não são armazenados no cache.
- Escritas TLB e reinicializações invalidam o cache. A leitura de checkpoint
  também limpa os índices copiados do destino antes de substituir a TLB.
  Campos serializados e esquema `c6495503…` permanecem idênticos; os helpers
  e metadados host novos participam integralmente da identidade de runtime.
  A troca de runtime continua exigindo opt-in explícito.

## Medição comercial

Windows x64, Ryzen 7 5700X3D, MSVC 19.44, Release, profiling desativado,
mesmos módulos AOT, entrada solta, afinidade 16384 e prioridade AboveNormal.
Retomada do manifesto local `interactive-menu.json`, orçamento de 30 milhões,
CPU GS para isolar o runtime. Carga e hashes finais ficam fora do intervalo;
scanout dos campos originais fica dentro. Nenhuma compilação concorrente
da tarefa. Os executáveis de controle e sincronização isolada foram congelados.

Cinco trios alternados, mais um trio de aquecimento excluído previamente.
Os 25 intervalos por variante correspondem a cinco intervalos de cada execução.

| Variante | Mediana: 30 milhões (s) | CPU total do processo (s) | Campo original (s) |
|---|---:|---:|---:|
| Controle contemporâneo | 11,341572 | 12,640625 | 1,838974 |
| Sincronização isolada | 10,722638 | 12,031250 | 1,733293 |
| Sincronização + cache TLB | **7,776684** | **9,078125** | **1,254745** |

Combinação: throughput mediano +45,84%, tempo de execução −31,43%.
O tempo de CPU do processo, que inclui carga/relatório/hashes, caiu 28,18%.
Faixas de execução: controle 10,182395–14,494990 s; sincronização
9,260945–10,928277 s; combinação 7,751996–7,936340 s. A combinação venceu
os cinco pares contra ambos os controles. Houve interferência externa,
inclusive no aquecimento; todos os valores foram conservados. O percentual
não é promessa de ganho universal nem medição de uma máquina ociosa.

Uma coleta independente anterior da sincronização isolada teve mediana
11,537564 → 10,936279 s (+5,50% de throughput). No trio final houve um par
em que ela perdeu para o controle. O ganho maior vem da combinação com TLB;
não atribuir à sincronização sozinha o resultado total.

Executável de medida: 11.940.352 → 11.949.056 bytes (+8.704).
Cache host: 256 entradas de índice por Memory (2 KiB nesta ABI).
Launcher local `out/launcher-msvc/ps2-native.exe` recompilado com o mesmo núcleo
e AOT. Não houve comparação de tempo de build nem nova sessão visual.

## Equivalência e limites

Os 18 ensaios da matriz conservaram o estado completo
`80c2d0d3f6635d1b24934ca1853b006f592888bd7a9293daa4126c10cf4da9a5`,
25.446.236 instruções AOT e 4.553.764 interpretadas em cada janela.
Validações separadas conferiram seis campos RGBA, VRAM e máscaras completas,
9.766 amostras PCM com timestamps, 12 polls SIO2, estado e ciclos/IRQ:
controle/novo, interpretação pura, checkpoint de 10 + 20 milhões, Vulkan
e Direct3D 12. Originais de firmware, disco e checkpoint foram preservados.

Vinte testes focados passaram: boot/TLB, ASID, permissões, sobreposição,
páginas grandes, contexto/cache EE, AOT, timers/IRQ, IOP/GS, DMA/SIF,
VIF/VU/Q, SPU2, checkpoint e sessão experimental Qt. A primeira verificação
de checkpoint detectou a mudança indevida de identidade pelos helpers;
o esquema original foi restaurado sem afrouxar a validação.

**1,254745 s por campo continua muito acima de tempo real.** Não demonstra
primeira partida ou gameplay. Busca de instruções continua verificando bytes;
despacho/observação e amostragem por instrução permanecem, assim como o
interpretador explícito. Um novo perfil deve medir esses custos após TLB.

CI de `c23188e` ainda não executada: a revisão automática bloqueou o push
para `origin/main`, exigindo autorização específica para publicar as alterações.
Nenhum ensaio comercial permanece ativo ao encerrar. Evidência local em
`out/runtime-final-summary.json`, `runtime-final-performance-results.json`,
`runtime-final-*-results.json` e `runtime-tlb-tests.log`; não distribuir
artefatos comerciais ou seus traces.

## Busca de instruções por página — 07/11/2026

Pedido: desempenho e fidelidade de Super Bust-A-Move, por execução direta,
sem launcher. O profiler existente agora separa interrupções e observação;
os escopos desaparecem dos builds sem profiling. Antes da mudança, a coleta
registrou 30.000.006 consultas de interrupção, 30 milhões de observações e
35.093.967 acessos de memória. Tempos instrumentados não são parcelas do
tempo Release.

### Mudança e isolamento

`Memory::fetch32` reutiliza a resolução de até 16 páginas completas de 4 KiB
de RAM, ROM ou scratchpad. A chave inclui página virtual, ASID, privilégio,
ERL e mapeamento. Escritas TLB, reset/carregamento e leitura de checkpoint
invalidam o cache host. Alinhamento continua obrigatório; páginas parciais
e dispositivos usam a resolução comum. Bytes e máscaras de conhecimento
são lidos novamente após sincronização DMA. Não são armazenados conteúdo
executável, conhecimento ou falhas. A comparação dos bytes AOT permanece.

384 bytes host adicionais por Memory nesta ABI, sem campos novos no
checkpoint, configuração pública, dependências ou regra por título.
Fontes congeladas a partir de `7e7ceda` mais o trabalho local existente,
registradas em `out/perf-nov07-baseline-provenance.json`. O par muda somente
`src/memory.cpp` e o cache host de `memory.hpp`; trabalho concorrente foi
preservado. Núcleo e traduções AOT recompilados juntos.

MSVC 19.44, `/O2 /Ob2 /DNDEBUG`, profiling OFF, afinidade 16384, prioridade
AboveNormal, mesmo manifesto `interactive-menu.json`, checkpoint, firmware,
disco verificados e entrada FFFF. GS CPU para isolar execução. Carga e hashes
finais fora da janela; scanout original dentro. Nenhuma compilação da tarefa
durante esta matriz. Cinco pares alternados, após aquecimento excluído previamente:

| Par | Controle (s / 30 milhões) | Cache de resolução (s) |
|---|---:|---:|
| 1 | 12,1153910 | 10,7895352 |
| 2 | 8,1082155 | 7,8050025 |
| 3 | 7,9320712 | 7,3612893 |
| 4 | 7,9313101 | 7,3675274 |
| 5 | 7,9974762 | 7,5366949 |
| **Mediana** | **7,9974762** | **7,5366949** |

Throughput mediano **+6,11%**, tempo **−5,76%**. Todos os cinco pares
favoreceram a mudança; houve variação externa, sobretudo no primeiro.
Aquecimento: 8,1666972 / 8,5739689 s, sem favorecer o cache.
Mediana dos 25 intervalos: **1,256657 → 1,174710 s por campo original**.
CPU total do processo, incluindo carga/hashes: 9,609375 → 9,156250 s.
Executável de medida: 12.016.128 → 12.017.152 bytes. Não é tempo real nem
medição de updates do jogo.

Duas candidatas foram descartadas: divisores especializados dos timers
venceram dois de cinco pares; consulta especializada de conhecimento de
palavras alinhadas venceu um de cinco. Não entraram no núcleo. Todos os
valores, incluindo aquecimentos, ficaram em
`out/perf-nov07-{timers,memory,fetch}-performance-results.json`; resumo em
`out/perf-nov07-fetch-summary.json`. O aquecimento inicial dos timers
coincidiu com o teste de checkpoint e não foi usado como evidência de ganho.
Os casos de borda dos três prescalers ficaram no snapshot local da candidata.

### Equivalência e referências

Dez validações pareadas: CPU, interpretação pura, checkpoint 10 + 20 milhões,
Vulkan e D3D12. Todas conservaram estado completo
`82c384eb3fc5822d9ea3469d265c514ab904ca16e6c0c02441ca07e37194c2ed`,
VRAM/máscaras, seis campos RGBA 640 × 224, 9.766 amostras PCM com timestamps,
12 polls de entrada, ciclos EE/IOP e efeitos modelados. Na janela híbrida,
25.446.236 instruções AOT e 4.553.764 interpretadas; VU1 não executou.
RTX 4070: 294 primitivas reais por backend (282 texturizadas e 12 flat),
108 lotes, zero fallback. Seus tempos são de validação, não um benchmark
pareado dos backends. Evidência em `out/perf-nov07-validation-summary.json`.

Oito regressões focadas passaram no snapshot pareado: boot híbrido/TLB,
cache/contexto EE, timers/IRQ, HBLANK IOP, quadro/timer Direct ELF e checkpoint.
Novos casos cobrem máscaras após stores, isolamento do scratchpad,
privilégio/ERL e ROM1 parcial. Testes existentes cobrem ASID,
mudança de TLB, páginas grandes, alteração de bytes e restauração aquecida.

Uma segunda compilação, de `7e7ceda` mais apenas esta alteração, passou
sete dos oito testes. `native-ee-context` ainda espera a recusa de
`MADD.S` para `1*2+1,25`, já suportado pelo núcleo. A implementação anterior
de `fetch32`, recompilada nessa mesma cópia, falha exatamente na mesma
asserção (`multiplier rounding not modeled`). É falha preexistente da base;
o teste corrigido nas edições concorrentes passou no snapshot pareado.
Essa correção alheia não foi incluída neste incremento.
Após sua integração em `3a40856`, uma nova cópia isolada desse commit mais
esta alteração foi recompilada: **8/8 testes passaram**, incluindo contexto
EE e checkpoint completo. A identidade de esquema permaneceu `5662ca9c…`.
Logs: `out/perf-nov07-final-clean-{build,tests}.log`.

Perfil posterior do snapshot pareado, obtido separadamente e com o mesmo
estado final: EE não atribuído 22,53%, interrupções 17,95%, observação
13,46%, memória 11,44%, IOP 10,81%, verificação de código 9,83% e GS CPU
3,74% do tempo exclusivo instrumentado. A instrumentação faz dezenas de
milhões de leituras de relógio e altera essas proporções. Usar os caminhos
identificados para uma próxima coleta Release, não como previsão de ganho
nem justificativa para retirar validações. Dados em
`out/perf-nov07-profile-final-results.json`.

Referências consultadas, sem copiar ou integrar código externo:

- [Play!: comandos por quadro](https://github.com/jpd002/Play-/blob/master/Source/gs/GSH_Vulkan/GSH_VulkanFrameCommandBuffer.cpp)
  e [licença permissiva](https://github.com/jpd002/Play-/blob/master/License.txt): submissões/fences.
- [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs/blob/main/README.md):
  compute/replay e limites declarados de precisão física de interpolação/AA1.
- [PCSX2: texture cache](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSTextureCache.h)
  e [Counters.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Counters.cpp):
  validade/invalidação e organização por eventos, sob GPL-3.0+.
- [Play!: COP_SCU](https://github.com/jpd002/Play-/blob/master/Source/COP_SCU.cpp):
  comparação arquitetural; o incremento artificial de Count por leitura
  não foi transportado para este runtime.

O núcleo permanece original BSD. A autorização para usar referências GPL
não resultou em incorporação nesta etapa. Sem sessão humana, áudio ouvido,
launcher, primeira partida ou certificação de outros dispositivos. A melhoria
medida pertence ao menu headless. CI/publicação não executadas nesta etapa.

## Super Bust-A-Move: partida e otimizações em investigação — 07/11/2026

O caminho headless com controle SIO2 alcançou ROUND A1, movimentou o lançador
e disparou bolhas. Não houve injeção de estado de jogo para selecionar a
partida. O checkpoint local `out/realtime-board3.ps2ncp` permite repetir a
mesma cena; não é um artefato distribuível. Ainda **não há tempo real**.

Snapshots isolados de `8af295e` mantêm separadas as experiências e as mudanças
concorrentes de compatibilidade. Os experimentos incluem agrupamento exato
de esperas EE/IOP até o próximo evento, validação de código por gerações de
página, triângulos AA1 no compute existente e janelas comprovadas de amostragem
de interrupções. O backend usa primitivas reais na RTX 4070. Nenhuma chamada
de cache, efeito de DMA, imagem ou síntese SPU2 foi removida para obter ganho.
Esses protótipos ainda exigem integração e validação na revisão corrente.

No tabuleiro, cinco pares Release/Vulkan de 30 milhões de instruções, sem
instrumentação intrusiva, reduziram a mediana de 2,5427510 para 2,1623833 s
com as janelas de IRQ/DMA. Cada par favoreceu a alteração. Os seis campos
originais representam cerca de 0,1 s de jogo; o resultado permanece muito
abaixo da velocidade original. Estado completo em todos os pares:
`ebbde2076edc85ecfb03129d03678664175fe477a8547c52c107614d58560042`.
O rasterizador CPU também produziu esse estado na mesma janela. Logs e
amostras: `out/realtime-irq-gameplay-measure-results.json`.

Uma continuação com 600 milhões de instruções e movimento/disparos produziu
122 campos, 244 polls e 195.314 amostras PCM. As 122 imagens, entradas,
PCM, estado GS e estado completo coincidiram antes/depois. Tempos de
validação: 54,3015537 e 46,7071104 s; não são uma série primária pareada.
O primeiro ensaio posterior recusou sobrescrever um WAV existente após
terminar a execução convidada. A repetição com prefixos exclusivos concluiu
em 46,8851845 s e preservou os mesmos resultados. Estado final:
`de2dddd1a3336270e39f17f1b61b95a2728563932d3e42f3b4a14807ee182516`.
Evidências: `out/realtime-gameplay-control-before-0.log`,
`out/realtime-gameplay-control2-after-0.log` e respectivas capturas/WAVs.
PCM comparado não constitui uma sessão de áudio ouvida.

O observador diagnóstico existente identificou uma rotina de manutenção de
D-cache entre os maiores grupos ainda interpretados. Capturaram-se somente
bytes executáveis conhecidos do checkpoint, com identidade da imagem e
guardas de código mantidas, para tradução AOT local do alias observado.
A tradução parcial ficou mais lenta: resultado negativo preservado em
`out/realtime-kernel-native-results.json`. Traduzir também CACHE, reutilizando
sua implementação existente, reduziu trocas de engine, mas sem uma janela
de ordenação adequada também ficou mais lento nos três pares registrados
em `out/realtime-kernel-cache-results.json`. A versão que admite apenas
DXWBIN na janela quiescente comprovada passou na comparação de imagem,
PCM, controle e estado; três pares deram medianas de 2,1546817 e 2,0356066 s
(`out/realtime-kernel-inert-results.json`). A cobertura nativa aumentou em
645.006 instruções nessa janela. Os 104 pares diferenciais de CACHE
detectaram uma diferença na falha de privilégio antes de `begin`; a correção
preserva PC/estado, recusa, aposentadoria e eventos. Passaram também o
resultado independente de bytes/máscaras após writeback e os casos de CACHE
intercalado com deadlines de IRQ, tracing completo e budgets arbitrários.

Uma alteração posterior evita construir mensagens CACHE e mudanças de
engine depois de um observador limitado encher seu registro. Observadores
externos continuam interessados por padrão; diagnósticos permanecem ativos
mesmo com o trace cheio. Três pares Release/Vulkan reduziram a mediana de
2,0298109 para 1,7669221 s, com o mesmo estado completo. A validação separada
preservou todos os campos, PCM e entradas. Artefatos:
`out/realtime-trace-primary-results.json` e
`out/realtime-trace-validate-results.json`. Novo perfil separado em
`out/realtime-trace-profile-results.json`; não usar seus tempos intrusivos
como desempenho do executável normal.

A integração local dessas alterações com a árvore corrente passou **25/26**
regressões focadas, incluindo os dois backends físicos e os novos testes de
CACHE/IRQ. O teste de checkpoint completo mantém a recusa de esquema vista
antes dessas integrações, nas alterações concorrentes de VU; nenhum hash de
esquema foi inventado ou admitido para contornar a falha. Logs:
`out/realtime-integration3-build-retry.log` e
`out/realtime-integration3-tests.log`. Não há CI publicado nem aceitação de
tempo real. Triângulos texturizados no compute são a investigação seguinte.

O primeiro protótipo de triângulos texturizados passou nos dois backends
físicos: 32 combinações de formato, winding, filtragem e AA1, texel
independente e linha mantida no rasterizador CPU. O esquema permanece
`5662ca9c…`: os novos métodos de observação/CACHE não têm campos de máquina
e foram marcados como helpers de execução, preservando a identidade anterior
sem admitir hashes novos. O checkpoint completo passou nessa base isolada.

Na cena comercial, todos os campos, PCM, entrada e estado completo
coincidiram. A cobertura GPU subiu de 1.508 para 1.893 primitivas e os
fallbacks caíram de 731 para 346, mas os lotes aumentaram de 729 para 792.
Três pares primários deram medianas de 1,7628481 para 1,7279783 s: ganho
pequeno, sem aceitação de tempo real. Evidências:
`out/realtime-triangle-{validate,primary}-results.json`,
`out/realtime-triangle-tests-gpu3.log` e
`out/realtime-triangle-tests-retry.log` (este último contém também as duas
falhas anteriores de fixture/validação, corrigidas na rodada GPU seguinte).
As falhas e amostras anteriores foram preservadas.

Essa versão só aceita páginas de textura já presentes no cache lógico;
TEXFLUSH ainda descarrega a fila. A experiência seguinte combina a prova de
uma amostra em um único tile lógico (preserva exatamente seu par de páginas)
com invalidação sem espera de host. Leituras, sobrescritas e FINISH continuam
exigindo a conclusão das dependências. Testes e medição dessa experiência
estão pendentes; nenhuma conclusão de ganho é atribuída a ela.

Referências adicionais efetivamente consultadas: rastreamento de páginas do
[paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs/blob/be775c796503f21d00d8b78ced4039b43605b992/gs/page_tracker.cpp),
[backend GL do PS2Recomp](https://github.com/LightVelox/PS2Recomp/blob/67bbd44ca258b279515cceab082468b0b11d5078/ps2xRuntime/src/lib/gs/gpu/gs_gpu_backend.cpp),
[esperas do interpretador PCSX2](https://github.com/PCSX2/pcsx2/blob/c09e8f34dd5c84fc1c39f2e8231a0589fdd2c4ef/pcsx2/Interpreter.cpp)
e [operações de cache do PCSX2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Cache.cpp).
DXWBIN escreve a linha suja e a invalida; não é uma instrução descartável.
A autorização do mantenedor para reutilizar GPL está registrada no pedido,
mas estas experiências não incorporaram código externo.

Atualização da mesma cena, 07/11: a prova de tile frio e o TEXFLUSH sem
submissão incondicional passaram nos dois backends físicos
(`out/realtime-triangle-queue-tests-gpu4.log`). Cinco pares Release/Vulkan
deram mediana de 1,7225732 para 1,6300283 s por 30 milhões de instruções,
reduzindo 792 para 244 lotes. A validação separada preservou o estado
`ebbde2076edc85ecfb03129d03678664175fe477a8547c52c107614d58560042`,
os seis campos, PCM e entrada (`out/realtime-triangle-queue-validate-results.json`).
Isso representa aproximadamente 0,1 s convidado; continua longe de tempo real.

A formatação de erros do GS somente quando necessários preservou a comparação
completa e reduziu a mediana de cinco pares de 1,6286384 para 1,6068984 s
(`out/realtime-lazy-primary-results.json`). Três tentativas sem ganho foram
retiradas: janela de IRQ enquanto o IOP executa (1,6532963/1,7091519 s,
`out/realtime-step-validate-results.json`), formatação tardia no IOP
(medianas de três pares 1,6022163/1,627348 s,
`out/realtime-lazy-iop-primary-results.json`) e dicas de índice para buscar
latches (validação 1,631034/1,6759709 s,
`out/realtime-latch-validate-results.json`). Todas preservaram a comparação
da cena; equivalência sem melhora não cumpre a meta de desempenho.

A integração na árvore corrente passou 25/26 regressões, incluindo Vulkan
e Direct3D 12 físicos (`out/realtime-integration4-tests.log`). A restauração
de checkpoint continua recusando uma diferença de esquema na árvore com
alterações concorrentes de VU. Nenhum hash foi admitido sem auditoria do
codec. Os resultados comerciais acima pertencem à fonte congelada e
identificada em cada build, não certificam a árvore corrente.

O conjunto gráfico validado foi registrado em `d36a53d`. A auditoria do
checkpoint foi concluída em `2a91994`: entre `1f3b1fb` e os helpers VU
atuais, `checkpoint_fields.inc`, os campos de EEState e os demais campos
armazenados não mudaram. Somente o par de esquemas revisado
`26777753…`/`db30806f…` é aceito, com opt-in de runtime e reserialização
idêntica do payload. A cadeia de migrações e a nova fixture passaram em
`out/realtime-integration5-wire-tests.log` (39,70 s). Os 25 testes restantes
da integração, incluindo os dois backends físicos, já haviam passado.

O agrupamento experimental de trechos aritméticos AOT foi retirado:
`out/realtime-linear-validate-results.json` preservou estado/campos/PCM/entrada,
mas passou de 1,6332603 para 1,7098904 s. Sua tradução adicional mudou
25.001 para 25.014 instruções estáticas; isso também não produziu ganho.
O experimento expôs uma representação de âncora de timer diferente da
amostragem por instrução. A regressão aritmética sem agrupamento reproduz
a diferença no orçamento 32 sem a correção e passa com a materialização
do último tick antes de finalizar a amostragem adiada
(`out/realtime-arithmetic-anchor-before-tests.log` e
`out/realtime-arithmetic-anchor-tests.log`). Nenhum tick/efeito foi removido.

Um perfil separado e limitado do mesmo tabuleiro encontrou 18.472 pares
PC/palavra, sem exceder o limite de 65.536; exclui as instruções já
agregadas. A espera mais frequente consulta CHCR do DMA VIF1
(`0x10009000`). O cache de identidade recebeu 4.553.243 consultas:
4.046.124 hits, 497.308 falhas de chave, 9.699 falhas de geração DMA e
112 falhas de versão CPU. Houve 1.661.018 pedidos de sincronização DMA
na validação de código (`out/realtime-pc-hot-vulkan.{log,err}`). Esse build
é diagnóstico, com instrumentação adicional no profiler existente, e não
fornece as medições primárias abaixo.

Aumentar o cache de 1.024 para 4.096 entradas preservou a validação completa
e melhorou os cinco pares: mediana de 1,613396 para 1,5737666 s
(`out/realtime-code-capacity-primary-results.json`). Todos os pares
favoreceram a mudança. As regressões de polling/timers/identidade do código
e cache passaram (`out/realtime-code-capacity-tests.log`, 2/2, 25,27 s).
A validação comercial separada continua com o estado `ebbde207…`, seis
campos e os mesmos PCM/inputs. Continua aproximadamente 16 vezes abaixo
do tempo original; não há aceitação de tempo real.

### Super Bust-A-Move: preparação AOT ampliada e validação corrente

A escolha normal de inlining para `aot_enter_checked` preservou as mesmas
validações e melhorou cinco pares na fonte congelada: mediana
1,5837779 → 1,5534447 s (`out/realtime-guard-primary-results.json`). O
executável caiu de 20.541.952 para 13.501.440 bytes. O conjunto corrente
passou as regressões de CACHE, polling e identidade (3/3); a reconstrução
completa de `9b1a20f` mais essa linha passou 24 testes de CPU/software e os
dois testes físicos Vulkan/D3D12 (`out/realtime-latest-cpu-tests.log`,
`out/realtime-latest-physical-tests.log`).

A preparação existente foi executada com mais 1.006 entradas observadas,
verificadas contra segmentos executáveis e palavras do ELF local, sem
palavras divergentes. A tradução passou de 25.014 para 93.437 instruções
estáticas. Cinco pares no mesmo esquema preservaram o estado completo
`550e7840…`, os seis campos RGBA, PCM e entrada: mediana
1,5625226 → 1,5051987 s (`out/realtime-observed-aot-primary-results.json`).
O interpretador caiu de 825.587 para 152.500 instruções por 30 milhões.
Isso permanece aproximadamente 15 vezes aquém da velocidade original.

A partida com entrada roteirizada avançou 600 milhões de instruções e
122 campos, incluindo movimentos e disparos. Antes/depois preservaram o
estado completo `348d8085…`, todos os campos RGBA, PCM e consultas do pad
(`out/realtime-scripted-entries-validation-results.json`). A captura do
campo 120 foi inspecionada: tabuleiro e personagens visíveis. A execução
ocorreu durante compilações; seus tempos não são amostras primárias. Há
PCM/WAV capturado, mas isso não certifica uma sessão interativa com som
ouvido e pacing de áudio em tempo real.

A revisão `9b1a20f` mais o guard e as novas traduções também preservou RGBA,
PCM, entrada e estado GS na janela curta
(`out/realtime-latest-commercial-results.json`). O novo esquema de provas
numéricas muda a serialização: o hash completo entre essas duas revisões
não é comparável e não foi anunciado como igual.

O perfil diagnóstico atual recompilou integralmente core e traduções com
os mesmos cabeçalhos e profiling habilitado. Ele substitui atribuições de
perfis antigos que reutilizaram objetos AOT com outro cabeçalho do
profiler. Os pares PC/palavra anteriormente usados na preparação foram
verificados contra o ELF e o resultado AOT teve comparação completa
independente; os benchmarks primários não usam profiling.

`out/realtime-current-profile-time-vulkan.log` registra aproximadamente
26% de EE exclusivo, 14% de GS, 14% de observação, 9% de interrupções e
8% de IOP. Os contadores separados registram 5.229.468 verificações de
código e 2.066.351 pedidos de sincronização DMA. O perfil instrumentado é
somente diagnóstico e não fornece throughput primário.

A instrumentação limitada de elegibilidade do horizonte preservou o mesmo
estado e encontrou 104.729 recusas pelo estado do IOP, 33.360 por VIF1/DMA
ativo e 25.102 aceitações (`out/realtime-current-profile-time-vulkan.err`).
Ainda não foi implementada uma ampliação dessa prova. A experiência de
observação agrupada anterior foi descartada: preservou estado, mas piorou
a mediana de 1,6274629 para 1,7525385 s. Todos os resultados negativos
permanecem locais. Tempo real continua pendente.
