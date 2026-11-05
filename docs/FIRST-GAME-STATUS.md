# Primeiro jogo comercial — execução parcial, sem jogabilidade

Execução local atualizada em 05/11/2026. **A meta de jogabilidade não foi atingida.**
O diagnóstico experimental produz imagens originais; inicialização normal,
menu, respostas visuais aos controles e gameplay não foram demonstrados.
Código original BSD-3-Clause; nenhum código de emulador, firmware, IRX ou jogo foi incorporado.

## Realinhamento técnico — 05/11/2026

Novo pedido do mantenedor retomou a execução encerrada no registro anterior.
O mesmo núcleo C++20 atende AOT, interpretação e dispositivos. Nenhuma
condição por jogo/PC foi acrescentada ao runtime. BIOS original continua
separada do cenário experimental: o controle original manteve a recusa
do encoder `0x7E`, EE `0x80007D20` / 17.690.079 instruções.

### Avanço funcional e correções

- `e07dac1`: SQC2 transporta `FloatBounds` pela proveniência já existente
  de memória; LQC2 restaura os limites no VU. QMTC2 aproveita candidatos
  finitos da metade baixa do GPR. Escritas independentes invalidam o
  certificado mesmo com bytes iguais; VF00 permanece constante.
- A retomada estrita começou em 7.125.000.000 instruções do jogo, antes
  da perda de informação, com zero origens numéricas incertas. Passou
  pelo antigo XYZ/DMA em 7.727.172.060; 87 campos comparáveis com o 125
  tiveram hashes idênticos e 38 capturas adicionais superaram a fronteira.
  Parou em VSQRT `0x1CD558`, 7.914.035.443 do jogo. A exceção genérica
  reporta o PC seguinte `0x1CD55C`; o operando foi isolado antes da SQRT.
- `1d97661`: cancelagem de `1 - [0x3F7FFFF1,0x3F7FFFF2]` produz apenas
  `0x35600000` ou `0x35700000`. O hull antigo tinha máscara `0x1FFFFF`;
  a enumeração limitada dos operandos conserva `0x100000`. SQRT usa
  até 256 combinações esparsas, filtradas pelos limites provados, mesmo
  quando o intervalo numérico é largo. Domínios densos continuam recusados.
- A continuação foi salva em 9.114.035.442 e 10.314.035.442 do jogo.
  O bloqueio seguinte é IOP `0x00062140`, contador 1.367.184.044:
  `SPU2 KON requires known constant zero left and right voice gain;
  audible synthesis is not modeled`. Não houve substituição por silêncio.
  O EE ainda podia esperar com IOP recusado; os números posteriores
  desse EE não constituem avanço funcional do sistema.
  Inspeção passiva isolou `SH` / `0xA44501A0`, base conhecida `0xBF900400`,
  valor conhecido `0x001F`, endereço físico `0x1F9005A0`: ativa as vozes
  0–4 do core SPU2 1. Seus ganhos L/R conhecidos são 2445/1796,
  418/1524, 1844/1405, 895/1680 e 1524/1524. A recusa é a ausência
  de síntese para esses ganhos audíveis, sem bits arbitrários a resolver.
- `bd6238b`: salvamento e carregamento recusam esse estado parcial de
  IOP antes de substituir o checkpoint/destino. O CLI o reporta como
  bloqueio. O sentinela sem ROM do IOP dormente no Direct ELF não é uma
  instrução recusada. O arquivo diagnóstico após a recusa foi preservado
  para inspeção, separado dos checkpoints retomáveis.
- `8f5bb8d`: todos os cabeçalhos usados no fingerprint de runtime passam
  a provocar reconfiguração do CMake, inclusive os excluídos do esquema
  de campos. A regra gerada do Ninja inclui `boot.hpp`, `profile.hpp` e
  `ee_semantics.hpp`; o build incremental terminou sem trabalho pendente.

Os quatro botões foram entregues ao SIO2 real do núcleo, ativos em nível
baixo. Start: ciclos 8.025.129.087–8.079.240.837 (11 campos); Cross:
8.256.333.837–8.349.799.587 (19); Up: 8.551.488.837–8.733.501.087
(37); Down: 8.876.159.337–9.004.059.837 (26). Não houve resposta visual
verificável: as imagens ainda mostram o indicador circular de carregamento.
Não confundir isso com menu nem navegação. As hipóteses prévias de encoder,
GS/UV, reset/timing SPU2, cache e SIO2 continuam explícitas no cenário;
nenhuma foi promovida a comportamento físico comprovado.

### Checkpoint anterior ao áudio

`out/game-first/superbust-before-audible-spu2.ps2ncp`: PC `0x1C5A6C`,
10.937.363.474 instruções totais / 10.904.946.798 do jogo,
10.937.372.580 ciclos EE. IOP ativo `0xB5D8`, contador 1.367.171.572,
sem recusa. Recuperado do último arquivo válido preservado (9.114 bilhões),
sem repetir o boot, com salvamento verificado a cada 100 milhões e limite
de 18 minutos; terminou em 815,954 s. O último estado anteriormente salvo
após falha do IOP não podia ser usado como ponto de retomada.

Payload de 61.393.914 bytes, SHA-256
`8ac0e76cd59d7a5497bca94013c14bc7ed03475d0a43c10359b842b002cf62a2`;
cenário estrito `be4e5be35272efd3349d94f3aad14e7a10741dc5a3f27cc42bcc6a9f127fa994`.
Runtime do executável de recuperação (profiling compilado, sessão inativa):
`10ba56b4e4e182fa18a0119d92628dc881d3503f020ecdcd7fd48f66de9ae801`.
Build normal final (profiling OFF):
`614fb7b39a1d3c825c1e7b06ef0b510093ebfc6596d8db0a9385ccadcb657f5d`.
Esquema comum `b32d8b0d82c1d14b0c3f8e5fb8912a2a8495f113256e122b4e924fd34f4e10e2`;
o opt-in explícito de mudança de runtime continua necessário.

O build normal restaurou em 0,864 s e reproduziu a recusa de KON numa
continuação limitada a 200 mil instruções (0,117 s só de execução), sem
salvar o estado recusado. O payload do ponto seguro permaneceu idêntico.
Duas continuações de 1.000 instruções com IOP ainda ativo concordaram no
estado completo: `202bc7db52216d357ef3b6e266b396979b8fcf663485be7952cf8f3da9db6f6b`.
Essa janela curta ainda não executou instruções AOT; a equivalência dos
motores com AOT efetivo é demonstrada nas janelas abaixo.
Identidades dos executáveis e fontes locais em
`out/game-first/realignment-final-build-provenance.json`; preservar os
binários estáticos correspondentes e recompilar tudo junto ao trocar ABI.

### AOT e baseline reproduzível

`cfc18eb` permite ler palavras originais de jump tables com guarda
SLTIU/BEQ/LW comprovada em segmento PF_X, sem relaxar relocations ou
`--table`. Recusa BSS, guarda ausente/desviável e sobreposição com código,
inclusive instruções descobertas depois. O ELF original passou de 13.168
instruções/4.332 blocos para 17.336/6.199, com três tabelas e 154 evidências
de alvos. O módulo local usa também `--entry=0x001D2EF8`, raiz explícita
do mecanismo existente conferida contra os bytes do ELF: 17.364 instruções,
6.217 blocos. Descoberta automática do caminho até essa raiz ainda falta.

Geração local pelo CLI existente: `ps2n-aot out/game-first/superbust.elf
out/game-first/superbust-realigned.cpp --hybrid --entry=0x00100068
--entry=0x00100084 --entry=0x001D2EF8`. O ELF é conferido por SHA-256
antes de associar o módulo; nenhum byte original é alterado.

Baseline local: Windows x64, MSVC 19.44.35229 Release `/O2`, sem profiling, sem jogo
ou compilação concorrentes. Checkpoint `realignment-before-sqrt.ps2ncp`,
7.914.035.442 do jogo; orçamento 1.000.000, sem captura/trace completo.
Cinco amostras por motor, intercaladas; relógio só de execução, excluindo
carga/salvamento e exportação. A raiz adicional só pode ser usada após
verificação de identidade do ELF e compilação conjunta com o núcleo.

| Módulo/motor | AOT / interpretadas | Mediana (s) | Faixa (s) |
|---|---:|---:|---:|
| AOT anterior | 15.320 / 984.680 | 0,550323 | 0,534293–0,569873 |
| AOT regenerado com tabelas | 15.546 / 984.454 | 0,521497 | 0,520453–0,565326 |
| Interpretador, comparação inicial | 0 / 1.000.000 | 0,503585 | 0,500379–0,519579 |
| AOT com raiz explícita | 990.864 / 9.136 | 0,430393 | 0,419924–0,487026 |
| Interpretador, comparação da raiz | 0 / 1.000.000 | 0,516288 | 0,512297–0,594122 |

Estado completo de **todas** essas amostras, SHA-256:
`4d545ee5de218678974a2a08bc9371c8bca7bf7bb5cc8574bbfc7af8fd81ed5d`.
A cobertura 99,0864% e a razão de throughput 1,1996 valem apenas para
essa janela e raiz local; não comprovam cobertura automática, FPS,
desempenho do jogo inteiro ou vantagem sobre outro projeto.

Repetição final, depois da otimização de polling, sem profiling ou
execução/compilação concorrente, mesmo checkpoint/orçamento e cinco
amostras intercaladas por motor:

| Motor | Mediana (s) | Faixa (s) | Throughput pela mediana |
|---|---:|---:|---:|
| AOT anterior à otimização | 0,420983 | 0,419600–0,445996 | 2,375 M instr./s |
| AOT final | 0,392803 | 0,389986–0,404047 | 2,546 M instr./s |
| Interpretador final | 0,413145 | 0,413000–0,414590 | 2,420 M instr./s |

As quinze amostras mantiveram o SHA-256 completo acima. Ganho de throughput
AOT pela retirada do polling repetido: 7,17% nesta janela. Comparação final
AOT/interpretação: 5,18%; a otimização também beneficia o interpretador,
portanto a razão histórica de 1,1996 não descreve o build final.

No trace de 20.000 instruções da região ativa, 6.553 passaram pelo
interpretador: 630 opcodes exclusivos desse motor e 5.923 instruções
traduzíveis fora dos blocos CFG provados. PCs mais frequentes: `80003B00`,
`80003B04`, `80003B08` (130 cada), `80002AD8`, `80002AE0` (128 cada),
incluindo kernel em RAM fora do módulo ELF. Não atribuir todo fallback à FPU.

### Perfil de componentes e sincronização

Configurar com `-DPS2N_ENABLE_PROFILING=ON` e usar `--profile` no CLI;
o padrão OFF elimina temporizadores por acesso e recusa o pedido explícito
de perfil. Contadores fixos locais ao host, sem campos serializados ou
mudança de relógio convidado. O perfil distingue tempo exclusivo e inclusivo;
o segundo inclui filhos e não deve ser somado entre componentes.

Janela de 100.000 instruções, 90.864 AOT, estado completo
`85a9cb466dade4f199a0b0bf73e5d789e0a139509fb2820dce42c11f0b8523a2`:

| Componente | Exclusivo antes (ms) | Após reutilizar amostragem (ms) |
|---|---:|---:|
| EE/dispatcher/relatório | 28,004 | 23,396 |
| Sincronização | 26,047 | 22,311 |
| Dispositivos | 24,020 | 20,374 |
| Memória EE | 17,487 | 17,836 |
| IOP | 12,511 | 10,917 |
| VU | 8,417 | 7,141 |
| Verificação de código | 6,106 | 6,090 |
| Provas numéricas | 3,337 | 3,302 |
| GS | 2,109 | 2,109 |
| Apresentação | 0 | 0 |

EE, sincronização e dispositivos são os três maiores grupos exclusivos
medidos. A segunda amostragem de IRQ na mesma fronteira foi removida entre
dispatcher e motor; instruções seguintes continuam amostradas, incluindo
delay slots/exceções. As chamadas de sync/IOP caíram de 249.987 para 215.161
(13,93%), com SHA-256 completo idêntico. Verificação de código permaneceu
por instrução; custo inclusivo após a mudança: 43,113 ms.
Uma segunda janela de 100.000 instruções, retomada em 9.114.035.442 do
jogo, também preservou o estado antes/depois da otimização:
`580d2443fa52fc92581a923f9853128b3b8ba41547d63af3e9501ab0a19072fe`.

O perfil custa caro nessa janela curta: 128,04 ms ativo contra 58,671 ms
inativo no build instrumentado anterior. Seus tempos incluem o próprio
custo dos temporizadores e chamadas sem trabalho pendente; não representam
frações exatas do build normal. IOP inclui CPU/RAM interna; provas numéricas
medem as operações instrumentadas, não toda propagação de bits desconhecidos.
EE é o tempo residual da sessão, incluindo dispatcher, observadores e
construção do relatório; não é uma medição isolada do pipeline da CPU.
Sem callback de imagem nessa medição, apresentação é zero; não há FPS ou
tempo de frame de gameplay para declarar.

### Campos consecutivos e validação final

Janela distinta de 30 milhões de instruções, retomada em 9.114.035.442 do
jogo, sem profiling/compilação/outro jogo concorrente, callback existente
com relógio e hash RGBA, sem gravar imagens. Híbrido: 25.793.845 AOT /
4.206.155 interpretadas (85,9795% AOT), 11,1036 s; interpretação: 12,0894 s.
Ambos produziram o estado completo
`3f8afd42bd49e7856379311267c45b75d7df1a6005ac2e61610fb1d0026aa852`.

Sete campos originais consecutivos de 640×224 coincidiram em ciclo e
SHA-256 RGBA, de 9.146.718.087 a 9.176.233.587 ciclos EE, separados por
4.919.250 ciclos modelados. Nos seis intervalos completos, mediana host
1.818,560 ms no híbrido (1.797,830–1.834,470) e 1.970,665 ms no interpretador
(1.968,220–2.024,590). Uma amostra por motor, incluindo scanout/hash e
sincronização: é custo de campos do **carregamento**, sem inferência de
FPS de jogo ou desempenho em tempo real. A cobertura dessa janela maior
não deve ser substituída pelos 99,0864% da janela curta predominantemente idle.

