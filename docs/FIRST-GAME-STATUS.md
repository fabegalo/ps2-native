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
