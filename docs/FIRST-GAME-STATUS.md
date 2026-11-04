# Primeiro jogo comercial — execução parcial, sem jogabilidade

Execução local em 03/11/2026. **A meta de jogabilidade não foi atingida.** O ELF original executa;
inicialização normal, primeiro frame, menu, controles e gameplay não foram demonstrados.
Código original BSD-3-Clause; nenhum código de emulador, firmware, IRX ou jogo foi incorporado.

## Seleção e identidade

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

## 04/11/2026 — Super Bust-A-Move (SLUS-20115)

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