Build normal OFF: 12 testes focados passaram, cobrindo cache/contexto EE,
incerteza FPU/VU, CPU/FPU/frame/exceções/timers Direct ELF, microVU e
checkpoint completo/CLI. Os testes de equivalência com profiling ativo e
recusa explícita no build OFF passaram; o save com IOP recusado preserva
o arquivo válido. O controle de BIOS original voltou ao mesmo encoder,
sem ativar hipóteses. Não foi repetida a suíte inteira localmente.
CI do código final `8f5bb8d`: seis alvos do
[núcleo](https://github.com/fabegalo/ps2-native/actions/runs/41968835701) e seis do
[launcher](https://github.com/fabegalo/ps2-native/actions/runs/41968835789)
concluídos com sucesso: Windows/Linux/macOS x64 e ARM64. Isso cobre builds
e regressões sintéticas; a execução comercial descrita foi local no Windows x64.

Evidência local ignorada: `out/game-first/realignment-game*.log`,
`realignment-benchmark-*.log`, `realignment-benchmark-summary.json`,
`realignment-fallback-causes.json`, `realignment-frame-comparison.json`,
`realignment-component-{profile,optimized}.log`. AOT proprietário,
checkpoints, imagens e traces permanecem locais.
Medições finais em `realignment-final-benchmark-*.log`, resumo JSON
correspondente, `realignment-frames*.log`, `realignment-audio-frontier.log`
e `realignment-audio-parameters.log`. Nenhum ensaio local ficou ativo.

## Encerramento solicitado — estado preservado em 04/11 (histórico)

Execução encerrada a pedido do mantenedor, sem novo ensaio longo.
O 126 terminou na mesma recusa DMA em 292,052 s desde 7,125 bilhões;
o experimento escalar não trouxe avanço. O 127 testou a identidade
numérica corrigida com política escalar estrita: retomada de 7,725 bilhões
em 0,974 s, mesma recusa após 1,234 s de execução. A correção genérica
preserva o fato de que duas cópias do mesmo valor representam o mesmo
operando; domínios independentes com hulls iguais continuam independentes.
O caso `(+1|-1)` copiado e multiplicado reproduziu a falha antes da
correção e passou depois, junto da divisão de cópias não nulas.

Os ajustes finais também preservam certificados da via de cache não
afetada, mantêm origens durante writeback/refill e recusam ressuscitar
uma origem após escrita independente com os mesmos bytes. Dados e
visibilidade do hardware continuam sujeitos às mesmas operações reais.
Quatro testes focados passaram: cache EE, SIF0 DMA, conhecimento FPU e
checkpoint completo. Não houve avanço de instruções na fronteira real;
nenhum ganho isolado de desempenho é declarado.

Checkpoint final completo `out/game-first/superbust-final-safe.ps2ncp`:
PC `0x80002ACC`, 7.759.586.676 instruções totais, 7.727.170.000 do jogo,
7.759.588.054 ciclos EE. Payload SHA-256
`f2ec4be4e971662f4e6ff514606992ffcc306c01e073ddd69afe047ff925cb97`,
60.162.874 bytes; runtime
`e19665218846a961f26985a7080cf8678c7a9f765369acdb1747f455975ecf8b`.
Fonte fixada em `cad88c0` mais os ajustes de identidade/cache, registrada
em `superbust-final-build-provenance.json`; preservar esse build compatível
para restaurar. Mudanças posteriores da `main` exigem revisar o esquema,
não reinterpretar o estado com ABI nova.

Verificação limitada final em `superbust-final-frontier.log`: carrega em
0,859 s e reproduz a recusa após 2.060 instruções, `0x1CE460`,
7.759.588.736 totais / 7.727.172.060 do jogo. O checkpoint seguro
continua anterior à instrução recusada. Imagem real preservada em
`superbust-menu127-vblank-293.bmp/.png`, ainda o indicador circular;
menu, navegação e gameplay não demonstrados. Start e Cross foram
aplicados pelo SIO2 durante a animação, sem resposta visual comprovada;
Up/Down no menu e partida continuam pendentes.

CI de `cad88c0`: núcleo e launcher aprovados nos seis alvos. A CI dos
últimos ajustes foi solicitada; sua conclusão permanece pendente ao parar.

## Fronteira localizada e refinamento experimental — 125/126

O 125 executou a nova prova estrita de multiplicação desde o prefixo
de seis bilhões, com parada/retomada em 6,325 bilhões para fixar fontes,
núcleo, AOT e harness contra alterações simultâneas da `main`. Fonte
fixada `2810c76`, runtime
`bb3af1d4b8d0783702bffd8519a38ef8eba75699d64f8d42c37f1ab6f7516ba4`;
retomada em 0,843 s. Os sete testes focados e a regressão com BIOS
original passaram: mesma recusa do encoder `0x7E`, `0x80007D20` /
17.690.079, sem hipótese de encoder nesse controle original.

Cross pelo SIO2, pressionado em 7.479.092.337 ciclos/campo 295 e solto
em 7.651.266.087/campo 330: 35 campos. Comparação por ciclo dos frames
com o 122 não mostrou mudança visual. O 125 terminou na mesma recusa
DMA VIF1, `0x1CE460`, 7.727.172.060 do jogo, 7.759.588.736 totais,
7.759.590.114 ciclos EE, IOP `0xB46C` / 969.948.764; 635,076 s desde
6,325 bilhões. Último frame real `superbust-menu125-vblank-293.bmp`,
hash RGBA `795b25e7a3d14b09885350a6bfb1f6bbb649356f7956b4db9c28f16fa8198bf9`.
Continua sendo um indicador circular, sem menu comprovado.

Busca binária com restaurações curtas localizou a primeira escrita
incerta: `SW` em `0x1CD120`, 7.727.167.542 do jogo. O registrador de
origem conserva 32 coordenadas Y consecutivas (`0x867C..0x869B`);
não há um valor único a publicar ao DMA. Checkpoint completo
`superbust-before-unknown-xy.ps2ncp` em 7.727.167.541, PC `0x1CD120`,
uma instrução antes. `superbust-coordinate-prefix7727.ps2ncp` preserva
7.727.000.000, apenas 172.060 instruções antes da recusa final.
Logs `coordinate-first-write-proof.log` e `coordinate-bisect-*.log`.

Foi preservado também `superbust-before-3d-7125.ps2ncp`, 7.125.000.000
do jogo / 7.157.416.676 totais, PC `0x81FC8`, payload SHA-256
`c739441f2699dfa1df2aa45cdfc54f1f253c4eecfc0b9e61c55eccddf7e95c8b`.
Esse prefixo tem zero origens numéricas incertas. A API explícita
`refine_checkpoint_scalar_carry` admite o experimento genérico já
existente somente com relógio/estado vinculados, FPRs conhecidos,
nenhuma relação armazenada e contador monotônico de origens zero.
O contador não é limpo por reset ou sobrescrita: estados já incertos
continuam recusados. A API normal de configuração mantém sua guarda.

A hipótese seleciona uma perda de carry dentro dos limites do produto
estrito, portanto concorda com todos os resultados/flags anteriormente
certificados. O teste compara 16.384 pares cobrindo todas as mantissas
grossas e extremos de expoente; asserções de estado completo verificam
que o refinamento muda somente uma flag host. O modelo físico desse
carry permanece **não verificado**, com evidência de referência externa;
o carregamento não o habilita automaticamente. Todos os sete testes
focados passaram após corrigir a instância reutilizada no teste da guarda.

126 concluído desde esse prefixo, sem repetir 7,125 bilhões de
instruções. Núcleo/AOT/harness com fontes fixadas em `6c94fd8` mais
o refinamento; esquema
`830377c10823d2223579db00dc995da1a05a0fcc433a85be4014e50ea96ac772`,
runtime `d62d319d99c680fd8995165517d12ea352a62db57193f85956422a3cc725d088`.
Carregamento em 0,976 s. Cenário experimental separado
`499f9eaf33cc3468a1fa1e189bde88cbfd152fb8405511be2c363fe93ce5b493`,
log `superbust-menu-126.log`, entrada `superbust-pad126.txt` solta (`FFFF`).
Terminou na mesma recusa; ainda faltam menu, resposta visual de Start/Cross/Up/Down e partida.

## Compatibilidade dos checkpoints após a atualização de FPU

A `main` recebeu `e0cfc2f`, que acrescentou duas flags de configuração
escalar ao estado. Os checkpoints dos ensaios anteriores continuam
utilizáveis por uma migração explícita, limitada aos hashes de origem
auditados e ao esquema de destino
`72d8f630ea9a0befdba0e33bbc9d80394b6a2c29db31cf0a402bd6299876a61e`.
O carregamento exige `allow_runtime_change`, reserializa todos os campos
antigos e compara seu SHA-256 com o payload original antes de substituir
o estado de destino. O experimento escalar não existia nas revisões
anteriores: fica desativado. Como seu histórico aritmético não era
registrado, a janela de configuração é fechada conservadoramente;
não se permite ativá-lo depois da restauração. Nenhum bit de hardware
desconhecido é preenchido pela migração.

Núcleo, AOT e harness recompilados com os mesmos cabeçalhos;
runtime local `9df78bdbd6c63e6d149cabab796d22f547b8ac88f85380a2a6e186bf8d6f95e5`.
Sete testes focados passaram, incluindo equivalência entre migração e
estado atual, continuação, recusa sem opt-in e guarda de configuração.
`checkpoint-scalar-migration-real.log`: o checkpoint real de 7,2 bilhões
carregou em 1,130 s. Duas continuações de 1.000 instruções terminaram em
7.232.417.676 totais, PC `0x1D2F08`, IOP ciclo 904.052.219, com estado
completo idêntico, SHA-256
`bb3a164a785c5cd997b812675a2418956822af40aa62eea588eac6d0d479b798`.
O novo hash inclui as duas flags host, enquanto a verificação interna
comprova byte a byte o payload legado.

`superbust-menu123-frontier.log`: restauração do checkpoint 121 em
1,091 s, PC `0x80002ACC`; a mesma recusa DMA foi reproduzida em
2.060 instruções/0,038364 s, 7.727.172.060 do jogo, 7.759.590.114
ciclos EE, IOP `0xB46C` / 969.948.764. Esse teste preserva a retomada
rápida; não demonstra que a nova FPU corrigiu coordenadas já calculadas.

## Checkpoints completos — retomada do ensaio 111

O 111 superou a recusa UV do 110 e terminou em CVT.W.S `0x186DFC`,
7.323.722.395 instruções do jogo; IOP `0x47A40` / 919.517.433 ativo.
7.356.139.465 ciclos EE, 394 stalls, 3.072,17 s; 383.994.854 escritas
de pixels e SIF1/SIF0 2.822.736/1.817.216 bytes. As capturas posteriores
ao Acclaim mostram uma animação parcial, sem menu comprovado.
`ps2n-debug log/report` das ferramentas novas identificou a recusa e os
bits desconhecidos de F0. Logs e imagens reais permanecem ignorados em
`out/game-first/superbust-menu-111.log` e `superbust-menu111-diagnostic`.

Foi criado um formato binário completo, por campos explícitos, sem
endereços host, handles ou callbacks. Preserva EE/COP0/COP1/VU, cache
sujo e tags, memória e incerteza, relações/domínios com identidade
compartilhada, IOP com load/branch pendente, DMA/SIF/CDVD/SIO2/SPU2,
GIF/VIF/GS/VRAM e temporização. Ponteiros externos de relógio/RAM são
validados e religados. Firmware, perfil, NVM e disco precisam ser
reapresentados com a mesma identidade; o disco exige SHA-256 fixado.
Checksum, limites de tamanho, esquema e cenário são verificados antes
de substituir o destino. Mudança de runtime exige opt-in explícito;
mudança de esquema é recusada. Salvamento atômico somente após retorno
bem-sucedido do executor; uma recusa parcial mantém o checkpoint anterior.

Para preservar o próprio 111, já em execução sem essa API, `ps2n-capture`
pausou o host na entrada verificada de `run_ee_boot`, entre chunks.
Adaptador compilado com os cabeçalhos/ABI exatos do processo 11b87b0,
imagem SHA-256 `9084f7988190150f689899edbea9ee955a8de8b491f2da07ddc46bb7ff62b321`.
O mapa foi aceito somente com seção de código idêntica. Teste separado
comprovou equivalência entre o salvador legado e o leitor atual, inclusive
continuação. A única adição de layout atual é propriedade host de strings
de diagnóstico restauradas; os campos serializados coincidem. O processo
original foi retomado após cada captura; nenhuma instrução/ciclo convidado
foi suprimido ou estado de hardware inventado.

| Checkpoint local | Instruções totais | PC | Payload SHA-256 |
|---|---:|---|---|
| `superbust-live-6000.ps2ncp` | 6.032.416.676 | `0x81FD0` | `17c3d26eee750ba1eadc5a5d2ac2ebbc34248c233b3c19eb5093137d007dd01b` |
| `superbust-live-7200.ps2ncp` | 7.232.416.676 | `0x1D2F0C` | `e91844ddbcddea11af618f77594766d8b0ec98453a284459dc2c169d3949219b` |

O prefixo de firmware tem 32.416.676 instruções: o segundo checkpoint
preserva exatamente 7,2 bilhões do jogo. Seus 59.930.607 bytes de payload
carregaram em **0,856724 s**, com os mesmos dados locais e hipóteses do
111. Duas continuações reais de 1.000 instruções chegaram a `0x1D2F08`,
7.232.417.676 totais, estado SHA-256
`b650f12dacda94ff43b4a0d67c6dcc682b5590a1d5499c92c8cd43967209c9a7`.
O checkpoint anterior carregou em 0,823251 s. Isso mede retomada e evita
repetir o prefixo; não representa aceleração das instruções ou FPS.
Evidências `checkpoint-real-{verify,7200-verify}.log` locais.

`ps2n-checkpoint inspect FILE` valida e mostra PC, contagens, hashes e
metadados. `ps2n-boot` e `ps2n-run` aceitam `--resume=FILE`,
`--checkpoint-out=FILE`, `--checkpoint-every=N` (padrão 100 milhões)
e `--checkpoint-scenario=SHA256` do cenário explícito.
`--checkpoint-allow-runtime-change` permite somente a revisão consciente
do runtime; não ignora esquema, firmware, NVM, disco ou cenário.
Scripts externos precisam preservar também seu estado host nos metadados;
a captura legada foi retomada com entrada manual e todos os botões soltos.
AOT é religado aos mesmos bytes/identidades e mantém suas guardas.

Testes focados passaram: estado contínuo versus retomado, cache sujo,
bits desconhecidos, domínios compartilhados, branch/load IOP pendentes,
GIF/IMAGE pela metade e Q pendente; CLI periódico versus execução contínua
com payload completo idêntico; recusas de cenário, corrupção e instrução
parcial sem destruir o último checkpoint. Formato e `.ps2ncp` são locais,
ignorados pelo Git; nenhum firmware, jogo ou estado proprietário foi comitado.
As hipóteses de hardware do 111 permanecem experimentais. O checkpoint
não comprova menu, controles, áudio ou jogabilidade.

Continuação 112: o mesmo checkpoint foi carregado, sem boot ou reinjeção
do ELF, com núcleo/AOT/harness recompilados juntos. O CVT.W.S agora prova
resultado único quando todos os valores permitidos pela máscara de bits
convertem para o mesmo inteiro; continua recusando resultados divergentes.
F0 `4329B5C0`, máscara `3F`, converte para 169 em todas as 64 variantes.
O [manual EE, página 356](https://studylib.net/doc/28989088/ee-insns)
define truncamento para zero e saturação por sinal. Consultado também
[PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/include/ps2_runtime_macros.h#L856):
seu uso de arredondamento host não foi adotado. Implementação independente
por inteiros e prova dos extremos, sem copiar código GPL.

O 112 superou CVT e parou 25 instruções depois, em DIV.S `0x186E60`,
7.323.722.420 do jogo, IOP ativo `0x47888` / 919.517.436.
Foram executadas só 123.722.420 instruções após a retomada, em 60,7225 s;
o prefixo de 7,2 bilhões foi preservado. Último checkpoint periódico
`superbust-menu112-latest.ps2ncp`, 7,3 bilhões do jogo, antes da recusa.
Cinco frames reais parciais, sem menu. Três testes focados aprovados:
contexto EE/conversões, checkpoint completo e CLI periódico.

## Continuações por checkpoint — 113 a 121

Os ensaios seguintes partiram de checkpoints completos; nenhum prefixo
de firmware ou jogo foi substituído por PC/contagem inventados.

| Ensaio | Instruções do jogo | Fronteira observada |
|---|---:|---|
| 113 | 7.358.161.563 | MUL.S `0x1D7D1C`, domínio opaco de sinal/expoente |
| 114 | 7.412.273.815 | CVT.W.S `0x11CD20`, inteiros possíveis 4095/4096 |
| 115 | 7.412.274.109 | ADDIU `0x187984` consome domínio perdido no cache; relatório antigo apontava a sucessora `0x187988` |
| 116 | 7.471.303.396 | DIV.S `0x186E60`, domínio maior que o limite finito inicial |
| 117 | 7.727.166.728 | C.LT.S `0x1CD584`, magnitude positiva incerta contra zero; relatório apontava `0x1CD588` |
| 118 | 7.727.166.924 | DIV.S `0x186E60`, intervalo amplo dividido por dois |
| 119/120 | 7.727.172.060 | DMA VIF1 recusa coordenadas XYZ indeterminadas |
| 121 | 7.727.170.000 | parada voluntária bem-sucedida, antes da recusa; checkpoint completo salvo |

Correções genéricas: DIV usa o circuito SRT para cada candidato de domínios
pequenos e conserva os possíveis quocientes e a classe comum de flags.
Divisor normal potência de dois também aceita intervalos: verificação local
de todos os 8.388.608 significandos normalizados confirmou preservação exata
no circuito compartilhado; sinais, expoentes, zeros e clamps continuam no
mesmo modelo escalar. É validação do circuito existente, não nova medição
física. Domínios opacos são particionados por sinal/expoente. CVT conserva
todos os inteiros possíveis em domínio limitado; comparações ordenadas
só prosseguem quando todos os extremos provam a mesma condição. Igualdade
falsa exige intervalos disjuntos, não só extremos diferentes.

Certificados de valores agora distinguem cache residente e backing RAM.
Escritas por alias uncached não trocam a origem da linha stale; manutenção,
refill e writeback invalidam certificados pertinentes. Não se publicou cache
sujo automaticamente, não se tornou DMA coerente e não se alterou timing.
O teste de checkpoint passou a habilitar efetivamente D-cache e comprova
que o valor sujo está ausente do backing antes de salvar.

O fingerprint de estado deixou de incluir `ee_semantics.hpp`, que contém
somente execução/contexto host; esse cabeçalho continua no fingerprint de
runtime. Uma ponte explicitamente revisada aceita apenas o par de schemas
`9199cae3…e2cc` → `771b6280…3f88`, com opt-in de alteração de runtime.
Não há mudança dos campos/bytes serializados. O checkpoint real de 7,2
bilhões foi recarregado pelo leitor novo em 0,831849 s: payload manteve
`e91844dd…219b`, e a continuação de 1.000 instruções manteve exatamente
`b650f12d…c9a7`. Mudanças futuras de campos não recebem essa exceção.

Novo checkpoint `out/game-first/superbust-menu121-latest.ps2ncp`:
PC `0x80002ACC`, 7.759.586.676 instruções totais, 7.759.588.054 ciclos,
IOP `0x479D0` / 969.948.506. Payload 58.919.095 bytes, SHA-256
`70824db1fd7da151a40a90cd6e7a807173f43f7a7b925cd05b7bc2e50761021b`.
Retomada em **0,825777 s**, seguida de 2.060 instruções em **0,0395265 s**
até reproduzir a mesma recusa. Logs locais `superbust-frontier-checkpoint.log`
e `superbust-frontier-resume-proof.log`. Isso evita repetir 7,727 bilhões;
não é comparação de FPS ou aceleração das instruções novas.

Na recusa, VIF1 tem MADR `0xF70C20`, QWC 5, TADR `0x5BCE80`.
Tag GIF REGLIST conhecida `B400000000008001` em `0xF70C00`, descriptors
`F5353535310`. O XYZ2 em `0xF70C28` contém valor `FF01860092D6`, máscara
`00FF0000`; outros vértices têm máscaras `7F`, `1FF` e `3F0000`.
São coordenadas incertas, não padding descartável. Não foram consumidas
como coordenadas conhecidas nem transformadas em imagem fictícia.

Sete testes focados passaram: cache, SIF0/DMA, contexto EE, VU float,
domínios FPU, checkpoint completo e CLI periódico. BIOS original repetida
com runner atual: mesmos 17.690.079/`0x80007D20`, sem hipótese do encoder.
A CI inicial do checkpoint detectou uma expectativa antiga de recusa CVT;
o teste foi atualizado para verificar o resultado/domínio certificado.
CI da revisão `9113b7a`: [núcleo](https://github.com/fabegalo/ps2-native/actions/runs/41947644566)
e [launcher](https://github.com/fabegalo/ps2-native/actions/runs/41947644578)
aprovados em Windows x64/ARM64, Linux x64/ARM64 e macOS Intel/Apple Silicon.

Capturas reais de 640x224 mostram uma animação com logotipo no canto,
sem menu demonstrado. Start foi aplicado no 118/119, mas a recusa ocorreu
no mesmo campo, sem resposta visual comprovada. O 122 aplica Start mais
cedo, por SIO2, para testar a possibilidade de avançar pela abertura usando
o controle. Isso é entrada convidada normal, sem salto de PC, alteração da
RAM do jogo ou sucesso sintetizado. Up/Down/Cross no menu e partida pendem.

O 122 terminou: retomou 7,4 bilhões em 0,834544 s, aplicou Start em
7.434.819.087 ciclos/campo 42 e soltou em 7.587.315.837/campo 73,
31 campos. Os 57 frames comparáveis com o 117 tiveram exatamente os mesmos
hashes; atingiu a mesma recusa/contagens, em 170,199 s. Não se demonstrou
resposta visual de Start. Arquivos reais `superbust-menu122-vblank-87.bmp`
e `.png`, 640x224, 6.023 pixels RGB não pretos, RGBA SHA-256
`795b25e7a3d14b09885350a6bfb1f6bbb649356f7956b4db9c28f16fa8198bf9`.
Nenhum processo de ensaio ou observador permaneceu ativo.

## Seleção e identidade — dados da execução inicial

- `games/` contém Shadow of the Colossus (USA), ISO 9660 de DVD, e Tetris Plus (USA), CD CUE/BIN.
  O segundo tem `BOOT`, executável `PS-X EXE` e serial `SLUS-00338`: é PS1, não um ELF R5900.
  O único candidato ao runtime EE existente é **Shadow of the Colossus, SCUS-97472**.
- ISO: 2.693.955.584 bytes, SHA-256
  `d431a5b7e6c7009713d743c22ac5fd61077da40d5f7923e3250fe072a226eacb`.
- `SYSTEM.CNF` aponta `cdrom0:\SCUS_974.72;1`; ELF32 MIPS ET_EXEC, 242.940 bytes, entrada
  `0x00100010`, dois PT_LOAD, sem símbolos de função/relocations verificáveis; SHA-256
  `d16ad76ba8cdf87695d94b1316d51c1f248959ec0749814c31bb7a2d55d9a2e1`.
- Dependências presentes no mesmo disco: `IOPRP300.IMG`, `GAMECORE.XFF`, `KERNEL.XFF`,
  `MANAGER.XFF`, `STARTUP.XFF`, `SG2IOPM1.IRX`; diretórios MODULES2 (MCMAN/MCSERV) e
  MODULES (DBCMAN, DS1O_D, LIBSD, MC2_D, PL2303, SIO2D, SIO2MAN, USBD).
  XFF exige carregamento dinâmico; o bootstrap pequeno não representa todo o código do jogo.
  Não se infere a ordem de uso dos módulos apenas por estarem no disco.

## Execução e bloqueios

| Rota | Evidência | Limite |
|---|---|---|
| Direct ELF inicial | 86.277 instruções retiradas; syscall `v1=0x3C` (SetupThread), PC `0x0010006C` | Entrega da exceção ainda ausente antes desta mudança |
| Direct ELF corrigido, interpretador | 86.278 instruções; `Cause=0x20`, `EPC=0x0010006C`, `EXL=1`; PC `0x80000180` | Checkpoint na entrada da exceção; nenhum handler/kernel é instalado pelo modo bare-metal |
| Direct ELF corrigido, AOT | 86.277 AOT + 1 interpretada, uma troca, mesmo checkpoint/COP0 | 23 instruções estáticas em 4 blocos; 99,9988% da execução deste prefixo, dominado pelo laço de limpeza, não do jogo completo |
| Boot normal do firmware pareado | 17.690.079 instruções; PC `0x80007D20`, zero escritas de pixels | ID `0x7E` do codificador de vídeo, barramento serial `0x42`, não medido |

O firmware é ROMVER `0200EC20040614`, ROM0 SHA-256 `d6653f4e…c6685b`, com a NVM Dragon
SCPH-70004 verificada (`5c3c7a94…b0c39b`), perfil GH-032-11. Não se usou outra revisão,
NVM sintética ou resposta assumida de MMIO. O boot normal foi repetido com o núcleo atual e
confirmou o mesmo bloqueio físico já identificado em M2.8. A outra placa possível e demais
limites dos dados são descritos em [M2.8](M2.8-STATUS.md).

### Experimento separado com kernel parcial

Para continuar verificando funcionalidades necessárias ao mesmo jogo, um harness **local, ignorado**
carregou os segmentos do ELF na RAM do firmware já interrompido. Preservou ROM0, IOP, TLB,
relógio e bits CU; selecionou explicitamente BEV=0, entrada e SP do ambiente de desenvolvimento.
Isso **não executa ExecPS2, não conclui o boot e não comprova um kernel inicializado**.
Os handlers e os resultados de serviços vêm de instruções Sony já residentes em RAM;
nenhum serviço foi interceptado ou substituído por retorno do host.

Esse experimento permitiu localizar e corrigir, em sequência:

1. Retorno do handler ao modo usuário (`ERET` em `0x80000328`), após SetupThread real.
2. Leitura de `T3_MODE`, PC `0x001102D4`, após 302.574 instruções.
3. CACHE DXWBIN por índice em VA zero, PC `0x80002AC8`, após 306.617.
4. PCPYH (`0x001283FC`, 309.275) e PCPYLD (`0x00128408`, 309.278).
5. `kuseg` com ERL durante seção crítica, PC seguinte `0x001103E4`, após 312.657.

Última repetição: **312.702 instruções retiradas**, parada em `0x001103E8` ao escrever
`T2_MODE=0xF82` (`0x10001010`): interrupções de temporizador/INTC ainda não implementadas.
O primeiro experimento zerava CU junto com o Status e parava indevidamente no MFC0 do jogo;
o resultado final preserva esses bits. Nenhuma dessas contagens promove o estágio de aceitação
além de "ELF executa". Não houve imagem, menu original, input ou jogabilidade.

## Código entregue e regressões

- `DiscImage`: setores 2048 e CD raw 2352 Mode 1/Mode 2 Form 1, diretórios/subdiretórios,
  BOOT2 e ELF pelo loader comum. Limites de extensão/leitura, inteiros both-endian e recusas
  explícitas de PS1, Form 2, multi-extent, imagens truncadas e formatos não modelados.
  Leitura aleatória pelo I/O comum; não carrega/copia a ISO inteira. CUE/compressão continuam fora
  dessa API; o probe do launcher existente permanece independente e inalterado.
- SYSCALL/BREAK entregam a exceção arquitetural, preservando EPC/BD em EXL aninhado,
  cancelando o branch pendente e retornando pelo dispatcher. Não implementam serviços HLE.
- Modo usuário/supervisor aplica permissões de segmento e TLB; EXL/ERL selecionam kernel.
  COP0/CACHE sem permissão falham; ERL mapeia os 2 GiB de kuseg por identidade, sem alias de 29 bits.
- CACHE index não traduz pela TLB; operações hit conservam a tradução. O modelo continua coerente
  sem cache físico; acesso a tags/dados não suportados continua recusado.
- PCPYH/PCPYLD compartilham semântica AOT/interpretador, inclusive alias entre operandos e `$zero`.
- Tn_MODE lê a configuração real do subconjunto suportado. EQUF/OVFF permanecem claros porque
  contar com interrupções habilitadas é recusado; não se inventam flags nem conclusão de IRQ.
- CLI `ps2n-run`: ISO via `--disc`, trace/tail, PC/COP0 e escritas GS. `ps2n-aot --disc`
  mantém a identidade do ELF e a geração idempotente. `ps2n-disc` lista/extrai BOOT2 para AOT offline.

Windows x86-64/MSVC Release: 5 testes focados aprovados inicialmente; **38 CTests aprovados**
na regressão final do núcleo (28,52 s), incluindo a guarda de privilégio CACHE. A CI por commit
é registrada abaixo. O teste de compilação em caminhos longos não
foi repetido localmente, pois o adaptador de paths/build não mudou; permanece na CI dos seis alvos.
O novo ELF próprio compara trace, memória, GPRs, COP0 e execução híbrida/interpretada de
SYSCALL, BREAK, delay slot, likely anulado e instruções MMI; 69 instruções, 14 AOT/55 interpretadas.
Testes separados cobrem EXL aninhado, ERL, permissões, cache index/hit, disco e recusas.

Não houve otimização de performance nesta tarefa. As três amostras do novo programa de exceções
mostraram híbrido 0,0419/0,0622/0,0542 ms contra interpretador 0,0363/0,0357/0,0354 ms:
**sem ganho** nessa carga curta. Não se apresenta FPS ou desempenho de gameplay.
Build incremental preserva os fontes AOT idênticos; a verificação posterior informou `ninja: no work to do`.

## Reprodução local

```sh
ps2n-disc "games/Shadow of the Colossus (USA).iso"
ps2n-aot "games/Shadow of the Colossus (USA).iso" out/game-first/scus.cpp --disc --hybrid
# Configurar o build existente com PS2N_ELF_AOT_SOURCE apontando esse C++ local.
ps2n-run-hybrid "games/Shadow of the Colossus (USA).iso" --disc --budget=1000000 --stop=0x80000180 --trace-out=out/game-first/actual-direct-hybrid.log
```

O endereço de parada é o vetor arquitetural da CPU, não um patch do título. A chegada ao checkpoint
não significa que SetupThread foi atendido. Sem kernel, continuar por RAM zerada não seria
inicialização válida do jogo. O launcher não foi alterado.

Registros locais ignorados: `out/game-first/` contém hashes completos de 18 arquivos de bootstrap,
IRX e XFF em `disc-metadata.json`, inventários, fonte/binário do harness, logs de cada bloqueio,
tradução AOT e relatórios de testes. Esses arquivos não são publicados. A ISO original permanece intacta.

## Referências e dados ainda necessários

Implementação independente baseada nos contratos dos manuais:
[EE Core, exceções e memória](https://docs.alexrp.com/mips/ee.pdf),
[EE Instruction Set, PCPYH/PCPYLD e CACHE](https://docs.alexrp.com/mips/ee_insns.pdf),
[EE User, Tn_MODE](https://manuals.plus/m/55bdc71d3aebc33752a7dad6526b78248b6c258294fcf8a3ebed1f010da22365.pdf).
As avaliações de gow-native-pc, PS2Recomp, Play! e PCSX2 continuam em
[REFERENCE-AUDIT](REFERENCE-AUDIT.md); nenhum código dessas referências foi integrado.

O ID do codificador não está na ROM0/NVM e não há captura desse registrador entre os recursos locais.
É preciso evidência do hardware compatível para concluir essa rota sem valores inventados.
Também faltam implementação de timer/INTC, serviços IOP/SIF e os demais dispositivos que a execução
vier a exigir. Não se confunde esse trabalho interno pendente com o dado físico ausente.
O próximo trabalho do mesmo jogo fica exclusivamente em [NEXT-TASK](NEXT-TASK.md).

## 03/11/2026 — seleção atual: The Simpsons: Hit & Run

Os novos discos locais foram examinados: God of War (SCUS-97399), Simpsons
(SLUS-20624) e Shadow. Simpsons foi priorizado pelo ELF monolítico, sem a camada
XFF de Shadow. ISO SHA-256 `9a06ea661088893df39103e0a15a8aec613246d9e181f5beee1d2b7222a44ebb`;
ELF `57f29e593b9f290a74ddf86576cd63916b1d68059bcc51972320a5190dc86653`.
Onze módulos em IRX, incluindo IOPRP254, PADMAN, LIBSD e RADSOUND, foram
inventariados com hashes em `out/game-first/simpsons-irx-manifest.json`, ignorado.

O harness local executou o ELF com o kernel Sony carregado pela BIOS parcial,
sem assumir IDs do encoder nem substituir syscalls por serviços do host.
**É diagnóstico de injeção após firmware bloqueado, não ExecPS2/boot normal.**
Interpretador e híbrido chegaram a **627.894 instruções**, `PC 0x002DEAF4`, com
RAM SHA-256 idêntica `e4157f8f9399112583e9160dd4d6d1015fffce94eb7e64bdf98b687fba705925`.
O startup AOT cobre 23 instruções estáticas/4 blocos; aposentou 214.944 instruções
na execução híbrida. A multiplicação Booth geral continua recusada nesse PC.
Zero pixels GS, zero frames reais, nenhum menu, controle ou gameplay demonstrado.
Logs, harness, traduções e snapshots permanecem locais em `out/game-first/`.

Revisão estudada do PS2Recomp: `e5636981a1c9de368f6b71e819c3dc188dfedbf4`.

| Arquivos consultados, somente como referência | Comparação e fundamento independente |
|---|---|
| `ps2xRecomp/src/lib/mmi_translator.cpp`, `ps2xRuntime/include/ps2_runtime_macros.h` | PSUBB por bytes, SA e movimentos/packing HI/LO; implementação portátil própria na semântica compartilhada, conferida no EE Instruction Set. |
| `ps2xRecomp/src/lib/fpu_translator.cpp`, macros FPU no mesmo header | A referência usa operações/conversões do host e não resolve integralmente o arredondamento EE. MOV/conversões usam bits inteiros; DIV usa circuito SRT próprio. MUL aceita apenas o subconjunto esparso verificado; sem aproximação geral silenciosa. |
| `ps2xRuntime/src/lib/Kernel/Syscalls/{Dispatcher,System}.cpp` | A rota de serviços do host não substitui a BIOS original no produto; syscalls continuam entrando no handler do convidado. |

Fontes independentes: [EE Core](https://docs.alexrp.com/mips/ee.pdf),
[EE Instruction Set](https://docs.alexrp.com/mips/ee_insns.pdf),
[Prabhu/Zyner, ARITH 1995, tabelas e circuito SRT](https://www.acsel-lab.com/arithmetic/arith12/papers/ARITH12_Prabhu.pdf),
[resultados medidos de FPU](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/cpu/ee_fpu/muldiv.expected).
Nenhum código GPL, firmware, módulo ou código traduzido comercial foi integrado.

### Reensaio de Simpsons — 04/11/2026

A pedido do usuário, The Simpsons: Hit & Run foi repetido no núcleo
`3681ddc`, após as capturas reais de Super Bust-A-Move. A ISO SLUS-20624
foi montada como PS2 DVD, sem reutilizar a mídia ou o AOT do outro título.
ELF e startup AOT conferidos pelo SHA-256 registrado acima. Firmware e
hipóteses explícitas são os do ensaio 101; injeção após checkpoint
`0x82000` / 32.416.676 instruções, não boot original do disco.

Híbrido e interpretador puro pararam em **849.776 instruções**,
EE **`0x001681B0`**, opcode `0x460CA302` (`MUL.S`, no delay slot de JAL).
Operandos crus: F20=`0x3F800000`, F12=`0x48B33333`. A recusa é
`MUL.S ambiguous low-column Booth rounding not modeled`.
IOP ativo em `0x2F28` / 4.158.306 instruções, sem recusa.
Zero bytes SIF1/SIF0, zero pacotes GIF, zero escritas de pixels;
scanout recusa PMODE ainda não escrito. **Nenhum frame do Simpsons.**

RAM idêntica nos dois motores, sem doublewords desconhecidos:
`c9059fda7e7b29936039341b31c8ee4b02b3e506f8f8a7a0c0193d7b791dadc7`.
Híbrido: 214.944 instruções AOT e 634.832 interpretadas, 0,385 s na fase
do jogo; interpretador: 849.776, 0,592 s. Medição única, sem reivindicar
benchmark geral. Nenhuma mudança no núcleo foi necessária para o ensaio.
Logs locais `out/game-first/simpsons-trial-1.log` e
`simpsons-trial-interpreter.log`; harness `simpsons-trial-probe.cpp`,
build `out/gs-iop-build/simpsons-trial.exe`. Artefatos proprietários
permanecem ignorados; capturas do Super Bust-A-Move preservadas.

### Continuação do Simpsons — FPU, DVD e FIFO MECHACON

As execuções 2–5 passaram por três fronteiras compartilhadas:

- MUL.S com multiplicando potência de dois e primeiro dígito radix-4
  negativo (`mantissa_b & 2`). O produto inteiro exato está numa fronteira
  de mantissa; perda positiva menor que um ULP seleciona o valor anterior.
  Verificação externa exaustiva das 4.194.304 mantissas dessa classe:
  zero diferenças. Comparação executada em processo independente, sem
  vincular ou copiar GPL para o núcleo BSD. É evidência do modelo derivado
  de hardware, não nova medição física. Classes mais amplas foram rejeitadas
  por contraexemplos e continuam recusadas. O ensaio 2 chegou a `0x16820C`
  / 873.443 instruções; o 3 passou ambos MUL.S e chegou a 22.422.127.
- NCMD 06 com formato de dados de 2048 bytes também aceita DVD PS2.
  Banda e geometria de busca seguem o meio físico; NCMD 08 de 2064 bytes
  continua recusado. Timing funcional, CAV nominal ainda explicitamente
  experimental. Teste próprio verifica bytes reais, espera antes do setor
  e IRQ somente após entrega DMA. O jogo leu setores reais da ISO,
  incluindo 403–420, e chegou a 93.116.950 instruções.
- Leitura de descarte da FIFO S vazia transporta byte desconhecido no IOP.
  Não inventa resposta nem latch. SSTATUS continua vazio; branch sobre o
  byte opaco recusa. Interface numérica EE mantém recusa explícita.
  Testes próprios cobrem transporte por RAM, estado vazio e consumo proibido.

Último ensaio 5: EE `0x80005F00` / **93.117.072 instruções**, IOP
`0x25E34` / **15.691.717**, bloqueado no **SCMD 08 ReadRTC**, ainda não
modelado. SIF1 576 bytes, SIF0 448; zero pacotes GIF/pixels/frames.
RAM `e375f45a1fd49b0752effcf2ef435ff3b57b328d408c36d2072ce6e8b6ea26d8`.
Logs locais `simpsons-trial-{2,3,4,5}.log`. Nenhum processo do jogo ficou ativo.

Repetição `simpsons-trial-5-interpreter.log`: mesmos PC, contagens, causa
e hash de RAM do híbrido; 93.117.072 instruções interpretadas. Amostras
únicas: híbrido 34,762 s, interpretador 33,818 s; nenhum ganho demonstrado.
A primeira CI de `6328bb7` revelou três fixtures MECHACON que exigiam
recusa já na leitura vazia. Elas agora exercitam um branch sobre o byte
opaco, preservando a prova de que ele não pode fabricar decisão numérica.
Os três testes corrigidos e `native-ee-context` passaram localmente.
CI final: núcleo de `79bb6d6`, execução `41850568279`, aprovado nos seis
alvos; launcher do código `6328bb7`, execução `41849651243`, aprovado nos
seis. A correção posterior altera somente fixtures/documentação.

Consulta prévia: PS2Recomp revisão `e5636981`, `fpu_translator.cpp` e
`ps2xIOP/src/emulator/imports/iop_cdvd.cpp`; serviços HLE não adotados.
Fundamentos/comparação: [pesquisa FPU](https://github.com/libretro/ps2#float-accuracy-the-ps2float-work),
[CDVD](https://psi-rockin.github.io/ps2tek/index.html#cdvdncommands) e
[definições SDK](https://github.com/ps2dev/ps2sdk/blob/master/common/include/libcdvd-common.h).
Nenhum código dessas referências foi integrado. Quatro testes focados
`native-ee-context`, `native-iop-knowledge`, `native-cdvd-dma` e
`native-cdvd-bus` passaram. Regressão original após as três mudanças:
17.690.079 / `0x80007D20`, 4.486.087 / `0x80005F00`, IOP 2.210.789 /
`0x40B0`, RAM canônica `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.

## 04/11/2026 — Super Bust-A-Move (SLUS-20115)

### Entrada digital durante a execução

`set_sio2_digital_pad_buttons` atualiza os botões ativos em nível baixo
de um controle digital explicitamente conectado. Não conecta periférico
implicitamente nem muda timing/configuração. A resposta de um pacote já
iniciado mantém a amostra anterior; apertar/soltar chega aos polls seguintes.
Reset remove a conexão. Teste próprio exercita três polls, Cross solto,
pressionado e solto, incluindo alterações durante pacotes em trânsito.
Os três testes focados SIO2 pad/DMA/idle passaram.

Consulta prévia de comportamento: PS2Recomp `ps2xRuntime/src/lib/ps2_pad.cpp`,
revisão `e5636981`, entrada host em bits ativos baixos; o caminho HLE não
foi adotado. Protocolo independente em
[psx-spx](https://psx-spx.consoledev.net/controllersandmemorycards/).
Implementação BSD original, sem código da referência incorporado.
O ensaio supervisionado usa `out/game-first/superbust-menu-probe.cpp`,
arquivo local de entrada `superbust-pad.txt` e log `superbust-menu-102.log`.
Progresso de menu/controle só será registrado após observação real do jogo.

### Continuação supervisionada — ensaios 102 e 103

O ensaio 102 repetiu os pixels do aviso e exercitou Cross, Start e Circle,
com press/release pelo SIO2; terminou voluntariamente aos 20 minutos,
2.540.000.000 instruções, EE `0x81FC8`, IOP `0xAE98` ativo. O aviso
persistiu nesse intervalo. Isso não prova que todos esses botões já eram
aceitos pelo estado do jogo. Capturas locais `superbust-menu-vblank-*`.

Para observar mais tempo emulado, o diagnóstico 103 compilou offline
somente os 32 bytes do laço de espera do mesmo kernel, com guardas por
instrução e conferência de ROM0/perfil e bytes atuais da RAM. Nenhuma
instrução/ciclo foi pulado. Tradução e ELF privados permanecem ignorados.
Hash dos 32 bytes `edfd55ab427b1e8c971f6e4165caa558a1d542e69789f27a0bab0316ff5c9f97`;
ELF local `07e1ecacc4412ad47000847438838e8279266f744fb26816a44dce46054b3470`.
As entradas do 102 foram repetidas nos mesmos ciclos. Em 2.540.000.000,
PCs EE/IOP e RAM coincidiram: `57a2ca102852f2044ebd7b2c33c593ec414478b21f2cdf80d26e3048e144dadf`.
Tempo desse trecho: 852,579 s contra 1.204,92 s, redução de 29,2% numa
comparação única do diagnóstico, não benchmark geral do produto.

Uma inspeção somente de leitura do processo supervisionado confirmou
Cross nos buffers reais do EE (`0x1E8C37`, `0x582A03`, `0x582A83`):
byte de botões alto `FF` para `BF`. A amostra concorrente não é save-state
coerente. Não houve escrita host em memória convidada ou alteração de flags.

O fluxo saiu do redesenho contínuo do aviso e, em **3.781.852.170**
instruções, chegou ao GS com sprite UV/PSMT4/DECAL e `TEST=0x5000F`.
Recusa seguinte: teste de alpha não modelado, EE `0x1CE454`;
IOP ativo `0xC7A4` / 476.783.608. Scanout preto da transição; ainda
sem menu demonstrado. RAM `8eea2723acf93a4195dd2e3aae8f4cdc7e94c5072b545e8b06288eab57a0ca85`.
Log `superbust-menu-103.log`, capturas `superbust-menu103-vblank-*`.

Implementadas as oito comparações ATE e as quatro ações AFAIL sobre
color/depth, com RGB_ONLY preservando alpha e FBMSK; comparação usa
alpha após textura e antes de blending. KEEP não consome profundidade
desconhecida. DATE continua recusado explicitamente. Consulta prévia:
PS2Recomp `gs_cpu_backend.cpp` na revisão `e5636981`, comportamento da
classificação do teste e máscaras; nenhuma implementação GPL incorporada.
Fixtures próprios cobrem tabela de verdade em AREF-1/AREF/AREF+1,
AFAIL, profundidade, máscaras e alpha DECAL diferente do alpha de vértice.
Oito testes gráficos/DMA/Direct ELF passaram. Um teste Direct ELF foi
alinhado ao motivo específico atual de recusa MSCAL sem motor VU.
Reexecução seguinte: harness `superbust-menu104-probe.cpp`, mesma sequência
de entrada do 103; nenhum resultado de menu antecipado.

### Abertura real e próxima fronteira FPU — ensaio 104

ATE/AFAIL passaram pelo PC anterior. Captura 14, ciclo 3.824.089.587:
tela original de créditos/direitos autorais, 640x224, 18.535 pixels não
pretos, RGBA SHA-256 `3003e21b2d66029b543aa6940961f6d5ac4e18a04fbc9fd95622211963bc715a`.
Captura 18, ciclo 5.703.243.087: logotipo Acclaim, 13.493 pixels não
pretos, SHA-256 `9665f363b8c18476eff3572263ad4c6405fcabe5e1bb8b96dcdee79eacbd43c0`.
Pixels vêm dos comandos do jogo; BMP/PNG locais `superbust-menu104-vblank-*`.
Start foi exercitado com press/release. Não há menu principal comprovado.

O harness recebeu extensão apenas do limite host de 8 para 16 bilhões,
mantendo 60 minutos, sem escrever em RAM/CPU/código convidado. Evento
local em `superbust-menu104-host-limit.log`, fonte local ajustada ao limite;
não é alteração do núcleo. A rodada terminou por recusa após 2.754,49 s:
**7.176.168.746** instruções, EE **`0x1CD584`**, opcode `0x46006034`,
**C.LT.S** ausente. IOP ativo `0xAE94` / 901.073.180. RAM
`bc5c550b8b49b93244f910759888d7e960e30304095b03fc3c9d7f75a5dc3e11`.
Em 2,54 bilhões, conservou hash/PCs anteriores (1.008,39 s). A carga host
não foi isolada nas amostras 102–104; os 29,2% observados no 103 não
constituem benchmark controlado.

Implementadas C.F/EQ/LT/LE e BC1F/T/FL/TL na semântica compartilhada
AOT/interpretador. Ordenação inteira: denormais/zeros com sinal são zero,
expoente 255 é finito. Só FCC/FCR31.23 muda; flags e FPRs preservados.
Campos reservados e CU1 desligado recusam; branches mantêm delay slot,
annul e metadados CFG. Consulta prévia: PS2Recomp `fpu_translator.cpp` e
`ps2_runtime_macros.h`, revisão `e5636981`; comparações IEEE do host não
adotadas. Fundamentos: [EE Instruction Set, pp.349–352](https://docs.alexrp.com/mips/ee_insns.pdf)
e [resultados físicos](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/cpu/ee_fpu/compare.expected).
Sem código das referências incorporado. Sete testes focados passaram,
incluindo `native-m30-direct-fpu` com trace/estado iguais nos dois motores
e oito combinações de condição/branch normal e likely.

Próxima rodada: `superbust-menu105-probe.cpp`, 90 minutos/16 bilhões,
supervisionada. Novo cenário, não replay: `10003` no arquivo de entrada
mantém Cross 60 campos após a primeira imagem não preta, solta 30 e
depois pulsa Start 30 campos/solta 90. `FFFF` desliga o script e solta;
entradas manuais continuam disponíveis. Relatórios em chunks de 200
milhões reduzem cópias de diagnóstico; clocks/instruções continuam
executados, com progresso a cada 30 VBLANKs. Objetivo: menu e navegação
visível, ainda não demonstrados.
Após ATE/AFAIL, a regressão do modo original manteve 17.690.079 /
`0x80007D20`, 4.486.087 / `0x80005F00`, IOP 2.210.789 / `0x40B0` e
RAM `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.

### Certificação adicional do multiplicador — continuação do menu

A rotina isolada do bloqueio 104, com FPR/VF/GPR capturados e coeficientes
do ELF original, passou C.LT.S e encontrou VMULbc em `0x1CD530`.
Isso é replay matemático parcial, sem restauração de periféricos/IRQ;
não é avanço comprovado da execução completa. O primeiro auxiliar ficou
incompatível com cabeçalhos VU alterados em paralelo; o rebuild coerente
resolveu a falha host, sem correção de jogo ou alteração convidada.

Refinado o intervalo conservador de MUL: a soma dos pesos iniciais abaixo
da coluna 15 limita a perda de TODOS os cortes. Compressores conservam
peso; carries para coluna 15 e cortes apenas retiram peso dessas colunas.
Só oito dígitos radix-4 as alcançam. Para linha negativa, complemento e
correção separados pesam `2^15-resíduo`, inclusive `2^15` no resíduo zero.
Aceita apenas quando os extremos do intervalo truncam ao mesmo resultado;
árvore e casos restantes continuam explicitamente não modelados. O limite
mais barato anterior permanece primeiro. Sem multiplicação float do host.
Consulta PS2Recomp FPU/macros da revisão acima mantida como referência de
comportamento, sem copiar/vincular sua implementação GPL.

Quatro milhões de pares sintéticos reproduzíveis, com sinais/expoentes
aleatórios e casos próximos de fronteiras, foram comparados em processo
separado ao modelo externo de circuito: **3.953.261 aceitos, 46.739
recusados, zero divergências**; flags intactas nas recusas. Regressões
próprias cobrem a nova certificação, os três produtos da rotina e a fronteira
que continua ambígua. Sete testes focados passaram. A rotina isolada agora
retorna em `0x1CD5C4` após 41 instruções, incluindo VSQRT exata/VWAITQ.
Não se apresenta isso como menu ou frame.

Microbenchmark do caminho já aceito, mesmos 1 milhão de pares/10 repetições
por amostra, cinco amostras: medianas 7,882 ns/produto antes e 8,147 ns
depois (3,4% mais lento observado). Carga host concorrente não isolada;
nenhum ganho de desempenho declarado. Regressão BIOS original repetida:
mesmos PCs/contagens e RAM canônica `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
Rodada 105 terminou com **7.176.168.811** instruções: recusa real de
VMULbc `0x4BC64218` em **`0x1CD530`** (PC de estado já avançado para
`0x1CD534`). Operandos coincidem com o replay isolado. IOP `0xAE94` /
901.073.188 ativo; 8.169 pacotes novos, 379.580.400 escritas de pixels.
RAM `5a8d8bb59d84bfcdbc98203fe3b459b11f39b8af0bc583d3953b4e1b2b5cb47e`,
um doubleword parcialmente desconhecido. 3.290,49 s, com compilação e
outra execução concorrentes; sem comparação de desempenho. Créditos e
Acclaim reais novamente capturados, nenhum menu. CI `692dd25`: núcleo
e launcher aprovados nos seis alvos.

Rodada 106, já com o limite refinado, encerrada voluntariamente para
incorporar a cobertura SQRT seguinte: EE `0x81FC0` / 1.098.589.884,
IOP `0xAE98` / 141.375.822 ativo, 670,33 s. Sem recusa, 213 novos pacotes,
23.466.480 escritas; RAM `a9b112b76b9796610adfce22b0249df5ca3cb291ea5e2b5096c07bf61f8498b2`.
Isso não comprovou ainda a passagem pelo VMULbc da rodada 105.

### Raiz SRT e nova rodada supervisionada — ensaio 107

Teste isolado com entrada sintética 0,01 da mesma rotina encontrou VSQRT
não exata em `0x1CD558`; não se afirma esse PC como bloqueio de uma rodada
completa. Implementação própria pela recorrência da eq.(2), seleção da
Tabela III e datapath das Figs.1/5 de
[Prabhu/Zyner, ARITH 1995](https://www.acsel-lab.com/arithmetic/arith12/papers/ARITH12_Prabhu.pdf).
Resto redundante, 25 dígitos radix-2, divisor dependente da raiz parcial;
sem correção IEEE ou sqrt do host. O dígito zero seleciona a partir das
linhas anteriores, mas o compressor principal avança; tratá-lo como bypass
altera o último bit. Atalho de raiz exatamente representável preservado.
Entradas negativas continuam recusadas até modelar suas flags de exceção;
denormais/zeros mantêm a convenção existente. Q conserva sete ciclos,
STATUS/stickies/MAC, espera e atualização compartilhadas.

Consulta prévia PS2Recomp FPU/macros `e5636981`: a rota de sqrt do host
não foi adotada. Vetores numéricos físicos
[EE SQRT](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/cpu/ee_fpu/sqrt.expected)
e [timing VU/Q](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/vu/lower/fdivdelay.expected)
conferidos; sem código de teste dessas referências incorporado. Oracle de
circuito compilado como processo externo separado, sem objetos GPL no BSD.
Um milhão de entradas sintéticas aleatórias: 501.602 aceitas, 498.398
negativas recusadas, zero divergências nos resultados aceitos da função
real do núcleo. Sete testes focados passaram, incluindo fronteiras do bit
baixo e Q pendente, sem trocar a recusa por flags fabricadas.
Microbenchmark de oito raízes já aceitas, quatro milhões de chamadas por
amostra/cinco amostras: mediana 41,787 ns antes e 44,113 ns depois (5,6%
mais lento observado). Entradas/repetições equivalentes, carga host não
isolada; a mudança amplia correção/cobertura, sem alegar ganho ou FPS.

A rotina isolada com 0,01 agora retorna em 41 instruções. Varredura local
de 158 entradas sintéticas: 142 retornam, 16 ainda recusam produtos Booth
ambíguos; não é gameplay nem prova de que esses valores ocorrem no jogo.
BIOS original novamente conserva PCs/contagens/hash canônicos. Rodada
completa 107 iniciada após build coerente: 90 minutos/16 bilhões, controles
em `superbust-pad107.txt`, mesmo cenário de press/release do 105. Processo
supervisionado; continuar até menu/navegação ou próximo bloqueio real.

### Aritmética VU com incerteza preservada

Novos helpers próprios conservam um intervalo de produtos Booth em vez de
escolher um último bit não provado. A representação `ObservedWord` guarda
os bits comuns e uma máscara que cobre TODO o intervalo, incluindo bits
intermediários que coincidem nos extremos. Sinais/expoentes desconhecidos,
fronteiras de exceção ou cancelamento não certificado continuam recusados.
Só frações com sinal/expoente/flags conhecidos podem seguir simbolicamente.
ADD usa alinhamento inteiro monotônico com os expoentes fixos; produtos
com entradas incertas usam um limite global de perda, sem supor que o
arredondamento Booth seja monotônico. Nenhuma aproximação float do host.

SQRT enumera até oito bits de fração incertos e preserva o conjunto de
resultados possível. Domínios maiores ou inválidos recusam com trabalho
limitado. Q pendente mantém sua máscara, latência de sete ciclos e flags;
leitura que exige valor conhecido ainda falha. VMUL/ACC/VADD/VADDq podem
eliminar a incerteza somente quando provam que os bits não afetam a saída.
Estados opacos continuam recusados, e o commit de lanes/flags é atômico.
O caminho escalar estrito de MUL continua recusando produtos ambíguos.
PS2Recomp FPU/macros foi consultado previamente; sua operação de host não
oferece esse contrato simbólico e não foi usada como base de código.

Validação externa separada, quatro milhões de produtos conhecidos: zero
falhas de contenção, 39.185 saídas com fração explicitamente incerta.
Outras quatro milhões de amostras com bits de entrada desconhecidos:
81.440 domínios recusados, zero falhas de contenção nos aceitos; 3.993.948
somas aceitas, zero divergências de contenção/flags. Amostras não são prova
exhaustiva dos conjuntos; os limites têm fundamento algébrico descrito acima.
Oito testes focados e seis regressões VU/VIF/PATH1/frame passaram. BIOS
original preservou PCs, contagens e RAM canônicos. Replay da rotina com
158 entradas sintéticas retornaram em todas. Correção da evidência: o
auxiliar verificava VF05.x, um registrador temporário zerado; a saída
relevante é VF04.x/y. A checagem de VF05 não certificava o resultado
trigonométrico. Não comprova execução completa, pixels ou controle.

Microbenchmark de VMUL de quatro lanes já conhecido, mesmas 100 mil
entradas/10 repetições/cinco amostras, incluiu gravação das fontes e leitura
do destino/MAC. Medianas 154,024 ns antes e 153,383 ns depois, checksums
iguais; variação observada de -0,4%, sem ganho relevante demonstrado e com
carga host concorrente. Fontes BSD de cada versão compiladas com ABI
coerente. Próxima rodada preparada em `superbust-menu108-*`, ainda sem
afirmar passagem por um novo bloqueio completo. O 107 continua supervisionado.

### Nova fronteira matricial e build fixo — ensaios 107 e 108

107 passou pelo VMULbc anterior e terminou em **7.176.168.943** instruções:
VMADDA `0x4BE830BE` recusada em **`0x1CD1DC`**; PC de estado já em
`0x1CD1E0`. IOP `0xAE98` / 901.073.205 ativo. 8.169 pacotes novos,
379.580.400 escritas de pixels; 3.121,47 s, carga host não isolada.
Backing de RAM `5a8d8bb59d84bfcdbc98203fe3b459b11f39b8af0bc583d3953b4e1b2b5cb47e`,
um doubleword parcialmente desconhecido. Não inclui um save-state de
D-cache/dispositivos. Só aviso, créditos e Acclaim comprovados, sem menu.

Replay isolado das quatro instruções matriciais, reconstruindo ACC a partir
dos vetores capturados, passou com a propagação simbólica. VF09 X/Y ficam
conhecidos (`C4800000`, `C4800000`); Z/W mantêm um bit incerto cada
(`ABCDABCC/00000001`). Nada converteu essa incerteza em valor conhecido.
Isso não é replay do sistema nem prova de frame.

Alterações paralelas de layout de EE/IOP tornaram novamente um auxiliar
incompatível com sua biblioteca; a falha host foi isolada da recusa do jogo.
Para evitar nova mistura de ABI, somente arquivos rastreados da revisão
**`6524aa8db4d8274a52beccdc54ae0f517b09007c`** da main foram exportados
para `out/gs-iop-frozen-source`, com build em `out/gs-iop-frozen-build`.
Código continua na main; essa cópia local é apenas fonte fixa de compilação.
Não contém jogo, firmware, NVM ou traces. Nenhum cache de firmware compartilhado.
Núcleo, cabeçalhos, auxiliares e AOT do jogo foram recompilados juntos.
14 testes pertinentes passaram, incluindo metadados COP1 e VU/VIF/PATH1.
BIOS original manteve PCs/contagens e SHA-256 canônico.

108 iniciada com essa revisão fixa, mesmas identidades/hipóteses e roteiro
de botões do 105, 90 minutos/16 bilhões. Binário SHA-256
`6beb4bef55c411230817b428db4d356c808ef92825a20efc33bdb9c648ac452a`;
manifesto local `superbust-menu108-build-provenance.json` registra hashes
de núcleo, cabeçalhos, probe e AOT. Entrada `superbust-pad108.txt`;
`superbust-capture108.txt=1` solicita cada VBLANK distinto, `0` mantém a
cadência econômica. Isso altera apenas a captura host. Dumps finais também
registram máscaras de RAM e FPR, sem alegar save-state restaurável.
Processo acompanhado; continuar até menu e resposta visível aos controles.
CI `2eb9955`: núcleo e launcher aprovados nos seis alvos.

### Limites numéricos certificados na VU — ensaio 108 e preparação do 109

108 terminou em **7.181.062.182** instruções: VSQRT `0x4B8703BD`,
**`0x1CD558`**, estado já em `0x1CD55C`. IOP `0xB464` /
901.684.861 ativo, sem recusa. 8.177 pacotes novos e 379.723.760 escritas
GS; 3.360,07 s com carga host concorrente. Backing RAM
`01920ee46f83175ddf0b4d9fb8ace361bc7922a3b8c5df6819be8e9bcd06871a`.
VF07.w tinha máscara de vinte bits (`3AE00000/000FFFFF`), excedendo o
limite opaco da SQRT. Só aviso, copyright e Acclaim foram observados.
As duas variantes entrelaçadas do Acclaim não são telas distintas do menu.

A VU agora conserva os intervalos numéricos produzidos pelos helpers
aritméticos, além da máscara de bits. VMUL/VADD/VSUB/ACC e broadcasts Q
consomem esses limites certificados; rotação mantém a associação à lane.
Commit continua atômico. QMTC2/LQC2/escritas brutas e resultados micro sem
certificado invalidam a prova anterior, mesmo com valor/máscara idênticos.
O intervalo de Q só aparece na conclusão real dos sete ciclos. Nenhum
bit desconhecido é escolhido. SQRT pode enumerar até 65.536 encodings de
um intervalo certificado; entradas opacas mantêm o teto de oito bits.
Não se presume monotonicidade do circuito SRT. Trabalho excessivo e
sinais/expoentes/flags sem prova continuam recusados.

Replay isolado desde `0x1CD508`, reconstruindo QMTC2 a partir do FPR12
capturado `3FC3B35F` e executando os coeficientes/instruções originais:
a revisão fixa do 108 reproduz a recusa em `0x1CD558`; com limites
preservados a rotina retorna em `0x1CD5C4`, 27 instruções. Saída real
**VF04.x/y `BD2B82CC/3F7FC686`, máscaras zero**. O resultado da raiz
já é conhecido antes de precisar do novo teto de enumeração, pois os
limites impedem a inflação anterior. Isso é prova local da rotina,
sem periféricos/IRQ, e ainda não comprova menu ou resposta aos controles.
PS2Recomp FPU/macros consultado como referência comportamental; nenhum
código GPL copiado, derivado ou vinculado.

Microbenchmark do caminho conhecido, 100 mil pares, dez repetições e cinco
amostras: mediana 154,056 ns anterior e 167,321 ns com certificados,
checksums iguais; custo observado de 8,6%, sem ganho de desempenho.
A versão inicial custava cerca de 17%; evitar cópias de lanes não escritas
reduziu esse custo. Máquina compartilhada, sem isolamento da carga host.
Testes focados cobrem contenção de SQRT, domínios incompatíveis/excessivos,
invalidação de prova em substituição bruta/micro, rotação, ACC e Q.
Rodada 109 concluída abaixo; conservar BIOS original e hipóteses
experimentais explícitas. Continuar até observar menu e navegação reais.

### Extremo exato do produto VU — ensaio 109 e continuação 110

109 terminou em **7.210.587.806** instruções, VMADDA `0x4BE830BE`
em **`0x1CD1DC`** (estado já em `0x1CD1E0`). Superou a SQRT do 108.
IOP ativo `0xC750` / 905.375.574, sem recusa; 8.228 pacotes novos,
380.592.128 escritas GS, 3.160,11 s em host compartilhado. Backing de RAM
`9f4f63b626875709d8ae69486be947b06e704b517a6a911c9866c8e0b5618060`,
com máscaras registradas separadamente. As capturas ainda mostram aviso,
copyright e Acclaim; nenhuma imagem demonstra menu ou navegação.

VF08.z/w continham `AB800000/007FFFFF`, multiplicados pela componente
unitária de VF06. A margem uniforme de perda Booth fazia o extremo
inferior atravessar artificialmente o expoente. O helper compartilhado
agora usa perda zero no limite inferior quando o menor multiplicador
certificado tem dezesseis bits baixos zerados. Esse extremo é exato;
qualquer multiplicador maior aumenta o produto inteiro em pelo menos
`2^23`, mais que a perda total limitada por `2^18`. A prova é direcional,
não presume monotonicidade do circuito nem escolhe a fração real.
Sinais, expoentes e flags continuam exigindo prova.

Replay isolado das mesmas quatro instruções, com cabeçalhos e núcleo
compatíveis: o helper anterior reproduz a recusa; o corrigido retorna em
`0x1CD1E4`. VF09.x/y conhecidos `C49F87FA/C42B169F`; z/w mantêm
`AB800000/007FFFFF`. Esse replay não restaura periféricos, cache ou IRQ e
não é prova de um frame. Treze testes focados passaram, incluindo
domínios de multiplicador incerto nos extremos de expoente, VU/VIF,
GS, SIO2 e AOT/interpretador. BIOS original mantém PCs/contagens e RAM
SHA-256 `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
Consultados [PS2Recomp VU](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/vu_translator.cpp)
e [manual VU](https://docs.alexrp.com/mips/ee_vu.pdf), sem incorporar código GPL.

**110 iniciada**, fonte fixa de `0454091` mais a correção acima em
`out/gs-iop-bounds-source`; núcleo, cabeçalhos, AOT e harness recompilados
juntos. Binário SHA-256
`18ebfc018df39974dd4f28854fee0eb1e44d9648a2d94adfb039e39eda32df5c`.
Manifesto local `superbust-menu110-build-provenance.json`; log
`superbust-menu-110.log`, entrada `superbust-pad110.txt`, captura
`superbust-capture110.txt`. Cauda por instrução desativada antes de sete
bilhões e lista final repetitiva de GIF substituída por contagem; nenhum
efeito convidado ou ciclo é omitido. Sem ganho de desempenho afirmado.
Limites supervisionados de 90 minutos/16 bilhões; mesmas identidades e
hipóteses experimentais do 109. Resultado abaixo.

### Triângulos UV no GS — ensaio 110 e continuação 111

110 superou as recusas numéricas dos ensaios 108 e 109. Terminou em
**7.284.393.384** instruções do jogo, EE **`0x1CE45C`**, por
`GS fractional triangle attribute DDA precision is not established`.
O EE consultava o DMA VIF1; a recusa veio do processamento gráfico real.
IOP **ativo `0xAE98` / 914.601.294**, sem recusa. 8.405 pacotes GIF novos,
382.818.208 escritas GS, SIF1/SIF0 2.821.232/1.794.256 bytes.
298 ciclos de stall modelados, 7.316.810.358 ciclos EE totais;
2.991,59 s em host compartilhado, sem comparação equivalente de desempenho.
Backing RAM `40f4f6300808fec2ab552241c4a817b9a51dedd59ca905d5cf796282a53490de`,
três doublewords com incerteza registrada. Isso não inclui cache/dispositivos
e não constitui um save-state restaurável.

As capturas confirmaram apenas aviso de Memory Card, créditos e Acclaim.
O pulso Start foi liberado no mesmo VBLANK 1134 do 109, com os mesmos
hashes de campo. O scanout final era **preto**, 640x224, hash RGBA
`6cf79e2ea48074d85c3879b2e34cd92246b816be78aa13de39d9743ab0f880e9`.
Nem a passagem das recusas VU nem um hash alternado do Acclaim comprovam menu.

O pacote pendente usa um TriangleStrip texturizado UV com vértices
fracionários. Consultados [PS2Recomp GS CPU](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp)
e [GS User's Manual v6](https://www.scribd.com/document/784545197/GS-Users-Manual),
seções 3.1 e UV. Eles descrevem a interpolação e o formato 10.4, mas não
estabelecem toda a precisão física do setup/DDA. Nenhum código GPL foi
copiado, adaptado ou vinculado.

A API diagnóstica compartilhada acrescentou o bit **4**, exclusivamente
para UV de triângulos: barycentrics racionais inteiros, truncados por piso
ao formato 10.4 e enviados ao sampler real. **Hipótese funcional, não
conformidade física demonstrada.** O modo de referência continua recusando
gradientes/amostras fracionários. A opção não altera cor, profundidade,
UV de sprites, filtros, geometria, lógica convidada ou dados de textura.
Testes verificam o opt-in, amostras da textura efetivamente carregada,
ausência de escritas na recusa padrão e manutenção das recusas de cor,
profundidade, sprites e bilinear. Seis testes focados passaram, incluindo
análise/diagnóstico, GS e frames. BIOS original preservou PCs/contagens e
RAM canônica `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.

**111 iniciada com as novas ferramentas.** `ps2n-debug log` identificou a
primeira recusa explícita na linha 357 do log 110. `ps2n-analyze` analisou
o ELF e o trecho `0x1CE450`; `ps2n-dev init` registrou o perfil local.
O harness usa o novo `DiagnosticRecorder` do núcleo, 512 eventos sem
streaming contínuo, ativado a partir de 7,28 bilhões. Relatórios JSON,
estado, eventos e frame final vão para `superbust-menu111-diagnostic` e
são lidos por `ps2n-debug report`. Históricos limitados não são contagens
cumulativas de pacotes; o prefixo e o relatório indicam a cobertura.
O runner experimental continua necessário para a injeção de ELF no kernel
parcial; o `replay` das ferramentas reexecuta desde o reset e não restaura RAM.

Núcleo, cabeçalhos, AOT e harness foram recompilados juntos. O manifesto
local `superbust-menu111-build-provenance.json` registra Git, alterações
locais das novas ferramentas e hashes das fontes/binários; o hash de fonte
foi preservado durante a compilação. Log `superbust-menu-111.log`, entrada
`superbust-pad111.txt`, captura `superbust-capture111.txt`. AOT do ELF e idle
verificado permanecem ativos após o prefixo inicial, com guarda por
instrução e fallback explícito. A hipótese UV é selecionada antes da
atividade convidada. Limites supervisionados de 90 minutos/16 bilhões,
mesmas identidades de firmware/disco e demais hipóteses do 110. Continuar
até comprovar menu, Start/Cross/Up/Down e início de partida.

BIN Mode2/2352: SHA-256 `232c139dfdd8b6fd9b201ac4c11f9e03b204732ee85adc42fdc06a0350929744`;
ELF `eeb913a6eac79a58d8424dc18190310f1ae5d14d762dec5a339f44ad9870d965`.
Seis arquivos IOP/IOPRP202, com hashes locais em `out/game-first/superbust-irx-manifest.json`.
Diagnóstico com kernel Sony parcial: **4.486.087** instruções, **4.483.572 AOT / 2.515 fallback**,
PC `0x80005F00`, leitura SMFLG recusada porque o IOP parou em `0x40B0` sem timing GS conhecido.
Zero pixels/frames; nenhum menu/controle/gameplay. `SetGsCrt` original também bloqueou no encoder.
Logs locais `superbust-final-{interpreter,hybrid}.log`; não é boot/ExecPS2 concluído.

MUL.S passou `0x002DEAF4` do Simpsons, alcançando **843.734** instruções/PC `0x001681B0`.
O caminho novo certifica intervalos de truncamento de 13 linhas Booth + 13 correções e no máximo
24 compressores, sem supor sua ordem: perda conservadora `<76*2^15`. Se os extremos retêm
mantissas diferentes, recusa sem alterar flags. Oráculo PS2Float executado **separadamente**, sem
ligação com o produto: 830.000 aceitos/1.050.001 pares, nenhuma diferença; árvore geral ainda pendente.
Referência comportamental: [pesquisa do multiplicador](https://github.com/libretro/ps2/blob/master/README.md).

SIF0 agora move payload IOP→FIFO limitada→EE, preserva backpressure/bytes, limpa incerteza somente
nas palavras sobrescritas e entrega IRQ IP3 real; não sintetiza RPC. PS2Recomp `Stubs/SIF.cpp`
foi confrontado com [DMAC/SIF](https://israpps.github.io/ps2tek/) e EE User v6 p.74.
Regressão própria AOT/interpretador: 3.540 instruções, 3.527 AOT, RAM/trace/eventos idênticos.

### Sincronização GS/IOP e reexecução do checkpoint

Consulta prévia ao PS2Recomp na mesma revisão `e5636981a1c9de368f6b71e819c3dc188dfedbf4`:
[`Kernel/EeScheduler.cpp`](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp)
usa eventos de VBLANK com período/duração fixos;
[`Kernel/Stubs/GS.cpp`](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp)
inclui serviços GS do host. Isso não comprova os valores de reset do PCRTC nem a
identificação do encoder deste console. Nenhum código, serviço HLE ou constante
de timing dessas implementações foi incorporado ao núcleo BSD.

**Erro demonstrado e corrigido:** `Iop::run_until` atualizava a geração de timing
somente depois de executar uma instrução. A primeira decisão de interrupção após
uma escrita do EE no GS podia usar o timing antigo. A atualização agora precede
`step()`; as bordas continuam processadas ao fim do ciclo, na granularidade 8:1
já existente. Não se reinicia o IOP nem se remove uma parada registrada.

O teste próprio em `tests/iop_m27.cpp` muda de SYNCV desconhecido para SINT suspenso
imediatamente após habilitar IRQ: antes da correção, falhou na primeira decisão;
depois, executa essa instrução. O caminho inverso recusa antes de aposentar outra
instrução, preservando PC/ciclo. Tornar o timing conhecido depois da recusa não
apaga a parada. Os testes anteriores de bordas NTSC/PAL, polling e IRQ continuam.

| Reexecução local, sem IDs assumidos | Resultado |
|---|---|
| Boot original ROM0/NVM, antes/depois | 17.690.079 instruções, PC `0x80007D20`, encoder `0x7E` não medido |
| Super Bust-A-Move híbrido, antes/depois | 4.486.087 instruções; 4.483.572 AOT / 2.515 interpretadas; PC `0x80005F00` |
| Super Bust-A-Move interpretador, depois | 4.486.087 instruções; mesmo PC e RAM do híbrido |
| IOP preservado | 2.210.789 instruções; PC `0x000040B0`, decisão de IRQ depende de VBLANK desconhecido |
| `SetGsCrt` Sony novamente, diagnóstico separado | 94.121 instruções; PC `0x80007D20`, mesma leitura desconhecida; injeção subsequente mantém o bloqueio SIF |
| Gráficos atribuíveis ao trecho do jogo | Diferença de contadores GIF e pixels antes/depois da injeção: **0 pacotes, 0 pixels** |

SHA-256 da RAM após o jogo, igual ao checkpoint anterior e entre motores:
`cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
ROM0, NVM e ELF foram revalidados e mantêm os hashes registrados anteriormente.
O trace local `out/game-first/gs-iop-boot-tail.trace` cobre o final do boot:
`CSR.RESET` em `0x8000D6C8`, `SMODE1` em `0x8000D54C`, leitura do encoder em
`0x80007D20`; não há escrita de `SYNCV` entre reset e bloqueio. Os logs novos são
`gs-iop-fixed-{hybrid,interpreter,crt}.log`, todos ignorados pelo Git, junto ao harness.

Build isolado `out/gs-iop-build`, Release/MSVC x64. Cinco testes focados aprovados:
`native-m26-iop`, `native-m27-iop`, `native-sif0-dma`, `native-m30-direct-dma` e
`native-m30-direct-sif0`. O build compartilhado foi abandonado após alterações
concorrentes de VIF causarem incompatibilidade de objetos; nenhum teste foi
considerado válido sobre esse build que falhou.

Medição intercalada do mesmo harness híbrido (boot, jogo e hash de RAM), três
amostras por versão: antes 3,020/3,130/3,109 s; após a correção de sincronização
2,959/3,027/2,930 s. Medianas 3,109/2,959 s; amostra curta em host compartilhado,
sem comprovação de ganho sustentado ou FPS. Todas mantiveram PC, contagem e RAM.
Dados locais em `out/game-first/gs-iop-performance.json`.

O **modo original** permanece bloqueado pela evidência ausente do encoder/PCRTC.
Foi repetido após as implementações seguintes: mesmos PCs, contagens e hash
`cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
As hipóteses abaixo são opções explícitas; não se tornaram defaults de console.

### Continuação experimental — subsistemas exercitados pelo jogo

Harness local ignorado `out/game-first/gs-iop-experiment-probe.cpp`, logs numerados
`gs-iop-experiment-N.log`. Mesmo ROM0/NVM/ELF, sem patch de instruções convidadas,
sem resposta RPC fabricada e sem substituição de syscall Sony. O ELF é injetado
pelo desenvolvedor; isso não demonstra boot de disco/ExecPS2 concluído.

Hipóteses identificadas em cada log: encoder `7E=1C/7F=10`, SYNCV após reset
`00C7800601A01801`, omissão experimental da escrita zero F510, latches de
configuração SBUS/ready bits; a partir do ensaio 15, DMACINTEN do IOP inicialmente
1. Nenhum desses valores foi medido neste console. As opções CLI correspondentes
são `--assume-video-encoder`, `--assume-gs-reset-syncv`,
`--diagnostic-omit-f510-zero-write`, `--assume-sbus-configuration-latches` e
`--assume-iop-dmacinten`. A forma exata da opção do encoder consta em `ps2n-boot --help`.

| Ensaio | Evidência / bloqueio seguinte |
|---|---|
| 3–5 | IPU reset/tabelas executados; boot alcança `0x8000EF60`, leitura MPU |
| 6 | MPU `FFFF` medido para GH-032-11/32; próximo PC `0x8000FFD4`, configuração SBUS |
| 7–9 | IOP RAM escrita pelo EE e handshake real; firmware chega a `0x00082000` após 32.416.676 instruções, sob hipóteses explícitas |
| 10 | CDVD NOP/IRQ permite IOP prosseguir; jogo `0x800067F0`, suspensão DMAC |
| 11–13 | CPND, reset PCE documentado e tags REFE/REF; jogo `0x800067F4`, tag SIF1 no IOP |
| 14–15 | IQE SIF1 e experimento DMACINTEN; 48 bytes recebidos; IOP `0x000170F8`, SH no BCR SIF0 |
| 16–18 | BCR parcial; IOP `0x0001712C`, tag `80019870` com seis palavras |
| 19 | SIF0 parcial: 32 bytes chegam ao EE; IRQ real entra no kernel; 4.510.037 instruções, `0x80000224`, PLZCW |
| 20–22 | PLZCW compartilhado AOT/interpretador; comunicação bidirecional continua; espera limitada por serviço RPC `80000596` ausente, sem resposta inventada |
| 23 | 144.172.410 instruções; `0x80006388`, parada de receptor SIF0; 1.552 bytes SIF1 / 1.536 SIF0 |
| 24 | 144.174.538 instruções; `0x80013FC4`, contexto COP0 Debug; IOP ativo em `0x00019180`; 1.664 bytes SIF1 / 1.536 SIF0 |

| 25 | 144.174.824 instruções; DSRA32 do contexto em `0x80014000` (PC reportado `0x80014004`), IOP `0x19368` ativo |
| 26 | 200.000.000 instruções; contexto restaurado, PC `0x80000328`, espera SMFLG após envio real de reset IOP; IOP `0xAE94` ativo |
| 27 | Janela local de 25.000 ciclos IOP confirma conclusão do pacote de reset com DICR mestre desligado; sua reativação não entregava o IRQ pendente |

Até o ensaio 32: **zero pacotes GIF do jogo, zero pixels, nenhum frame/menu/gameplay**.
Os contadores são diferenças antes/depois da execução do ELF, não evidência sintética.
As esperas do jogo foram executadas integralmente; o orçamento subiu de 10 para
40 e depois 200 milhões de instruções, sem abreviar loops por endereço.

Implementações independentes BSD e referências comportamentais:

- [PS2Recomp SIF/DMA](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xTest/src/ps2_sif_dma_tests.cpp): RAM IOP separada e transporte; seus serviços HLE não foram incorporados.
- [PS2Recomp IPU](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/Stubs/IPU.cpp): sequência de inicialização confrontada com registradores; somente FIFO/tabelas, sem alegar decodificação MPEG.
- [EE User v6](https://manuals.plus/m/55bdc71d3aebc33752a7dad6526b78248b6c258294fcf8a3ebed1f010da22365.pdf), pp.62/66/71/74: suspensão, reset PCE=0 e canal. CDE/CPC permanecem simbólicos. Parada sem CPND limitada a receptor sem payload nem FIFO pendente, no modelo atômico por qword; races físicas não verificadas.
- [ps2tek](https://psi-rockin.github.io/ps2tek/): tags, IQE independente da máscara de conclusão, CDVD NOP/NOPsync e IRQ. Throughput SIF por instrução e latência NOP de um ciclo são hipóteses funcionais, não timing medido.
- [Pesquisa SIF com resultados de PS2](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Sif.h): preenchimento de pacote parcial usa as posições correspondentes do último qword completo. O modelo retém a informação dos bytes e recusa posições indefinidas; não lê além do payload nem preenche com zeros inventados.
- [PLZCW no PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/mmi_translator.cpp) confrontado com [EE Instruction Set p.215](https://docs.alexrp.com/mips/ee_insns.pdf). Testes cobrem todos os comprimentos de sinal, zero/ones, alias, campos reservados, metade superior e execução AOT.
- [COP0 no PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/cop0_translator.cpp) confrontado com [EE Core pp.80/84](https://docs.alexrp.com/mips/ee.pdf): configuração indefinida deve permanecer simbólica ao salvar contexto; não adotar fallback zero.

Testes focados exercitam bytes reais, backpressure, limites de RAM, rejeição de
memória indefinida, IQE nos dois sentidos, pacotes de 1/2/3/5/6/7 palavras,
CPND sem IRQ antecipada e equivalência AOT/interpretador. Nenhum firmware, trace,
IRX, jogo ou código traduzido comercial foi incorporado ao Git.

A leitura/salvamento/restauração de BPC/PCCR conserva os campos de reset indefinidos,
com enables documentados em zero. Ativar breakpoints/contadores continua recusado.
Shifts e packing transportam as máscaras; testes cobrem descarte, sinal, alias,
metade superior e contagem de shift desconhecida. Referência prévia adicional:
[PS2Recomp SPECIAL](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/special_translator.cpp).
No ensaio 26 há uma palavra dupla de RAM com bits indefinidos: o hash registrado
pelo harness é explicitamente do backing canônico, não um hash de RAM inteiramente conhecida.

O ensaio 27 localiza uma corrida de IRQ: pacote SIF de 112 bytes com comando de
reset chega ao IOP no ciclo 22.073.734; DICR.23 havia sido limpo no ciclo 22.073.732
e volta a 1 no ciclo 22.073.747. O modelo atual só entregava IRQ na conclusão,
não na reativação do mestre com flag pendente. A correção validada reavalia a
saída nas escritas e registra a borda em I_STAT, sem duplicá-la por simples ACK.
[PS2Recomp IOP](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xIOP/src/emulator/core/iop_memory.cpp)
foi consultado; seu modelo não estabelece esse comportamento de DICR.
[psx-spx DMA](https://psx-spx.consoledev.net/dmachannels/) documenta reavaliação e
borda; ps2tek documenta DICR2/DMACINTEN. A retenção de flag IQE enquanto o mestre
está desligado permanece hipótese funcional isolada nos testes de SIF, sem medição
física; não confundir o teste sintético com prova de hardware.

### ROM1 e continuação após o reset IOP

| Ensaio | Evidência / bloqueio seguinte |
|---|---|
| 28–29 | IRQ pendente entregue ao reativar DICR; IOP prossegue até `0x0001AAF4`, parada de receptor SIF1 vazio por escrita zero CHCR |
| 30–31 | Parada vazia sem falsa conclusão; jogo executa 148.328.331 instruções, EE `0x80005F54`, IOP `0x00015E94` / 22.593.123 instruções, leitura da ROM1 ainda ausente |
| 32 | ROM1 validada e mapeada: 151.325.094 instruções, EE `0x80005F00`, IOP `0x000040B0` / 22.967.716; habilitação de IRQ SIO2 ocioso ainda tratada como desconhecida |

Todos mantêm zero pacotes GIF/pixels do jogo. Ensaios 30 e 31 têm os mesmos
registradores, máscaras indefinidas e RAM canônica SHA-256
`037f8a595c1ce5ec82001026f424ab7fb992895f52d677aa847b2271f6c19385`, com uma
palavra dupla indefinida. Cobertura AOT ampliada com continuações observadas
após ERET (`0x00100068`/`0x00100084`): 144.197.163 instruções AOT e 4.131.168
interpretadas. Tempo do trecho do jogo: 39,5633 s / 33,2298 s; apenas um par
sequencial no host compartilhado, sem comprovação de ganho sustentado ou FPS.
O ensaio 32 mantém o mesmo hash de RAM EE e executa em 33,8756 s.

ROM1 é opcional e somente aceita por `load_bios_rom1` com vínculo explícito a
SHA-256 da ROM0, ROMVER, modelo, mainboard, SHA-256 próprio, build interno,
versão DVD e procedência declarada. O loader confere ROMDIR/EXTINFO e o boot
revalida os bytes. Não escolhe arquivos por nome nem reaproveita outro perfil.
EE e IOP compartilham o conteúdo validado, somente leitura; reset sem ROM1
remove o mapeamento. Faixas não fornecidas continuam recusadas.

O dump local de 524.288 bytes tem SHA-256
`b86fbccaa111ca7497630337dafea7da7320fc4f924ada22b92145ef806e7b42`, build interno
`20040729-164055,`, DVD `3.10E`, soma dos módulos 428.933 bytes. A combinação
foi examinada contra o [relato físico PS2Ident de SCPH-70004](https://www.psx-place.com/threads/request-im-collecting-info-about-ps2-consoles-revisions.23276/page-10),
com mesma ROMVER `0200EC20040614`, CRC32 da ROM0 `6f8e3c29`, build DVD e soma
dos módulos. O relato usa GH-032-12; o perfil local é GH-032-11 e pertence à
mesma geração de DVD universal. É evidência de compatibilidade de metadados,
não autenticação da procedência do dump nem comparação do chip DVD completo.
ROM2 local é idêntica à ROM1 e não foi utilizada. EROM não foi mapeada.

PS2Recomp foi consultado previamente em seu mapa
[IOP memory](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xIOP/src/emulator/core/iop_memory.cpp):
não fornece implementação de ROM1 ou SIO2 para adotar como evidência.
O [PS2Ident](https://github.com/ps2homebrew/PS2Ident) e o relato físico documentam
a região DVD em `0xBE000000`. Testes próprios cobrem identidade, corrupção,
aliases EE, leitura/fetch IOP, escrita recusada e isolamento entre resets.
Dez testes focados de ROM1/ROM0/CPU/IOP/SIF passaram após o mapeamento.
Tags SIF com IRQ sem END continuam recusadas: stop/continue não comprovado.

O modelo ocioso de SIO2 deriva de não existir comando iniciado desde reset.
CTRL.0, FIFO e DMA que poderiam iniciar transferência recusam antes de alterar
estado; IRQ17 ocioso é conhecido. Isso não implementa respostas de periféricos.
O [fluxo SIO2 documentado em ps2tek](https://psi-rockin.github.io/ps2tek/#sio2registers)
foi confrontado com PS2Recomp antes da alteração. Teste próprio habilita IRQ17,
observa ausência de evento e alcança a recusa explícita do primeiro start;
quatro testes IOP/SIF passaram. A execução seguinte continua obrigatória.

O ensaio 33 atingiu 154.883.782 instruções (EE `0x80005F00`, IOP `0x1AEE8` /
23.412.547) até ler `0x1F40200B`. A revisão estrita de SIO2 mostrou que o ensaio
presumia implicitamente I_STAT.17 inicialmente limpo. O ensaio 34, com essa
presunção removida, voltou a parar em IOP `0x40B0` / 22.967.716. Portanto, o
avanço de 33 é experimental; não é prova do estado físico de reset.
A opção `--assume-iop-sio2-irq-clear` agora identifica essa hipótese explicitamente.
O default continua desconhecido até ACK do convidado; reset comum descarta a opção.

A montagem CDVD é explícita, antes da execução, de uma imagem somente leitura
com BOOT2 válido e meio CD/DVD informado. Não depende do nome do título e não
instala serviços no convidado. O estado inicial montado é disco detectado,
spindle ativo e pausado; tempo de detecção/spin-up não foi medido. Os bits 6–7
de status seguem indefinidos no IOP; leituras EE que perderiam essa informação
são recusadas. UpdateStickyFlags modifica o latch de fato e responde busy se
há comando pendente. Reset elimina a imagem, sem herdar outro perfil.
Consultado previamente [CDVD no PS2Recomp](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xIOP/src/emulator/imports/iop_cdvd.cpp);
seus serviços HLE não foram incorporados. Registradores seguem ps2tek.
Cinco testes ROM1/IOP/CDVD/SIO2 e o teste de disco ISO/Mode1/Mode2 passaram.
O modo BIOS original foi repetido sem as opções: mesmos PCs e hash anteriores
(`gs-iop-original-final.log`, ignorado), zero pacotes/pixels do jogo.

### CDVD ReadCd e DMA3 — continuação

- Ensaio 35: 154.886.806 instruções, EE `0x80005F00`, IOP `0x1B470` /
  23.412.925; escrita HOWTO `0x80` em `0x1F402006`. UpdateStickyFlags foi
  executado pelo convidado. Zero pacotes GIF/pixels.
- Ensaios 36–37: 154.888.934 instruções, IOP `0x1ADF4` / 23.413.191;
  ReadCd do LBA 16, um setor, retry `0x10`, spindle `0x85`, formato 2048.
  Zero pacotes GIF/pixels; ainda nenhum setor transferido nesses ensaios.
- ReadCd CLV/2048 e DMA3 request-mode transferem setores reais da imagem,
  com buffer de um setor, backpressure, DPCR/DMACEN, BCR/MADR, flag DMA e
  IRQ CDVD apenas depois dos bytes entregues. Tamanhos, modos, endereços e
  comandos não cobertos continuam recusados. Sem execução host de IRX/RPC.
- Testes próprios validam dois setores byte a byte, ausência de IRQ antes
  dos dados, recepção curta que mantém o comando ocupado, canal desabilitado
  e ACK independente. Seis testes IOP/SIF/CDVD passaram.
- HOWTO `0x80` foi relacionado ao caminho de dados de 2048 bytes em
  [ps2sdk CDVDMAN](https://github.com/ps2dev/ps2sdk/blob/master/iop/cdvd/cdvdman/src/cdvdman.c)
  e ao [mapa DEV5](https://github.com/ps2dev/ps2sdk/blob/master/common/include/dev5_mmio_hwport.h).
  PS2Recomp CDVD foi consultado previamente; implementação BSD independente,
  sem copiar nem adaptar seus serviços HLE ou código de outras licenças.
- Latência de setor CLV e seek de 30/100 ms são hipóteses funcionais baseadas
  em ps2tek, não timing medido. CAV radial continua recusado no default.
  A montagem pode habilitar explicitamente o experimento CAV com tempo
  nominal CLV; ele é registrado no log e não é evidência de fidelidade temporal.
- CI do commit `d4f5803`: seis alvos do núcleo aprovados no
  [run 41781003115](https://github.com/fabegalo/ps2-native/actions/runs/41781003115).
  Isso cobre o checkpoint ROM1/CDVD media, ainda não o DMA3 acrescentado depois.

### Setores reais e continuação do SIF1

O ensaio 38 transferiu os LBAs 16, 18, 24 e 15637–15691 da imagem local;
a última faixa contém IOPRP202. O convidado voltou a executar configuração
MECHACON e inicialização SIF depois dessas leituras. Não houve loader IRX host.
O IOP parou em `0xAE98`, ciclo 31.825.435, na tag SIF1 `NEXT` de QWC zero
em `0x21570`, destino `0x21380`. O harness continuou indevidamente o EE até
um bilhão de instruções, no loop `0x81FC0–0x81FDC`: isso não é progresso.
A continuação local agora termina também quando o IOP está parado.

Implementado `NEXT` genérico: payload adjacente à tag, próximo TADR fornecido
por ADDR, QWC zero sem fabricar dados ou conclusão. Testes próprios cobrem
payload, salto vazio, endereço inválido e regressões REFE/REF/IRQ.
Consultado previamente [PS2Recomp DMA](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/Stubs/DMA.cpp)
e conferida a semântica na [documentação DMA](https://psi-rockin.github.io/ps2tek/#dmac).

Ensaio 39: `NEXT` executado; 1.920 bytes SIF1 / 1.696 SIF0. Novo bloqueio
IOP `0x1F458`, 31.828.183 instruções: comando MECHACON `03:00`, leitura
da versão. EE amostrado em `0x81FD0`, orçamento acumulado 240 milhões,
62,7584 s; RAM canônica `e2811c37c7ba28af796ae54231016dbf6e23f3db0e0a254f655de966dd04976d`,
um doubleword parcialmente desconhecido. Zero pacotes GIF/pixels do jogo.

A consulta `03:00` usa agora os quatro bytes já medidos do perfil selecionado,
sem versão universal ou byte de sucesso acrescentado; perfil desconhecido e
outros subcomandos continuam recusados. PS2Recomp CDVD foi novamente consultado.
O IRQ de ReadCd passou de `3` nos ensaios 38–39 a `2` (conclusão de comando),
conforme o comportamento de referência examinado em
[PCSX2 CDVD](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/CDVD/CDVD.cpp);
a menção de ps2tek a ambos os bits é incerta. Sem medição física reivindicada.
Sete testes focados IOP/SIF/CDVD/SIO2 passaram; após a consulta de versão,
três testes diretamente afetados passaram. Reexecução do jogo em andamento.

Ensaio 40: a consulta MECHACON avançou e o CDVD chegou ao LBA 15824,
final de RSPU2DRV.IRX. SIF1 4.112 bytes, SIF0 2.352 bytes. Novo bloqueio:
IRQ SPU2 desconhecido ao reabilitar interrupções, IOP `0x40B0`, 43.408.842
instruções; EE `0x81FC4`, 320 milhões acumulados, 88,4422 s.
RAM `68c8999aceb882f3498a567682de746f2e28e77625d2a21b462173bfe83e215b`;
zero pacotes GIF/pixels. As amostras anteriores do loop idle ainda acompanhavam
leituras CDVD reais; não eram por si só evidência de travamento.
O modo original foi repetido após DMA3/NEXT/MECHACON: mesmas contagens/PCs e
RAM `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.

### Primeiro pedido DMA GIF — investigação em andamento

SPU2 foi consultado em [PS2Recomp libsd](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xIOP/src/modules/libsd.cpp)
e no mapa IOP: o serviço HLE não foi usado. A opção separada
`--assume-spu2-quiescent-reset` declara cores inativos/IRQ9 limpo, estado inicial
não medido neste console; defaults continuam desconhecidos, e todos os acessos
SPU2/DMA de som continuam recusados. Teste cobre opt-in, recusa e reset.

Com essa hipótese adicional, ensaio 41 chegou ao primeiro pedido DMA GIF:
EE `0x1CEC78`, 337.635.052 instruções; IOP ativo `0xAE94` / 46.256.466.
SIF1 5.264 bytes / SIF0 2.592. CHCR do canal 2 recebe `0x101`; a RAM convidada
contém BITBLTBUF/TRXPOS/TRXREG/TRXDIR para imagem de 256×256, seguida de tag IMAGE.
Ainda zero pacotes/pixels no ensaio 41, pois o canal não estava implementado.

O transporte normal do canal 2 foi ligado ao parser GIF/GS comum, com origem
na RAM, MADR/QWC/STR, D_STAT, DMAE, prioridade e CPND. Dados desconhecidos e
modos não implementados recusam; conclusão só após entrega ao GIF. A hipótese
de throughput é um qword por BUSCLK, sem alegar timing de barramento físico.
Consultados previamente [PS2Recomp DMA](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/Stubs/DMA.cpp)
e [árbitro GIF](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/gs/ps2_gif_arbiter.cpp).
Implementação própria, sem reaproveitar código GPL. Teste próprio confere os
quatro pixels enviados por uma transferência IMAGE, suspensão e dados indefinidos.

Commit `6ca180f`: CI núcleo e launcher aprovadas nos seis alvos, runs
[41783825838](https://github.com/fabegalo/ps2-native/actions/runs/41783825838) e
[41783825839](https://github.com/fabegalo/ps2-native/actions/runs/41783825839).
Isso ainda não cobre as alterações posteriores de DMA GIF/SPU2.

### Pacotes gráficos reais — ensaio 42

O jogo completou **16 pacotes GIF PATH3**, cada um com quatro escritas A+D e
16.384 qwords IMAGE. O GS registrou **1.048.576 escritas de pixels**: uma
limpeza real da VRAM, executada pelos comandos do jogo. O pacote anterior de
44 escritas pertence ao firmware e foi excluído da contagem do jogo.
Não é um frame exibido, tela de abertura, menu ou gameplay.

EE `0x1CDF74`, 338.172.827 instruções (94,5503 s); IOP ativo `0xAE98` /
46.323.687. Próximo bloqueio: LD do GS CSR, seguido de DSRL 16 / ANDI FF
para obter a revisão do GS. RAM canônica
`4b766ae1b4393f039a5348d88312632222a72c4fe1c2d456b492ad3964630137`.
Extração diagnóstica dos primeiros 256×256 pixels da VRAM: preta,
SHA-256 RGBA8 `b7997c60612a8f60f104b19583162aaac7dde1f1ab2e8ef7c9b935550e2b0aa8`;
artefato local ignorado, não apresentado como framebuffer do jogo.

Oito testes focados GIF/GS/SIF/SPU2 passaram. O modo original foi repetido
após essas mudanças: 17.690.079 / 4.486.087 instruções, PCs
`0x80007D20` / `0x80005F00`, mesmo SHA-256 da RAM `cead2f7c…def9c354`.

A leitura parcial do CSR agora expõe REV/ID somente quando medidos no perfil.
A base local PS2Ident (formato conferido em `ps2db.h`, revisão
`4b84d1882b47fbcdcff7d34ef36e9e726512b2e2`) registra `1B/55` para
SCPH-70004 GH-032-11/GH-032-32 e ROMVER 0200EC20040614.
FIELD/NFIELD, FIFO e bits não comprovados permanecem simbolicamente desconhecidos.
Não há fallback por região ou por nome semelhante. PS2Recomp memory/GS foi
consultado antes da implementação; os valores vêm das medições PS2Ident.

### CSR e máscara de interrupções — ensaio 43

A leitura da revisão passou sem tornar conhecidos FIELD/NFIELD/FIFO. O próximo
bloqueio foi EE `0x80000D4C`, 338.172.879 instruções, SD de `0xFF00` ao IMR;
IOP ativo `0x80000094` / 46.323.694. Foram mantidos os 16 pacotes e 1.048.576
pixels da limpeza (102,145 s). O dump posterior do harness tentou ler RAM IOP
não escrita; isso encerrou a coleta, sem alterar o bloqueio do runtime.

O bit 15 do IMR é descartado na escrita, preservando as cinco máscaras e os
bits 13/14 em um. A correção cobre stores de 32/64 bits e testa um evento
pendente para impedir reconhecimento ou IRQ indevido. Outros campos reservados
continuam recusados. Consultados previamente
[PS2Recomp GS](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRuntime/src/lib/Kernel/Stubs/GS.cpp)
(helpers HLE, sem contrato IMR aproveitável) e comportamento de
[PCSX2 GS](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS.cpp).
Implementação BSD própria; não há alegação de nova medição física do IMR.

Commit `d4d98a1`: núcleo e launcher aprovados nos seis alvos, runs
[41785878199](https://github.com/fabegalo/ps2-native/actions/runs/41785878199) e
[41785878204](https://github.com/fabegalo/ps2-native/actions/runs/41785878204).
A correção posterior do IMR passou em cinco testes locais focados (eventos,
IRQ, reset, conhecimento parcial e DMA GIF); ainda não coberta por esses runs.

### Após IMR — ensaio 44

IMR foi superado. EE `0x1092CC`, 339.407.163 instruções (94,3134 s), encontrou
ADDI ainda não decodificada. IOP ativo `0x4698` / 46.477.979, 16 pacotes GIF e
1.048.576 escritas de pixels, ainda apenas limpeza da VRAM. RAM canônica
`f25c267cb6c290a08d3575bf94310ec3ce26cb8d55584cc6f7185a123016f5a7`.
O modo original repetido após IMR manteve contagens/PCs/hash anteriores.

ADDI passa a ser uma fronteira explícita do interpretador: soma de palavra,
extensão de sinal, preservação da metade alta do GPR e exceção 12 em overflow,
sem alterar o destino. O handler convidado recebe EPC/BD corretos e o branch
pendente é cancelado no trap. Testes próprios incluem limites positivo/negativo,
destino zero, alias, delay slot e origem desconhecida. Consultados PS2Recomp
decoder/translator e EE Core User's Manual, seção 4.2.16; código próprio BSD.

### Após ADDI — ensaio 45

ADDI executou e o EE avançou até `0x109364`, 339.415.003 instruções (91,365 s),
ADD de registradores ainda ausente. IOP ativo `0xAE94` / 46.478.959. RAM canônica
`be56d15eac3de03a4a05cbb1a6c6d54292b0f19e173138594b2f445193cb9086`.
Continuam 16 pacotes/1.048.576 pixels da limpeza, sem frame exibido.
O modo original repetido após ADDI preservou PCs, contagens e hash anteriores.

A família ADD/SUB/DADD/DSUB/DADDI usa agora o mesmo caminho de exceção de ADDI,
com aritmética host unsigned para não introduzir overflow indefinido em C++.
AOT continua com fronteira explícita. Testes cobrem resultados nos limites,
overflow nos dois sentidos, alias e campos reservados. PS2Recomp special
translator foi consultado antes; não foi copiada sua tradução sem exceções.

### Após aritmética inteira — ensaio 46

EE `0x100938`, 339.415.345 instruções (100,716 s): SUB.S ausente, operandos
`00000000` e `431B0000`. IOP ativo `0xAE98` / 46.479.002; mesmos 16 pacotes e
1.048.576 pixels. RAM canônica
`7bb5c94c14e55264f66f654a4ec47a197133bf3c6f9ba9b5db458b787c9d1049`.
O modo original foi repetido após a família inteira: mesmo hash/PC/contagens.

ADD.S/SUB.S passam a usar um somador inteiro independente de float do host:
alinhamento com um bit de guarda, descarte antes da normalização, truncamento,
expoente 255 finito, denormais como zero e flags O/U com histórico sticky.
Consultados previamente [PS2Recomp FPU](https://github.com/ran-j/PS2Recomp/blob/e5636981a1c9de368f6b71e819c3dc188dfedbf4/ps2xRecomp/src/lib/fpu_translator.cpp)
e seus macros; implementação própria baseada no contrato aritmético e na
[descrição de precisão](https://pcsx2.net/blog/2009/ps2-vu-vector-unit-documentation-part-1/).
Testes próprios usam observações numéricas de
[ps2autotests](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/cpu/ee_fpu/arithmetic.expected),
incluindo sinais de zero e valores no expoente estendido. Isso não certifica
a FPU inteira: MUL/MADD e o acumulador continuam com os limites registrados.

Commit `8410973` (IMR/ADDI): núcleo e launcher aprovados nas seis arquiteturas,
runs [41787441581](https://github.com/fabegalo/ps2-native/actions/runs/41787441581)
e [41787441462](https://github.com/fabegalo/ps2-native/actions/runs/41787441462).
Commit `94230de` (família aritmética inteira): testes focados aprovados localmente;
CI e ensaios posteriores registrados separadamente.

### MUL.S e limite comprovado — ensaio 47

SUB.S foi executada. O novo bloqueio é EE `0x100948`, 339.415.349 instruções
(106,423 s), MUL.S com `fs=C31B0000` (-155) e `ft=453B8000` (3000).
IOP ativo `0xAE94` / 46.479.003. Permanecem os 16 pacotes GIF e 1.048.576
escritas de pixels; nenhum frame exibido. RAM canônica igual ao ensaio 46,
`7bb5c94c14e55264f66f654a4ec47a197133bf3c6f9ba9b5db458b787c9d1049`.
Após ADD.S/SUB.S, o modo original também repetiu os mesmos PCs/contagens e
hash `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.

Produto inteiro das mantissas: `0x718680000000`. O limite conservador já
implementado abrange mantissas retidas `E30CFF` e `E30D00`; portanto não decide
entre `C8E30CFF` e `C8E30D00`. O multiplicador tem somente 15 zeros baixos;
a pesquisa publicada alerta que esse limite não permite substituir a árvore
truncada por multiplicação comum. Não foi relaxada a recusa nem inserido caso
especial por operandos, endereço ou jogo. Continuar exige caracterizar os
carries descartados do multiplicador, com implementação BSD independente e
resultados de referência verificáveis. O repositório PS2Float consultado não
declara licença; nenhum código dele foi copiado, traduzido, vinculado ou incorporado.

CI do commit `94230de`: núcleo aprovado nos seis alvos, run
[41787994140](https://github.com/fabegalo/ps2-native/actions/runs/41787994140).
O commit `1ddd0d1` compilou, mas a CI encontrou um teste antigo que esperava
ADD.S não implementada. Corrigido em `dd6ed41`: a fixture de recusa usa SQRT.S,
ainda ausente. `native-m20-boot` e `native-ee-context` passaram localmente;
verificar a nova CI antes de considerar o conjunto concluído.

Validação final: `dd6ed41` aprovado nos seis alvos do núcleo,
[run 41789281037](https://github.com/fabegalo/ps2-native/actions/runs/41789281037).
Launcher de `1ddd0d1` aprovado nas seis arquiteturas,
[run 41788609723](https://github.com/fabegalo/ps2-native/actions/runs/41788609723).
Nenhum executável local de diagnóstico foi deixado rodando. Nenhum frame de
jogo foi declarado; primeiro pacote real comprovado, jogabilidade não atingida.

### Continuação do multiplicador — carry da coluna 15

A ambiguidade do ensaio 47 foi reduzida por uma propriedade de uma classe de
operandos, sem tabela por valores ou endereço: mantissa do segundo operando
com exatamente 15 zeros baixos. A recodificação radix-4 começa com -2 na
coluna 14. O complemento e a correção de sinal retardada têm, separadamente,
um bit nessa coluna; o corte de cada linha elimina o carry para a coluna 15.
Os dígitos seguintes começam na coluna 16 e não podem mudar essa perda.
O produto antes da normalização é, portanto, o produto inteiro menos `2^15`.
As outras classes continuam sob a prova de intervalo anterior e recusam quando
ela não decide o resultado. Isso não é um multiplicador geral concluído.

PS2Recomp FPU/macros foi consultado antes. A descrição comportamental do carry
foi confrontada com a referência PS2Float, sem copiar ou incorporar sua árvore
nem sua implementação. A implementação BSD é a redução aritmética dessa classe.
A observação física publicada por F0bes confirma `1 * 129,5` como `43017FFF`,
enquanto a ordem inversa preserva `43018000`:
[relato](https://fobes.dev/ps2/detecting-emu-vu-floats) e
[resultados crus](https://gist.github.com/F0bes/5f209d8a0890879ad0e25ceb6973977a).
O teste também cobre sinal, multiplicando não unitário e produto cuja mantissa
retida não muda apesar da perda. Os resultados físicos citados são VU; o modelo
comum EE/VU é a referência comportamental, sem alegar nova medição de COP1 local.

Ensaio 48: passou o MUL.S anterior e duas DIV.S, alcançando EE `0x100998`,
339.415.369 instruções, IOP `0xAE94` / 46.479.005. Ainda 16 pacotes e
1.048.576 escritas de pixels, apenas a limpeza. Tempo local 97,9671 s;
RAM SHA-256 `74118d79b7f299f540c08869a19a0ceb05a5dda4ff128be9a1f27ff0bdd70e28`.
A nova multiplicação usa `44FA0000 * 447A0000` (2000 × 1000). O limite esparso
anterior exigia 18 zeros baixos, embora 16 sejam suficientes: o primeiro dígito
Booth não nulo, inclusive complemento/correção de sinal, começa na coluna 16
ou superior, sem contribuição nas colunas descartadas 0–14. A implementação
passa a aceitar essa classe pelo produto inteiro exato; a classe de 15 zeros
continua com perda de carry, e os demais casos continuam sujeitos à prova de
intervalo ou recusa. Regressões incluem 16/17 zeros e multiplicando não inteiro.
O harness local agora continua além do primeiro pacote, até recusa ou orçamento.

Ensaio 49: multiplicações da projeção superadas; EE `0x1CD9CC`, 339.416.879
instruções, IOP `0xAE98` / 46.479.194. Recusa de NEG.S `46008807`.
95,7098 s; RAM SHA-256 `5c8f447f3c467323c9756f558ba05f76a1be98d99e8fe1e9683e74d5def8a6b5`.
Os 16 pacotes/1.048.576 pixels continuam apenas a limpeza. Testes locais de
contexto EE e boot aprovados após ampliar a classe esparsa.

NEG.S e ABS.S acrescentados como fronteiras explícitas do interpretador:
inversão/remoção do bit de sinal, preservação de todos os bits de magnitude,
inclusive denormais e expoente 255; limpam causas O/U, preservam sticky/I/D.
Consulta prévia ao tradutor FPU/macros do PS2Recomp; ele usa operações host e
não resolve a fidelidade de flags. Valores confrontados com observações
[ps2autotests arithmetic.expected](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/cpu/ee_fpu/arithmetic.expected),
flags com manual EE e comportamento de referência PCSX2. Implementação BSD
independente por operações inteiras, sem incorporar código das referências.
Regressões próprias cobrem payloads, zeros, alias fd/fs, campos reservados e CU1.

Após NEG.S/ABS.S, repetição original: firmware 17.690.079 instruções em
`0x80007D20`; ELF injetado 4.486.087 em `0x80005F00`, IOP `0x40B0` /
2.210.789. Zero pacotes/pixels do jogo e RAM canônica idêntica
`cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
CI de `278a1d0`: núcleo 41791750967 e launcher 41791750925, ambos aprovados
nos seis alvos. A CI das correções seguintes é acompanhada separadamente.

Ensaio 50: NEG.S passou; EE `0x1CDA04` / 339.416.893 instruções, IOP
`0xAE98` / 46.479.196, 96,4465 s, RAM igual ao ensaio 49. MUL.S usa
`4A800000 * 4B7FFFFF`; ainda só os 16 pacotes da limpeza. Instrumentação
confirma SMODE2=3 e PMODE ainda não escrito; não existe scanout configurado.

A prova do carry foi generalizada para os 16 bits baixos do multiplicador
iguais a `-2^k` módulo 65536, k de 0 a 15. Há um único dígito Booth negativo
abaixo da coluna 16; os seguintes não podem alterar colunas inferiores.
Quando a mantissa do multiplicando vezes `2^k` tem 15 bits baixos nulos,
a linha complementada e a correção separada de sinal geram um carry para 15,
perdido pelo corte. O produto retido equivale ao produto inteiro menos 32768.
Isso inclui a prova anterior (k=15) e a mantissa máxima (k=0), sem tabela de
operandos, título ou PC. Classes fora da prova continuam recusadas quando o
intervalo não decide. Mantissas de `1 * CF_MAX_MANTISSA`, ordem inversa e
`1 * CF_MIN` conferem com observações físicas **EE**, em
[muldiv.expected](https://github.com/unknownbrackets/ps2autotests/blob/master/tests/cpu/ee_fpu/muldiv.expected).
Regressões também cobrem posições diferentes do dígito e expoentes. Testes
`native-ee-context` e `native-m20-boot` aprovados. A implementação permanece
inteira e independente; não foi incorporada uma árvore de código externo.

Ensaio 51: multiplicador passou; EE `0x1CD4E4` / 339.416.913 instruções,
IOP `0xAE98` / 46.479.198. Opcode `4A202128` é **VADD.w**, não VMUL
(tabela COP2 Special1, função 0x28). 94,3292 s; RAM SHA-256
`619a09cb251c2e004f3696a0f8d2f0a3386bfeee64e0b8e03aac0e7d1a5bd718`.
Continua sem PMODE e só a limpeza inicial. A sequência convidada monta matriz
identidade com VSUB, VADD.w, VMR32 e SQC2.

Consultados PS2Recomp `vu_translator.cpp`/`ps2_vu1_upper.cpp`, tabela COP2 do
[ps2tek](https://psi-rockin.github.io/ps2tek/index.html) e
[manual VU 6.0](https://docs.alexrp.com/mips/ee_vu.pdf), VADD/VMR32/LQC2/SQC2.
Implementação independente: VADD somente na classe exata com pelo menos um
addend zero (denormal tratado como zero); demais operandos recusados, sem
assumir precisão geral de VU. Máscara, sinais e flags MAC/status/sticky calculados
antes de commit atômico. VMR32 preserva bits desconhecidos, máscara e alias.
LQC2/SQC2 transportam 128 bits observados, mantêm VF00 e recusam desalinhamento
conforme manual, diferentemente de LQ/SQ. Semântica compartilhada AOT/intérprete.
Mantém o modelo síncrono explícito existente de macros; não declara pipeline
VU de quatro ciclos nem microprogramas completos. Nenhuma lógica de jogo
substituída, nenhum dado proprietário incluído no código/testes.

Ensaio 52: matriz identidade concluída; EE `0x1CD208` / 339.416.958 instruções,
IOP `0xAE98` / 46.479.204, 89,593 s. RAM SHA-256
`4f083bd2cb55035cd50b8fad28f4afa1aefa679b85db227e2b41e977df3d9276`.
Nova recusa em VMULAx, seguido de VMADDAy/VMADDAz/VMADDw para multiplicação
de matrizes. Sem PMODE, mesmos 16 pacotes da limpeza. Testes de contexto,
boot, VU existente e execução GS convidada aprovados. CI de `bc80a16` e
`1c929e3`: seis alvos do núcleo e seis do launcher aprovados.

A investigação do acumulador consultou novamente o caminho VU de PS2Recomp
e o manual, seções 2.1/2.2/3.3.6 e instruções MUL/MULA/MADD/MADDA.
O [relato técnico de cottonvibes](https://pcsx2.net/blog/2009/ps2-vu-vector-unit-documentation-part-1/),
nota 3, confirma que VU e FPU usam o alinhamento com um guard bit. VADD passa
a compartilhar o somador inteiro existente, incluindo MAC/status O/U. O
multiplicador usa as mesmas provas de Booth, preservando recusas ambíguas.
ACC passa a ter quatro palavras observadas inicialmente desconhecidas; versões
vetoriais e broadcast de MUL/MULA/MADD/MADDA validam todos os componentes
antes de modificar destino, ACC ou flags. MADD não modifica ACC. Produtos e
somas excepcionais ainda recusam, pois o estado extra de overflow do ACC e
as regras combinadas de exceção não estão implementados. Sem float/FMA host.

Ensaio 53: concluiu a multiplicação de matrizes; EE `0x1CD240` /
339.417.051 instruções, IOP `0xAE94` / 46.479.215. Recusa em VOPMULA
`4BC522FE`; 98,8241 s. RAM SHA-256
`9921efa669f7d010ac7902e81488c1929edb81a4e45b0a065809d12856bb21dd`.
Mesmos pacotes/pixels de limpeza, sem PMODE. Modo original repetido após ACC:
mesmos PCs, contagens, zero pixels e hash canônico. CI de `b176844` aprovada
nos seis alvos do núcleo (41794461017) e launcher (41794461153).

Próxima extensão: VOPMULA/VOPMSUB com permutação cíclica xyz e operandos na
ordem da instrução, preservando w. Referência prévia PS2Recomp VU upper e
manual VU pp.116–117/305–306. Reutiliza o produto e acumulador com recusas
numéricas existentes. VADD/VSUB broadcast e VSUB geral usam o somador comum
com um guard bit; os testes verificam produto vetorial não trivial, máscara
fixa xyz, alias, preservação de w, broadcast e subtrações não cancelantes.
O tempo dos ensaios é observação local, não benchmark controlado: builds e
trabalho concorrente impedem atribuir diferenças de duração a ganho de desempenho.

Ensaio 54: produto vetorial e ADD/SUB broadcast passaram. EE `0x1CD290`,
339.417.063 instruções; IOP `0xAE94` / 46.479.217; 90,4409 s. VSQRT Q,VF05.x
é a nova fronteira. RAM SHA-256
`abffb5fd47843d18aeb4371116a550e8ed0eef5be84cdc41ac1baa02efb62f34`.
Ainda somente 16 pacotes/1.048.576 escritas da limpeza, sem PMODE ou scanout.
CI de `0482701` e `afdc202`: núcleo e launcher aprovados nos seis alvos.

Implementação seguinte: VSQRT exata, VDIV, VWAITQ, VADDq e VMULq. Consulta
prévia a PS2Recomp `vu_translator.cpp` mostrou Q resolvido imediatamente;
o modelo aqui segue os sete ciclos de DIV/SQRT do manual VU, com Q anterior
observável até a conclusão. Raízes somente quando o quadrado inteiro prova
representabilidade exata; demais entradas recusam, sem sqrt do host. DIV
reutiliza o circuito SRT inteiro existente; não houve incorporação de código
externo. Macros ALU ainda usam o modelo síncrono anterior, não uma alegação
de pipeline VU completo.

A espera mantém contagem separada de ciclos e instruções: Count, timers,
GS, IOP e apresentação observam instruções aposentadas + ciclos de stall.
Compare é retido quando a espera atravessa seu valor, inclusive no wrap.
Isso preserva a hipótese base de um ciclo por instrução e acrescenta a
latência comprovada de Q; não declara timing superscalar completo.

Ensaio 55: VSQRT/VDIV/Q passaram; 22 ciclos de stall, EE `0x1CD310` /
339.417.122 instruções; IOP `0xAE94` / 46.479.227; 94,1727 s. Próxima recusa
VMOVE `4BE5233C`. RAM SHA-256
`f8f6e8821b83ca2e6510224373c289b6e14419107632f6d2aae463c49d876fa6`.
Sem novos pacotes/pixels ou PMODE. Quatro testes focados aprovados, incluindo
Q antigo antes da conclusão, espera sem aposentadorias extras, flags I/D,
reset, incerteza, Compare atravessado e timer BUSCLK. Original repetido:
mesmos PCs/contagens e hash canônico, zero pixels.

VMOVE implementado após consulta à tradução VU de PS2Recomp: cópia crua
por componente selecionado, sem alterar flags; VF00 permanece constante.
Implementação BSD por máscaras inteiras, preservando alias e bits desconhecidos.
Os quatro testes focados passaram; ensaio 56 iniciado para observar a continuação.

Ensaio 56: VMOVE passou; EE em espera `0x81FCC`, 340.000.000 instruções;
IOP parou em delay slot `0x65330` / 46.490.573, SH de 8 em `0x1F9007CA`.
95,0408 s; 22 ciclos de stall. RAM SHA-256
`dab444eaa2bfea43d016b3710d5321c72e8f640c88f1c757ee2269861a8b404e`.
SIF1 6.528 bytes/SIF0 3.696; ainda apenas limpeza GIF, sem PMODE.

Investigação de 0x7CA: PS2Recomp `ps2_audio.cpp` fornece áudio host e não
um modelo desse MMIO. PS2SDK `spu2_mmio_hwport.h` o nomeia `m_unknown7ca`;
`libsd/src/freesd.c`, InitSpdif, escreve 8 depois de MODE/MEDIA. Não é
SPDIF_PROTECT, que fica em 0x7CC. PCSX2 `SPU2/RegTable.cpp` o guarda como
Unknown2, referência de comportamento e não prova física. Nenhum código
externo foi incorporado.

Adicionado experimento **opt-in** de retenção de escritas halfword conhecidas
em 0x7CA, omitindo efeitos desconhecidos; leituras continuam recusadas.
O reset desabilita a hipótese, e o modo original recusa a escrita com causa.
A hipótese é registrada pelo harness, não convertida em comportamento padrão
nem em evidência de áudio correto. O ensaio seguinte usa essa hipótese extra
para investigar os bloqueios posteriores.

Ensaio 57, com hipótese 7CA: IOP avançou até `0x65948` / 46.490.611,
SH de zero em `0x1F900B60`; EE espera `0x81FCC`, 340.000.000 instruções,
22 stalls, 98,0444 s. RAM e contadores GIF iguais ao ensaio 56. Cinco testes
focados passaram; modo original repetido manteve PCs, contagens e hash canônico.
A CI encontrou no commit Q uma diferença do patch publicado: advance_q ficou
após return ao selecionar hunks sem contexto. `da867c6` publica a ordem correta
já testada localmente; nova CI acompanhada. Não foi relaxada a regressão.

A inspeção local dos setores do próprio disco identificou no IRX a sequência
SH zero em 0x760/0x762/0xB60/0xB62; o endereço 0xB60 não foi produzido por
uma exceção de endereço inventada. A referência PCSX2 aplica máscara 0x7FF
na janela SPU2 e conserva a região 0x346..0x3FF como armazenamento bruto;
isso não prova seus efeitos em hardware. O diagnóstico seguinte opta
separadamente por reter escritas halfword conhecidas nessa região não
atribuída e seu espelho. Modo original, leituras e registradores funcionais
continuam recusando o que não é modelado. Não é prova de áudio correto.

Acrescentada leitura diagnóstica da RAM IOP no instante da parada, com
nullopt para bytes nunca escritos. Ela não é uma leitura de barramento EE,
não retoma o IOP e não relaxa a recusa de ee_read após stop. Permite registrar
as instruções próximas dos próximos bloqueios sem reconstruir código externo.

Ensaio 58: região 0xB60 passou sob hipótese explícita; IOP `0x65B58` /
46.497.746, escrita zero em 0x1F9007B8; EE espera `0x81FCC` / 340.000.000,
22 stalls; 94,3279 s. RAM, GIF e ausência de PMODE iguais ao ensaio 57.
Modo original repetido mantém o hash canônico. A tabela de referência marca
também 0x746..0x75F e 0x7B0..0x7BF como regiões não atribuídas; o experimento
foi estendido a essas lacunas e seus espelhos, sem aceitar leituras ou
registradores funcionais. Isso segue sendo hipótese, não medição do hardware.

Ensaio 59: IOP `0x66B68` / 46.497.781, preparando FIFO SPU2 em 0x1F9001AC;
o caminho PIO do workspace recusa sem taxa explícita. EE espera `0x81FCC`,
340.000.000 instruções, 22 stalls; 94,4362 s. RAM/GIF/PMODE iguais ao 58.
A continuação seleciona 8 ciclos IOP por halfword como hipótese não medida.
O caminho transfere os bytes reais FIFO→RAM e só encerra busy ao drenar;
não fabrica DMA, IRQ ou conteúdo. Teste de PIO próprio existente exercita
latências 3/17, payloads, wrap, busy, compartilhamento de RAM e recusas;
seis testes focados passaram antes do ensaio 60. A escolha 8 não comprova
timing físico nem fidelidade de áudio; contadores/palavras transferidas foram
acrescentados ao diagnóstico local. PS2Recomp áudio consultado antes do uso,
sem incorporar código. O trabalho PIO paralelo é preservado, não reimplementado.

Ensaio 60: o guest enviou 16 halfwords reais por PIO core 0, gravadas em
RAM SPU2 0x2800..0x280F: oito 0x0707 e oito zero. Core 1 não transferiu.
IOP avançou até `0x66C10` / 46.498.128, escrita em VOLL da voz 0/core 1,
0x1F900400. EE espera `0x81FCC` / 340.000.000, 22 stalls, 92,5297 s.
RAM EE e GIF iguais ao ensaio 59; ainda sem PMODE/frame.

Próxima extensão funcional: VOLL/VOLR constantes, PITCH, ADSR1/2 e SSA
para vozes estabelecidas inativas. Referências prévias PS2Recomp áudio,
PS2SDK `spu2_mmio_hwport.h` e comportamento de registradores em PCSX2
`RegWrite_VoiceParams`/`RegWrite_VoiceAddr`; implementação BSD independente.
SSA só consome quatro bits altos e os bits baixos 15..3; readback reservado
continua simbólico e pode ser copiado para outro SSA sem consumir incerteza.
KON, volume sweep e processamento ENVX permanecem recusados, não simulados
por sucesso. Nenhuma voz é iniciada por escrever parâmetros.

Sete testes focados passaram para parâmetros/SSA; ensaio 61 iniciado.
CI da correção publicada de Q `da867c6`: núcleo 41799523668 e launcher
41799523720 aprovados nos seis alvos. `be450b1` também passou nos seis do
núcleo (41800073863); demais pipelines continuam acompanhados. O caminho
PIO paralelo foi registrado em `387aabe`; seu conteúdo não foi incluído nos
commits de configuração de vozes deste trabalho.

Ensaio 61: parâmetros das 48 vozes passaram; IOP `0x66CD4` / 46.499.143,
KON core 1 em 0x1F9005A0. EE `0x81FCC` / 340.000.000, 22 stalls,
95,7011 s; RAM/GIF/PMODE iguais ao 60. PIO preservou as 16 palavras reais.

Continuação de KON/KOFF: PS2Recomp áudio novamente consultado; não contém
modelo de comandos SPU2. PS2SDK fornece mapa, e PCSX2 `RegTable.cpp`,
`spu2sys.cpp` e `Mixer.cpp` foram consultados somente como referência de
comportamento. Implementação BSD própria: comandos halfword pendentes,
consumidos na borda de 48 kHz; readback é comando, não bitmap de atividade.
A fase física desses comandos ainda não foi medida. Bits reservados ficam
desconhecidos, inclusive ao copiar o comando entre cores.

Vozes iniciadas tornam-se simbólicas: somente VOLL=VOLR=0 constantes permitem
provar a contribuição zero no mixer sem inventar ADPCM ou envelope. KON exige
ambos os comparadores IRQ de RAM explicitamente desabilitados. KOFF não prova
fim da release nem permite unmute; qualquer ganho não zero posterior recusa.
ENVX, ENDX, NAX e LSAX não são fabricados. Os buffers de captura pré-volume
das vozes 1/3 permanecem desconhecidos na inspeção RAM, mesmo com ganho zero.
Isso não implementa reprodução audível nem decodificação, e continua dependendo
da hipótese inicial explícita de vozes inativas. Sete testes focados passaram,
incluindo readback antes/depois da borda, cópia entre cores, desconhecimento,
unmute após KOFF e IRQ do outro core. Ensaio 62 preparado.

CI de `be450b1`, `fbb2140`, `d63cdba`, `387aabe` e `8e3a16f` concluída com
sucesso no núcleo e launcher, seis alvos por workflow. Para `8e3a16f`, runs
41802889808 / 41802889728. Isso valida builds/testes, não execução do jogo.

Ensaio 62: KON, espera e KOFF passaram. EE `0x81FCC` / 360.000.000;
IOP `0x66D94` / 46.555.415, limpeza de ENDX em 0x1F900342. 97,8691 s,
22 stalls, mesmo hash RAM e apenas 16 pacotes/limpeza da VRAM; sem PMODE.
ENDX agora aceita limpeza do banco por escrita halfword. Só permanece
comprovadamente zero enquanto nenhum decoder desconhecido puder alterá-lo;
a próxima borda invalida os bits das vozes simbólicas. Não fabrica fim de
reprodução. Sete testes focados passaram, incluindo os dois cores/bancos,
limpeza independente do dado e recusa de ENDX posterior de voz desconhecida.
Ensaio 63 iniciado. Uma sobreposição do commit paralelo ADMA `9150a3c`
retirou do índice a implementação KON publicada em `04d26de`, sem mudar o
workspace testado; a integração seguinte a restaura junto com ENDX.

Ensaio 63: IOP `0x66DA0` / 46.555.418, três instruções após a limpeza,
recusou a cópia SH do readback ENDX por conter bits reservados desconhecidos.
EE `0x81FCC` / 360.000.000, 106,22 s; mesmo hash/pacotes/pixels e sem PMODE.
Corrigido o consumo de incerteza: ENDX limpa no ato da escrita e não usa o
payload, portanto a cópia não precisa inventar bits reservados. Regressão
própria copia LHU→SH nos dois bancos/cores. Três testes SPU2 focados passaram;
ensaio 64 iniciado. Modo BIOS original repetido após KON/ENDX: PCs, contagens,
zero pacotes/pixels e hash canônico `cead2f7c...def9c354` preservados.

Ensaio 64: limpeza/cópia ENDX passaram; IOP `0x603A8` / 46.559.703,
LHU de EEA core 0 (0x1F90033C) antes de escrita conhecida. EE `0x81FCC` /
360.000.000, 98,6717 s, 22 stalls; mesmo hash RAM, 16 pacotes, sem PMODE.
O driver transforma EEA em endereço final da área de efeitos; não é apenas
preservação de bits. PS2Recomp não oferece o reset desse MMIO. PS2SDK libsd
programa E/F na inicialização completa; PCSX2 inicia essas mesmas áreas.
Essas referências não medem o reset deste console/perfil.

Próximo diagnóstico acrescenta valores iniciais EEA E/F, incluindo reservados
zero, como hipótese explícita selecionada antes da execução. A API exige
valores de quatro bits por core; não aceita mudança após começar. Nenhum
valor padrão foi adicionado: reset remove a hipótese, escrita convidada a
supera, e outros registradores/reservados posteriores continuam desconhecidos.
Não se trata de prova de efeitos, reset físico ou boot original concluído.

Ensaio 65: hipótese inicial EEA permitiu avançar 70.794 instruções IOP;
novo PC `0x603B0` / 46.630.497, SLL de um readback EEA já escrito pelo guest,
com reservados desconhecidos. EE `0x81FD0` / 360.000.000, 109,055 s, 22 stalls;
mesmos pacotes/pixels, sem PMODE. A hipótese inicial não foi aplicada de novo
após a escrita. Modo original repetido conserva PCs/contagens e hash canônico.

Correção genérica seguinte: SLL propaga conhecimento bit a bit, insere zeros
baixos e só descarta a incerteza deslocada para fora dos 32 bits. SLLV/SRLV
compartilham a semântica, consumindo somente os cinco bits baixos da contagem.
PS2Recomp `ps2xIOP/src/emulator/core/iop_cpu.cpp` consultado; código BSD próprio,
sem incorporar a implementação GPL. Fixtures exercitam todos os deslocamentos,
aliases, variável com bits altos desconhecidos, consumo posterior e load delay.
CI `04d26de` e `1556c86` concluída com sucesso no núcleo/launcher, seis alvos.
A regressão da sobreposição `9150a3c` falhou no núcleo e foi reparada em `1556c86`.

RAM EE do ensaio 65: `774ccf4ff6dac8da93b7367fc8a5738804bb36e5d7d3c03a6554ea2b24bc61c0`.

Ensaio 66: SLL passou; IOP `0x669D8` / 46.630.506, SUBU do endereço EEA
menos o tamanho de efeitos. EE `0x81FD0` / 360.000.000, 107,865 s;
RAM/GIF/PMODE iguais ao 65. SUBU/ADDU agora preservam o prefixo baixo conhecido:
carry/borrow só propagam para cima, sem permitir inferir os bits altos.
Operandos inteiramente conhecidos mantêm caminho direto; x-x é zero exato.
Testes próprios cobrem limites 1/4/16/21/31 bits, carry, borrow, aliases,
wrap, load delay e recusa posterior quando uma comparação exige bits ausentes.
O patch SLL inicialmente não entrou em `2a6445a` por uma quebra de linha na
seleção parcial; `5965b89` publicou imediatamente o código validado, sem
reescrever histórico. CI da cópia ENDX `91d0197`: ambos workflows aprovados
nos seis alvos. As execuções locais sempre usaram o código efetivamente testado.

Ensaio 67: SUBU passou; IOP `0x6672C` / 46.630.522, SH do alto de ESA
0x1F9002E0 com os quatro bits de endereço conhecidos, reservados desconhecidos.
EE `0x81FD0` / 360.000.000, 106,489 s; RAM/GIF/PMODE iguais ao 66.
Escritas ESA/EEA agora consomem somente seus campos: alto de quatro bits,
baixo ESA de 16 bits. Não convertem reservados desconhecidos em números.
Fixture própria percorre EEA→endereço em bytes→SUBU→ESA nos dois cores,
sem hipóteses iniciais, usando EEA explicitamente escrito pelo convidado.

Ensaio 68: ESA passou; IOP `0x55248` / 46.630.819, escrita zero no volume
master atual MVOLXL (0x1F900770). EE `0x81FD0` / 360.000.000, 103,358 s;
RAM/GIF/PMODE iguais ao 67. PCSX2 relata verificação física de que escritas
MVOLX não alteram o volume; PS2SDK confirma mapa, PS2Recomp áudio consultado.
Implementação própria trata apenas tentativas halfword como ignoradas, sem
criar readback. Coeficientes IIR/COMB/WALL/APF/entrada podem ser configurados
quando efeitos estão explicitamente desabilitados. Não executa reverberação,
não habilita efeitos, não muda o volume atual nem inventa amostras.
O push SSH deixou de autenticar; a sessão existente da mesma conta no GitHub
CLI permitiu publicar `ab13949` por HTTPS, preservando origin e os commits.

Ensaio 69: tabela MVOLX/coeficientes passou; IOP `0x64CE8` / 46.648.926,
LHU de ENVX voz 0/core 0. EE `0x81FD0` / 360.000.000, 99,8434 s;
mesmo hash/pacotes e sem PMODE. O guest já havia emitido KOFF com ADSR2=0.

Implementado limite superior de envelope, não um nível fictício: após um
KON aceito, a amplitude ADSR alcançável está entre 0 e 32767. KOFF é consumido
na borda de amostra; release linear com shift 0..11 subtrai 8<<(11-shift) por
amostra. O limite converge a zero em tempo finito; somente então ENVX=0 e
mudança de ganho são permitidos. Novo KON invalida a prova, e ataque audível
continua recusado. Modos exponenciais/lentos ou mudança para esses modos
não recebem um prazo inventado. ENDX/NAX/LSAX/ADPCM continuam desconhecidos.
PS2Recomp áudio consultado previamente; taxa/envelope conferidos na descrição
PSX-SPX e referência comportamental PCSX2 ADSR, sem código incorporado.

Ensaio 70: ENVX após release foi provado e lido pelo guest; IOP avançou a
`0x6575C` / 46.715.096, ATTR=0xC080 habilitando efeitos core 0. EE `0x81FD0` /
360.000.000, 102,857 s; RAM EE SHA-256
`40644d20191c124d5372260960d854d71136adf0804280d9fa44e118887145d4`.
Ainda 16 pacotes/limpeza, sem PMODE. Original repetido após aritmética/release:
mesmos PCs, contagens, zero pixels e hash canônico.

Próxima fronteira: efeitos ativos com saída matematicamente zero. A habilitação
só pode prosseguir com EVOLL/EVOLR conhecidos zero, área ESA/EEA conhecida,
ambos comparadores IRQ desligados e nenhuma transferência concorrente.
O conteúdo de toda a área potencialmente escrito pelo DSP perde conhecimento,
sem zerar os bytes ou afirmar que a reverberação foi calculada. Ganho audível,
IRQ, área desconhecida/sobreposta aos buffers dinâmicos e arbitragem PIO/ADMA
recusam. Desabilitar efeitos não recupera dados de RAM já tornados desconhecidos.
Isso é execução conservadora de estado parcial, não implementação de áudio
reverberado. Referências comportamentais SPU2 já consultadas, código BSD próprio.

Ensaio 71: core 0 habilitou efeitos com saída provadamente zero; core 1 parou
em `0x65768` / 46.715.099 por ESA ainda desconhecido. EE `0x81FD0` /
360.000.000, 99,1884 s; RAM/GIF/PMODE iguais ao 70. Seis testes focados
(SPU2 controle/PIO/ADMA, SPDIF, boot e conhecimento IOP) passaram.
`8bdb0ca` e `6d49893`: núcleo e launcher aprovados nos seis alvos.

Próximo experimento: ESA interno inicial EFFF8/FFFF8, como na referência
comportamental, mas sem medição do console. Opção separada, desativada por
padrão e removida no reset; não fornece readback. Cada metade escrita pelo
guest substitui a parte correspondente da hipótese. Endereços inválidos,
mudança tardia e leituras não comprovadas recusam. PS2Recomp áudio consultado;
a referência de inicialização SPU2 não é prova de valores físicos de reset.

Ensaio 72: ambos cores habilitaram efeitos; IOP `0x60388` / 46.731.259,
delay slot SH EEA=F com efeitos ativos. EE `0x81FD4` / 360.000.000,
97,2465 s, RAM `9a2dbb6f82838a989fea2f4e3b089f59f809b54ec328b624dcefea19cab48d90`.
Nenhuma escrita PMODE ou novos pixels. Modo original repetido após ESA inicial:
mesmos PCs/contagens/hash canônico. Configuração ativa de ESA/EEA agora valida
primeiro a área prospectiva e invalida conhecimento de toda a área alcançável;
não recupera bytes anteriores, não executa DSP nem altera amostras com ganho zero.
Sobreposição com buffers dinâmicos, wrap e desalinhamento continuam recusados.

Ensaio 73: EEA ativo passou; IOP `0x8B78` / 46.735.070, BNE de argumento
carregado da pilha `0x1F4A24` por LW em `0x8B44`, ainda desconhecido.
EE `0x81FD4` / 360.000.000, 96,7716 s;
RAM `1acb50614373c149ee1880529660193dba9aaf9379e0a7294a7846cc6cd2bf5a`.
16 pacotes e limpeza, sem PMODE. Próximo ensaio rastreia a proveniência da
pilha com passos pequenos somente na janela relevante; o trace fica local.
CI de `6693ca7`: núcleo e launcher aprovados nos seis alvos.

Ensaio 74 com cópias IOP preservando bytes conhecidos: BNE passou; DIVU em
`0x8B94` / 46.735.073 recusou o argumento parcial. EE `0x81FD4` / 341.463.896; 100,673 s, mesmos RAM/GIF.
Trace local confirmou EEA lido em `0x603A8`, convertido em endereço, usado
pelo driver e salvo para formatação em `0x1F4A24`. A parte desconhecida vem
dos bits reservados de EEA, não de um número arbitrário na pilha. Nenhuma
mensagem foi pulada ou resposta printf fabricada.

Ensaio seguinte isola outra hipótese: doze bits não usados de ESA-high/EEA
lidos como zero, conforme referência comportamental, sem medição física.
Opção própria antes da execução, desativada/removida no reset; só vale após
escrita conhecida do endereço. Não fornece reset de ESA, não se estende a
SSA, não muda IRQ/DMA/RAM/áudio. Testes mantêm recusas por padrão e após reset.
PS2Recomp IOP/áudio e mapa SPU2 consultados; implementação independente BSD.
`e116c8a`: núcleo e launcher aprovados nos seis alvos.

Ensaio 75: formatação do argumento EEA passou; IOP `0x666C0` / 46.736.104,
SH de zero em 0x1F9002E4, primeiro deslocamento APF. EE `0x81FD4` /
341.472.144, 104,185 s; mesmo hash RAM/16 pacotes, sem PMODE. Original repetido:
PCs 80007D20/80005F00, contagens e hash canônico inalterados.

Configurados agora os halfwords dos 22 deslocamentos de taps SPU2 (2E4..33A)
e coeficientes com efeito ativo de saída zero. Toda a área circular continua
com conteúdo desconhecido; mudar offsets/coeficientes não revalida RAM ou
inventa DSP. Leituras de offset e larguras não estabelecidas recusam. PS2Recomp
áudio e referência comportamental da área circular SPU2 consultados novamente.

Ensaio 76: taps/coeficientes passaram; IOP `0x60388` / 46.740.752,
EE `0x81FCC` / 360.000.000, 113,543 s. Escrita EEA=E do core 1 acontece
antes de ESA ser movido da área F; validação síncrona da escrita recusou a
configuração intermediária. RAM `4e1f70666b406d3b3118d1c61f0951af5b616aed3cb5621db7bc9b93b0f41066`.
Ainda 16 pacotes/limpeza, sem PMODE.

A configuração ativa de ESA/EEA agora marca a área pendente; a próxima borda
modelada de síntese valida e invalida RAM antes de consumir estado de áudio.
Isso substitui a validação imediata descrita no ensaio 72. Stores separados
podem atravessar um estado intermediário inválido entre bordas; se tal estado
alcançar a borda, recusa. Testes diferenciam correção antes/depois da borda,
sem permitir workspace circular desconhecido ou áudio fabricado. A fase
reset-alinhada permanece hipótese já explícita do relógio SPU2.

Ensaio 77: configuração ainda invertida na borda, IOP `0x4710` / 46.741.248;
EE `0x81FC8` / 360.000.000, 101,559 s. RAM
`433532101c80ab10c8d8cbd0a2933d4e8844ad8d592b23507992a740c6579257`.
Sem novos pixels/PMODE. A referência PCSX2 Reverb.cpp trata ESA>EEA como
área inativa, mas isso não é medição física. O próximo ensaio isola essa
hipótese em opção própria antes da execução; defaults/reset continuam
recusando. Área invertida torna-se intervalo vazio apenas nesse experimento,
sem recuperar conhecimento de RAM anteriormente invalidada. Áreas desalinhadas
ou sobrepostas a buffers dinâmicos continuam recusadas. Ganhos de efeitos
continuam obrigatoriamente zero; não é implementação de DSP.
Referência consultada: https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Reverb.cpp
Sem copiar/incorporar código GPL. Busca em documentação pública não forneceu
confirmação física do comportamento ESA>EEA; a incerteza permanece registrada.

Ensaio 78: área invertida experimental passou; IOP `0x66128` / 46.754.814,
configuração TSA baixo=849F. EE `0x81FC4` / 360.000.000, 113,810 s;
RAM `3b80af0282085097ff125390f1104614ef1b8b28c7b8365c93d53b37c73ac4af`.
16 pacotes/limpeza, sem PMODE. TSA agora retém a configuração inteira do
halfword baixo sem iniciar transferência ou arredondar. A recusa de consumo
PIO não alinhado foi movida para seu início. Testes percorrem os oito valores
dos bits baixos nos dois cores, sem fabricar RAM. Referências PS2Recomp áudio,
mapa PS2tek e configuração/DMA SPU2 consultadas; DMA normal ainda não implementado.

Ensaio 79: TSA configurado; IOP `0x66160` / 46.754.845, ATTR core 1=C021
selecionando DMA normal de escrita. EE `0x81FC4` / 360.000.000, 100,838 s;
mesmo hash do 78, sem PMODE/novos pixels.

DMA normal implementado no barramento compartilhado existente: payloads reais
IOP→RAM SPU2, TSA sem arredondamento, MADR/BCR/CHCR e IRQ por término real.
Bytes desconhecidos permanecem desconhecidos na RAM de som; a transferência
não precisa interpretar seu valor. Dois canais dividem um único slot, com
throughput/round-robin explícitos. Gates DMACEN/DPCR pausam sem completar.
Sem DMA de leitura, wrap de payload, escrita em buffers dinâmicos, IRQ de RAM,
PIO simultâneo ou sobreposição com área de efeitos ativa. Uma área de efeitos
disjunta de ganho zero é permitida sob a hipótese de throughput agregado.
O ensaio 80 seleciona 16 ciclos IOP/palavra de 32 bits, não medidos em hardware;
sem a opção, iniciar DMA continua recusado. Seis testes focados passaram,
incluindo payload, bytes opacos, limites, gates, IRQ, conflitos e regressões.
PS2Recomp áudio e comportamento DMA SPU2 consultados; código/testes BSD próprios.

Ensaio 80: 256 palavras reais de 32 bits por DMA normal core 1, além de 16
halfwords PIO. IOP chegou a `0x65D20` / 46.759.272, consultando STATX bit 7
após o payload. EE `0x81FC4` / 360.000.000, 103,276 s; hash RAM igual ao 79.
Ainda 16 pacotes/limpeza de VRAM, PMODE não escrito: nenhum frame exibido.
STATX agora deriva busy do payload pendente e ready do término de uma
transferência normal observada, somente enquanto ATTR mantém modo DMA-write.
Antes de uma transferência observada, ready permanece desconhecido. Testes
alternam polling de CHCR/STATX, dois cores, IRQ ligada/desligada e payload
parcialmente desconhecido; seis testes focados passaram (4,67 s).
Referência comportamental consultada: PCSX2 SPU2/spu2.cpp (notificação de
conclusão); nenhuma implementação GPL incorporada. Timing segue experimental.
CI de `1ef95a1`, `778745c` e `e58bfea`: núcleo e launcher aprovados nos seis alvos.
Ensaio 81: DMA normal core 1 completou 11.008 palavras de 32 bits; IOP
alcançou `0x478EC` / 46.964.935, leitura SIO2 RECV2 (1F808270).
EE `0x81FC4` / 360.000.000, 107,273 s; RAM igual ao 80. Ainda somente
16 pacotes/limpeza, sem PMODE/frame. RECV2/RECV3 agora transportam amostras
inteiramente desconhecidas, sem escolher um valor de reset ou conexão.
Uma decisão que consuma esses bits continua recusada. Fixture própria prova
load/store/load e parada apenas no branch consumidor. Três testes focados
passaram (IOP knowledge, DMA SPU2, boot M20). PS2Recomp ps2_pad.cpp consultado:
backend de controles host, sem semântica MMIO SIO2. Mapa e incerteza de RECV2/3
consultados em PS2tek e PCSX2 Sio2.cpp; sem código copiado.
Repetição do modo original após DMA: ROM0 `0x80007D20` / 17.690.079,
ELF `0x80005F00` / 4.486.087, IOP `0x40B0` / 2.210.789; zero pixels/GIF;
RAM `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
Ensaio 82: 31.584 palavras DMA core 1; EE chegou a `0x112EC0` /
348.197.915 instruções consumindo FIELD (CSR.13), que permanece desconhecido
por padrão. IOP parou em `0x4E570` / 47.389.200, consumindo valor opaco
carregado de 0x506B0 por PADMAN. Tempo 106,650 s; RAM
`886717b3fb2cb95118826bf39f3c31a14ecfb1d41c7e246ec926a8227a5c842c`.
Sem PMODE/frame, ainda 16 pacotes de limpeza. O ensaio 83 seleciona uma
hipótese GS separada: FIELD inicial=1, borda GS-vsync a 7 half-lines NTSC /
6 PAL após VBON. FIELD alterna por bordas atravessadas quando VFP é ímpar;
no experimento analógico com VFP par, assume 1 na borda. VSINT usa essa mesma
fase somente com opt-in; interrupções VBON/VBOF do IOP conservam o relógio
anterior. Sync suspenso preserva FIELD; timing desconhecido invalida seu
conhecimento. Reset/retomada e escritas recusadas são testados. NFIELD e
outros bits não ganham valores. Sem opt-in, modo original fica inalterado.
Sete testes focados GS aprovados; teste sintético não mede fase física.
PS2Recomp gs_frontend.cpp consultado (frontend de comandos, sem FIELD);
PCSX2 Counters.cpp consultado para borda atrasada e condição VFP/CMOD.
Implementação BSD própria; nenhuma cópia/adaptação GPL. A fase aproximada
não é medição do SCPH-70004 e não vira default. CI `8ec7190`: núcleo e
launcher aprovados nos seis alvos.
Ensaio 83: FIELD passou; EE `0x81FD0` / 360.000.000 em espera; IOP manteve
`0x4E570` / 47.389.200. 107,186 s, 31.584 palavras DMA, sem novos pixels/PMODE.
RAM `b850b4d2e979ac203a979e451aaaeaac5e31a8fc72753257c4ab2af891d80394`.
Inspeção local confirma RECV2 deslocado por 4/5, mascarado com 1 e salvo
para escolher controle de temporização PADMAN; PS2SDK padData.c usa os mesmos
campos stat70bit. A referência PCSX2 conserva RECV2=F, mas isso não mede os
bits no console do perfil. Ensaio 84 seleciona apenas RECV2.4/.5=0 em opção
estreita, antes da execução; os outros 30 bits e RECV3 continuam desconhecidos.
Reset remove a hipótese; configuração tardia/valores fora de dois bits recusam.
Testes exercitam as quatro combinações, recusa dos demais bits, reset e RECV3;
IOP knowledge e boot M20 passaram. A opção não configura periféricos, não
cria conexão e não produz respostas seriais. Código BSD próprio.
Ensaio 84: seleção de timing PADMAN passou; IOP `0x47850` / 47.580.787,
início SIO2 sem experimento serial habilitado. EE `0x81FD0` / 360.000.000,
104,214 s; RAM igual ao 83. Sem PMODE/frame, 31.584 palavras DMA SPU2.
Ensaio 85 habilita apenas receptor infravermelho ausente, timeout explícito
8192 ciclos IOP e DMA SIO2 4 ciclos/palavra; os outros periféricos continuam
recusados. Testes SIO2 idle/DMA passaram. O modo original repetido após
FIELD/RECV2 manteve PCs, contagens e hash canônicos, sem pixels.
Ensaio 85: mesmo PC/contagem do 84; FIFO contém dois polls reais de controle,
`01 42 00 00 00`, e a ausência de IR corretamente não responde como pad.
105,522 s, mesmo hash, sem PMODE/frame. Implementado controle digital padrão
selecionado explicitamente por porta: comando 42 de cinco bytes responde ID
41/5A e estado ativo-baixo configurado dos botões; bits sem L3/R3 permanecem
soltos. A resposta percorre FIFO/DMA e só conclui no prazo serial escolhido.
Não há resposta RPC host, alteração do convidado, modo analógico ou comandos
não conhecidos. Topologia contraditória, packet size/comando desconhecido e
mistura de status conectado/ausente ainda recusam. Reset remove o dispositivo.
Ensaio 86 configura pads digitais nas portas 0/1, todos os botões soltos;
8192 ciclos IOP por transação continuam hipótese, não serial timing medido.
Três testes focados passaram: estados de botões distintos, duas portas,
conclusão/IRQ, DMA com bytes reais e padding opaco, configuração e reset.
PS2Recomp ps2_pad.cpp consultado como referência de separação de entrada host;
protocolo digital consultado em https://psx-spx.consoledev.net/controllersandmemorycards/
e contagem de portas/status em PCSX2 Sio2.cpp. Código/testes originais BSD,
sem incorporar GPL. CI `0f05001` e `3050f99` passou nos seis alvos do núcleo
e do launcher.
Ensaio 86: polls digitais completaram; IOP avançou a 48.196.722 instruções,
`0x47850`, agora transmitindo dois `01 43 00 01 00` para detectar configuração.
EE `0x81FD0` / 360.000.000; 108,811 s; RAM
`c2a89a619a2def4a0dc60dea2fe34c785f28f5e0479d505b03a54458cfef9416`.
Sem PMODE/frame; 31.584 palavras DMA SPU2. Controle digital padrão não
reconhece comandos diferentes de 42: DAT permanece sem acionamento e não há
ACK após o comando. A conclusão por timeout devolve os bytes de linha alta
com status de falha por porta; não entra em modo de configuração. Isso é
exercitado no ensaio 87, conservando a duração serial explícita. A identificação
padrão e os botões reaparecem no poll seguinte, comprovados por teste próprio.
Três testes SIO2 aprovados. Consulta adicional comportamental:
https://github.com/stenzek/duckstation/blob/master/src/core/digital_controller.cpp
Sem código copiado, adaptado ou vinculado; implementação/testes BSD próprios.
Modo original repetido após os pads: PCs/contagens/hash canônicos preservados.Ensaio 87: comandos incompatíveis com pad digital terminaram por timeout;
IOP continuou, PC `0x477C` / 48.198.520 instruções, sem parada. EE chegou a
`0x1E39F4` / 353.171.462, CACHE DXLTG. 104,139 s; RAM
`e6e96f95509b7f492a2a5f8ed952ee2c3fa0799d013f36a9834600e498b04374`.
16 pacotes / 1.048.576 pixels de limpeza, sem PMODE/scanout.

Ensaio 88 ativa desde o reset um modelo funcional opcional de D-cache: linhas
com dados e conhecimento próprios, escrita de volta e DMA sem coerência
implícita. Config.DCE/K0 e C da TLB determinam uso; modo 3 suportado,
modo 0 e PREF ainda recusam. Reset remove a opção. TagLo preserva bits
indefinidos; não devolve tag zero para pular a rotina do convidado.
O modelo mantém a hipótese temporal anterior: refill/writeback síncronos,
sem latência adicional ou contenção de RDRAM. Não simula I-cache.
A injeção host descarrega/invalida o cache antes dos segmentos novos,
escritos pelo alias uncached; dumps leem RAM pelo mesmo alias sem alterar tags.
Checkpoint ROM0 idêntico, 32.416.676 instruções em `0x82000`.
O jogo parou antes em CACHE DHWBIN `0x1E55D0` (PC seguinte reportado
`0x1E55D4`), 4.510.892 instruções: alias RAM uncached usado para operação
hit, uso indefinido na especificação. 1,301 s; zero GIF/pixels nesta execução.
RAM `614a120fc246a7513698b334d88e0491e7b6b5cb1d1bd78a8637a541c24408fa`.
Ensaio 89 habilita separadamente a hipótese de traduzir esse alias e procurar
a linha pelo PFN real; descarrega seus dados reais, sem resposta de sucesso
fabricada. Fora dessa hipótese, a operação continua recusada.

Referências comportamentais consultadas antes da implementação: PS2Recomp
`instruction_translator.cpp` em e5636981a1c9de368f6b71e819c3dc188dfedbf4
ignora CACHE, portanto não fornece um modelo de tags; PCSX2 `Cache.cpp`
consultado sem reutilizar seu workaround de DXLTG. Contratos do EE em
https://docs.alexrp.com/mips/ee.pdf (pp.78,86,132–138) e
https://docs.alexrp.com/mips/ee_insns.pdf (pp.298–306).
Cache de 64 conjuntos, duas vias de 64 bytes, PFN físico e LRF; invalidação
preserva PFN/LRF. Tags de reset têm estado zero e PFN indefinido. Código e
fixtures originais BSD; nenhum código GPL, firmware ou trecho comercial
incorporado. CI `938ed35` e `833004c`: núcleo e launcher, seis alvos aprovados.
Ensaio 89: DXLTG e manutenção real do cache passaram; próximo bloqueio EE
`0x108AD8`, início DMA1/VIF1 source-chain com TTE (CHCR=145), após
353.409.922 instruções. A primeira tag real em RAM é END com QWC=0;
não é autorização para simular conclusão das transferências seguintes.
IOP `0xAE94` / 48.228.327 continua sem parada. 101,643 s; sem ganho de
performance alegado (estados finais e modelo de memória diferentes).
16 pacotes/1.048.576 pixels de limpeza; ainda sem PMODE/frame. RAM
`b042f6cb2f0eb92a06c4114a0c8f78f21a76d24540661498edb4751a6b84ddd9`.
Seis testes focados aprovados. Fixture de cache verifica tags/estados,
visibilidade entre aliases, LRF, bits desconhecidos, mapeamento, lock,
instruções COP0 e semântica AOT compartilhada. Um pacote real sintético
GIF/LABEL permanece invisível ao DMA até a escrita de volta e depois
chega ao GS com o valor original. Esse fixture não é frame do jogo.
Modo original repetido após o cache: PCs, contagens e hash canônicos preservados; zero GIF/pixels.

Continuação DMA VIF: consultados PS2Recomp Kernel/Stubs/DMA.cpp e
ps2_vif1_interpreter.cpp no mesmo ref e PCSX2 Hw.cpp (comportamento de TADR).
Código BSD próprio para canais 0/1 normal e source-chain REFE/CNT/NEXT/REF/END.
TTE entrega somente os 64 bits superiores da tag ao parser VIF antes do payload;
não insere NOPs para os bits inferiores. Dados e comandos percorrem a RAM real,
VIF e GIF PATH2. Tag/payload custam um BUSCLK cada, hipótese funcional já usada
no DMA GIF; não há arbitragem física ou latência de memória comprovada.
CNT avança TADR conforme payload consumido, END conserva TADR. D_STAT/STR
mudam após a última transferência, com TIE/IRQ, DMAE, prioridade e suspensão.
Fontes desconhecidas/fora da RAM, resume, CALL/RET/REFS, prioridade de tag,
concorrência VIF/GIF e comandos VU não suportados recusam sem concluir DMA.
Fixtures próprios verificam ambos os canais, tag vazia com prazo, STROW
atravessando tag/payload, encadeamento e DIRECT até o LABEL real do GS.
Seis testes focados aprovados; fixture AOT/direct-frame ainda precisa ser
construído neste diretório isolado antes da sua execução.
CI do cache a08f31d detectou duas declarações DEV9 concorrentes incluídas
sem implementação publicada; d033f16 remove somente essas declarações
publicadas, preservando o trabalho local. CI reenviada para os seis alvos.
Ensaio 90: DMA VIF passou; EE `0x1024C0` / 358.037.955 instruções,
GS recusou profundidade durante sprite (ZBUF PSMZ16S, escrita habilitada).
IOP `0x4AAC` / 48.806.831; 110,572 s. PMODE=66, SMODE2=3 e circuito 2
DISPFB=1400 / DISPLAY=1BF9FF0183227C foram escritos pelo jogo. Scanout
recusa entrelaçamento; ainda 16 pacotes e 1.048.576 pixels de limpeza.
O dump adicional da RAM foi interrompido pela recusa GS persistente ao
sincronizar DMA; não há novo hash de RAM para este ensaio. Não houve frame.
Modo original repetido depois do DMA VIF conserva PCs/contagens/hash canônicos.

Continuação GS (ensaio 91): consultados PS2Recomp `gs_frontend.cpp` e
`ps2_gs_memory.cpp` no ref e5636981a1c9de368f6b71e819c3dc188dfedbf4,
GS User's Manual 6.0 (diagramas de páginas/blocos/colunas, TEST/ZBUF,
SCANMSK e PCRTC) e PS2SDK graph_mode. Referência de comportamento apenas;
implementação BSD independente por permutações de bits e fixtures próprios.
Profundidade Z32/Z24/Z16/Z16S na VRAM, comparação NEVER/ALWAYS/GEQUAL/GREATER
em pontos/sprites e ZMSK. O renderer CT32/24 recusa Z16 incompatível e
profundidades fora da faixa; alpha test, textura e blend continuam pendentes.
INT+FFMD expõe os pixels nativos consecutivos de um campo, sem duplicar
linhas nem inventar o campo vizinho. FFMD=0 permanece recusado.
Ensaio 91: EE `0x1024C0` / 358.037.955, IOP `0x4AAC` / 48.806.831;
109,607 s, sem comparação de desempenho com estados finais diferentes.
Próxima recusa: SCANMSK ainda não escrito. Scanout válido 640x224,
zero pixels não pretos, RGBA SHA-256
`6cf79e2ea48074d85c3879b2e34cd92246b816be78aa13de39d9743ab0f880e9`.
É a limpeza preta do jogo; não é menu ou evidência de gameplay.
Seis testes GS/VRAM/GIF/VIF e frames sintéticos AOT/interpretador passaram.
CI d033f16: núcleo e launcher aprovados nos seis alvos; 0570023 launcher
aprovado nos seis, núcleo ainda em execução na última consulta.
SCANMSK/FBA sem escrita não recebem zero: o renderer propaga a incerteza.
Para SCANMSK desconhecido, combina conservadoramente desenhar/não desenhar;
só bits conhecidos e iguais nos dois resultados permanecem conhecidos.
FBA desconhecido afeta somente alpha bit 31 quando ele não era já 1.
Scanout RGB continua recusando pixels com qualquer bit RGB indeterminado;
escritas condicionais não incrementam o contador de pixels comprovados.
Uma escrita posterior do convidado resolve a incerteza. Nenhuma hipótese de
reset adicional, pacote host ou comando externo foi inserido no jogo.
CI 0570023: os seis jobs do núcleo também terminaram com sucesso.
Ensaio 92 com incerteza preservada: EE `0x1CECA0` / 387.565.264,
IOP `0xD628` / 52.497.745 sem parada; 130,654 s. 24 novos pacotes GIF,
1.048.832 escritas comprovadas (256 entradas adicionais transferidas),
scanout 640x224 ainda preto com o mesmo digest. Próximo bloqueio: upload
PSMT8 208x128, DBP=1680, DBW=4. O erro persistente de DMA impede o dump
secundário de RAM; nenhuma nova identidade de RAM alegada.
Modo original repetido após f6344e4: PCs/contagens/hash canônicos preservados.
Continuação PSMT8: consultado PS2Recomp GS memory e os diagramas GS
8.3.3; implementação independente com permutação de bits, byte lanes,
colunas pares/ímpares e páginas 128x64. Upload IMAGE/HWREG e cópia local
preservam bytes vizinhos e recusam alinhamento inválido. PSMT8 não é
aceito como framebuffer de desenho ou formato de scanout.
Ensaio 93: upload PSMT8 completo, 26.624 índices reais adicionais;
25 pacotes novos, 1.075.456 escritas comprovadas. EE `0x1CC550` /
387.571.287, IOP `0x4CAF8` / 52.498.498 ativo, 126,490 s.
Bloqueio: start do DMA GIF source-chain, CHCR=105. Scanout continua preto.
RAM SHA-256 `4e15e4c972a83f56926a3a337668f17d87a5998c2939680ae91af38bbc05737c`,
um doubleword com bits desconhecidos.
Continuação: tags do GIF usam o mesmo motor source-chain BSD do VIF,
consultado previamente contra PS2Recomp DMA.cpp e os protocolos do EE.
REFE/CNT/NEXT/REF/END e IRQ/TIE transportam bytes reais; TTE no GIF,
CALL/RET/REFS, scratchpad e arbitragem simultânea continuam recusados.
Fixture GIF atravessa REF+END e CNT+IRQ; dados da tag não viram pacote.
Diagnóstico da RAM física é leitura sem efeitos, com máscara de incerteza:
não refaz DMA recusado, não preenche/esvazia cache nem avança dispositivos.
Oito testes focados aprovados. CI f6344e4: núcleo e launcher nos seis alvos.
Ensaio 94: DMA GIF source-chain passou. EE `0x1CECA0` / 392.490.598,
IOP `0x4CB18` / 53.113.412 ativo, 134,160 s. 35 pacotes novos,
1.075.584 escritas comprovadas, scanout ainda preto. Novo upload recusado:
PSMT4 64x96, DBP=16FF, DBW=2. RAM
`51f49fbd4275b5929d60dfc201d0a0d9627dfdfbab27084a533fe8d51ac89155`,
um doubleword parcialmente desconhecido; captura sem avançar DMA.
Continuação PSMT4: consultados os traits P4 de PS2Recomp GS memory e
GS User's Manual 8.3.4. Implementação independente de páginas 128x128,
blocos 32x16, colunas alternadas, seleção de nibble e payload IMAGE/HWREG.
Fixtures verificam bijeção por nibble em quatro páginas, vetores físicos,
preservação de vizinhos, incerteza, alinhamento e padding. Seis testes
focados GS/GIF/AOT aprovados; ensaio 95 em andamento.
Ensaio 95: PSMT4 passou; 88 pacotes GIF novos, 1.819.120 escritas
comprovadas, ainda scanout preto. EE `0x1DD778` / 481.038.854,
IOP `0x4CCBC` / 64.181.944 ativo, 183,966 s. Recusa de PSUBW
0x70433848 numa rotina convidada de comparação de memória/strings.
RAM `f2358e67943f7f0ce52500a595e7a31afccc0325b409e8ba4da6967ec31a74d7`,
um doubleword parcialmente desconhecido.
Consultado PS2Recomp mmi_translation_helpers.cpp no mesmo ref e tabela MMI0
PS2tek antes de implementar PSUBW. Semântica compartilhada AOT/interpretador:
quatro lanes de 32 bits, módulo 2^32, sem carry/borrow entre lanes ou exceção
signed. Testes próprios de limites, alias do destino e zero register;
cinco testes focados de contexto/cache/DMA/frame AOT aprovados.
Ensaio 96: PSUBW passou e o orçamento de 1.000.000.000 instruções foi
atingido sem recusa: EE `0x81FC4`, IOP `0xAE98` / 129.052.087 ativo;
393,907 s. 193 pacotes GIF novos, 1.819.120 escritas comprovadas.
O framebuffer de desenho mudou, mas o scanout 640x224 continua preto,
com o mesmo digest. RAM
`a37033d2bdd36ad6c14c5dd17e0931cd127962dc0624d90946dcaf4c7dc32d26`,
um doubleword parcialmente desconhecido. Nenhuma imagem de jogo declarada.
Ensaio 97 amplia a janela acompanhada para até 4 bilhões de instruções
ou 20 minutos, com captura de até três scanouts distintos e não pretos
no VBLANK modelado. As hipóteses anteriores continuam explícitas.
CI b6ea81f: núcleo e launcher aprovados nos seis alvos. CI 259346e:
launcher aprovado nos seis; núcleo com cinco aprovados e macOS ARM64
ainda em fila na última consulta.
Regressão do modo original repetida após DMA compartilhado, PSMT4 e PSUBW:
ROM0 `0x80007D20` / 17.690.079; ELF `0x80005F00` / 4.486.087;
IOP `0x40B0` / 2.210.789, zero GIF/pixels e RAM canônica
`cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
Ensaio 97: EE `0x81FDC` / 1.260.000.000; IOP `0x47850` /
159.520.606 parou numa sondagem SIO2 de Memory Card (`81 11`), pois
a presença do periférico nessa porta não estava estabelecida. 551,305 s,
246 novos pacotes GIF, 1.819.120 escritas e scanout ainda preto.
RAM `a0b3e17bb39ec7f51e54af0372e72369eb69f079035926bcee0965482a47259a`,
um doubleword parcialmente desconhecido. Nenhum frame não preto capturado.
Ensaio 98 configura explicitamente os slots de Memory Card vazios no
harness, usando a API de portas desconectadas já existente (máscara 12,
timeout 8192). Consultado PS2Recomp mcserv: usa serviços HLE, não adotados.
Três testes SIO2 pad/DMA/idle aprovados; nenhum save montado ou modificado.
CI 259346e: núcleo e launcher aprovados nos seis alvos.
Ensaio 98: portas vazias passaram. EE `0x108AE0` / 1.263.217.757,
IOP `0xB4D0` / 161.954.306 ativo, 512,774 s. Recusa VIF1 FLUSH
(`0x11`); 248 pacotes novos e scanout ainda preto. RAM
`99860af9ab8f84a4523484ef213acb721e51af3321a49690808bf99252d8bf67`,
um doubleword parcialmente desconhecido.
Consultados PS2Recomp VIF interpreter e EE User's Manual FLUSH/DIRECTHL.
FLUSHE aceita o estado Ready comprovado (todos os inícios micro continuam
recusados); FLUSH verifica PATH2 e não aguarda PATH3. Esperas de caminho
ocupado continuam recusadas. DIRECTHL usa os bytes reais de PATH2 somente
com PATH3 ocioso, sem descartar pacotes ou inventar execução VU.
Quatro testes VIF/GIF/AOT/setup aprovados, incluindo payload parcial,
timing de END, caminhos ocupados e rejeição dos comandos exclusivos no VIF0.
Ensaio 99 confirmou FLUSH/DIRECTHL: EE `0x1CE460` / 1.263.225.943,
IOP `0xAE98` / 161.955.330 ativo; 534,413 s, sem alegação de desempenho
(compilação concorrente durante parte desse ensaio). 255 pacotes novos e
1.948.160 escritas comprovadas. Bloqueio: sprite UV texturizado/blended,
PRIM=156, PSMT4, CLUT CT32 CSM1, filtro nearest, MODULATE e alpha 44.
Scanout continua preto; RAM com o mesmo digest do ensaio 98.
Replay local da cadeia isolou o mesmo comando, mas usa estado GS/VRAM
incompleto e não constitui execução do jogo ou evidência de frame.
Continuação GS: consultados novamente PS2Recomp gs_frontend/gs_cpu_backend
e GS User's Manual (CLUT, UV, texture functions e alpha). Implementação BSD
independente de sprites UV com passos exatos em 10.4, nearest nível zero,
CT32/PSMT8/PSMT4, CLUT CSM1 CT32 copiada em TEX0/TEX2, MODULATE/DECAL,
wrap e alpha blending inteiro. FBA, máscara de framebuffer, scissor e
profundidade continuam compartilhados. O renderer recusa slopes UV cuja
precisão DDA não está estabelecida, cache obsoleto/feedback, outros formatos,
filtros e funções. Nenhum float host ou resposta gráfica fabricada.
Oito testes focados aprovados: paleta persistente após alteração da origem,
TEX2, índices 4/8 bits, UV escalado, alpha, arredondamento negativo, recusa de
cache e regressões GS/GIF/VIF/AOT. Modo original repetido: mesmos PCs,
contagens e RAM canônica `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
Ensaio 100, ainda sem hipótese SCANMSK: sprites executados, 265 pacotes novos,
2.077.200 escritas comprovadas por transferências. A máscara não escrita
torna os desenhos condicionais e propaga incerteza também à profundidade.
EE `0x1CE468` / 1.268.129.288 recusa leitura de profundidade indeterminada;
IOP `0x47860` / 162.568.248 ativo; 536,405 s (build concorrente, sem comparação
de performance). Scanout recusado por bits desconhecidos, nenhum frame.
RAM `7863c1c16e8123c62dab0e1ce3a2aadd573c43b5dee2095d501f089e9b4e4aa1`,
um doubleword parcialmente desconhecido.
Ensaio 101 usa SCANMSK=0 como hipótese explícita de reset, coerente com
PS2Recomp GS::reset, não como medição de hardware. API restrita à configuração
anterior à atividade convidada; reset do perfil normal descarta a hipótese.
Testes verificam desenho real sob a opção, reset e preservação do modo normal.
Nenhuma alteração de SCANMSK foi inserida nos comandos ou no ELF do jogo.

### Primeiras capturas reais — ensaio 101

O jogo exibiu sua própria tela de Memory Card (PS2) ausente. Três capturas
no VBLANK contêm 64.478 pixels RGB não pretos cada. São campos nativos
640x224 de INT+FFMD, sem duplicação de linhas ou deinterlace inventado.
Os comandos vieram do jogo por DMA/VIF DIRECTHL/GIF; CLUT, textura e
sprites/blending produziram os pixels. A configuração de slots vazios é
explícita, e nenhum Memory Card ou save do usuário foi criado ou modificado.

| Captura | Ciclo EE | SHA-256 RGBA8 |
|---|---:|---|
| 1 | 1.305.433.587 | `9257654792aa082dfee0ff24fd8e8d1f8d650d2a52c8be2be814d03ad34b8290` |
| 2 | 1.310.352.837 | `8af7db8a2ee95d10fd4a4ebb3242109ad0fcc6f1b0709f5c07e9dc9185f91e34` |
| 3 | 1.315.272.087 | `9257654792aa082dfee0ff24fd8e8d1f8d650d2a52c8be2be814d03ad34b8290` |

As capturas 1/3 coincidem; a segunda difere com a fase de campo do desenho.
Arquivos locais `out/game-first/superbust-vblank-{1,2,3}.bmp` e conversões
PNG sem alteração de dimensão/pixels; inspeção visual das capturas 1/2
confirma texto e painel do jogo. Não são extrações avulsas de textura,
limpeza preta, replay parcial, imagem gerada ou frame sintético.

Parada voluntária do harness após as três capturas, no limite do chunk:
EE `0x81FDC` / 1.300.000.000, IOP `0xAE94` / 166.552.087 ativo,
nenhuma recusa. 326 pacotes GIF novos e 30.892.656 escritas de pixels/índices,
incluindo limpezas repetidas e desenhos agora conhecidos. 554,279 s;
não é velocidade de tempo real nem comparação de desempenho.
RAM SHA-256 `0390138ea61f0f91fd5bb337f5ee2da6b432dbd894217d4a9da02ede736ca032`,
um doubleword parcialmente desconhecido, não consumido pelo scanout.

Oito testes focados GS/GIF/VIF/AOT passaram. CI `d7588b0` e `767fc94`:
núcleo e launcher aprovados em Windows x64/ARM64, Linux x64/ARM64 e
macOS Intel/Apple Silicon. O modo BIOS original continua sem a hipótese
SCANMSK; a regressão preservou PCs, contagens e RAM canônicos.

Limite: estas capturas provam a apresentação gráfica do jogo na execução
experimental, com as hipóteses registradas e injeção do ELF. Não provam
boot original de disco, menu principal, gameplay, áudio ou fidelidade total.
