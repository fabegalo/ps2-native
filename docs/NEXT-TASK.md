# Próxima execução — jogos reais

Pedido atual: continuar Super Bust-A-Move do aviso de Memory Card até o
menu e comprovar resposta aos controles. SIO2 agora aceita press/release;
Cross chegou aos buffers EE sem escrita host em RAM. O ensaio 103 passou
o redesenho do aviso, mas parou em `0x1CE454` / 3.781.852.170, teste de
alpha GS. ATE/AFAIL implementados, oito testes focados passaram; executar
`out/game-first/superbust-menu104-run.cmd` após build coerente para passar
essa fronteira e investigar as seguintes. Capturar apenas pixels reais.
Entrada supervisionada em `out/game-first/superbust-pad.txt`, palavra
hex ativa baixa; `FFFF` solto, `BFFF` Cross, `FFF7` Start, `DFFF` Circle;
`10000` encerra voluntariamente no próximo VBLANK conhecido.
O harness 104 repete as entradas do 103 até o bloqueio antigo e depois
aceita o arquivo. Limite 60 minutos/8 bilhões, processo sempre acompanhado.
Usa AOT local do laço do mesmo kernel, bytes/ROM0 conferidos; em 2,54
bilhões o 103 reproduziu PCs/RAM do 102 em 29,2% menos tempo. Nenhum
save-state completo disponível. Hipóteses do diagnóstico permanecem
explícitas. Evidência em FIRST-GAME-STATUS, sem menu ainda demonstrado.

## Checkpoint preservado — Simpsons

Continuação de The Simpsons: Hit & Run (SLUS-20624), ensaio 5: EE
`0x80005F00` / 93.117.072, IOP `0x25E34` / 15.691.717, SCMD 08 ReadRTC
não modelado. Investigar fonte/estado do RTC, formato BCD e transporte
da resposta antes de implementar; não inventar relógio ou sucesso.
MUL.S inicial, leitura de dados em DVD e descarte de FIFO vazia passaram.
576/448 bytes SIF1/SIF0, zero pacotes GIF/pixels/frames. Quatro testes
focados e regressão BIOS original passaram. Manter hipóteses do harness
explícitas; consultar PS2Recomp antes de implementar subsistemas.
Logs `out/game-first/simpsons-trial-{2,3,4,5}.log`; relatório
[primeiro jogo](FIRST-GAME-STATUS.md#reensaio-de-simpsons--04112026).

## Checkpoint preservado — Super Bust-A-Move (SLUS-20115)

O objetivo de capturar pixels reais do jogo foi atingido no ensaio 101:
três capturas no VBLANK, campos nativos 640x224, mostram a tela original
de Memory Card ausente. Não há prova de menu principal ou gameplay.

Próximo trabalho, se solicitado: continuar a partir desse diagnóstico até
menu e gameplay, exercitando entrada real do controle e investigando os
próximos bloqueios. Não substituir a tela do jogo por interface host nem
fabricar sucesso de Memory Card/RPC. Preservar os saves reais do usuário.

Último estado: EE `0x81FDC` / 1.300.000.000 instruções; IOP `0xAE94` /
166.552.087 ativo, sem recusa. O harness parou voluntariamente após três
capturas, não por bloqueio desse PC. 326 pacotes GIF novos; RAM SHA-256
`0390138ea61f0f91fd5bb337f5ee2da6b432dbd894217d4a9da02ede736ca032`.

Harness local: `out/game-first/gs-iop-experiment-probe.cpp`; build isolado
`out/gs-iop-build`; log `out/game-first/gs-iop-experiment-101.log`.
Capturas `out/game-first/superbust-vblank-{1,2,3}.bmp` e `.png`.
O limite atual é recusa, 4 bilhões de instruções, 20 minutos ou três
scanouts consecutivamente distintos não pretos. Para continuar além da
tela já capturada, ajustar apenas esse critério de parada do diagnóstico.

Execução experimental por injeção de ELF, com hipóteses explícitas de
encoder/SYNCV/FIELD, fase GS, SCANMSK=0, F510/SBUS, IRQ/DMACINTEN,
timing/cache/SPU2/SIO2/CAV. Pads digitais 0/1 com botões soltos; slots
de Memory Card 2/3 explicitamente vazios, timeout 8192. Nenhum save
montado. ROM1 vinculada aos hashes/perfil; ROM2/EROM não fornecidas.
Não transformar hipóteses em defaults nem misturar firmware.

O modo original preserva ROM0 `0x80007D20` / 17.690.079, ELF injetado
`0x80005F00` / 4.486.087 e IOP `0x40B0` / 2.210.789; RAM canônica
`cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.

Consultar PS2Recomp antes de implementar subsistemas, sem copiar/derivar
código GPL. Oito testes gráficos focados passaram; `d7588b0` e `767fc94`
aprovados nos seis alvos de núcleo e launcher. Evidência e limites em
[FIRST-GAME-STATUS](FIRST-GAME-STATUS.md#primeiras-capturas-reais--ensaio-101).
