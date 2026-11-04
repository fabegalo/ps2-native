# Próxima execução — Super Bust-A-Move

Pedido ativo: continuar do aviso de Memory Card até o menu principal e
comprovar resposta visual aos controles e iniciar partida. **110 terminou**
em `0x1CE45C` / 7.284.393.384 por precisão DDA de UV triangular no GS.
IOP ativo `0xAE98` / 914.601.294. Superou as recusas VU do 108/109,
mas só aviso, créditos e Acclaim foram observados; scanout final preto.

**111 em execução**, novas ferramentas locais e núcleo em
`out/dev-tools-build`. Núcleo, cabeçalhos, AOT e auxiliares recompilados
juntos, com hashes registrados em `superbust-menu111-build-provenance.json`.
Log `out/game-first/superbust-menu-111.log`; entrada `superbust-pad111.txt`,
captura `superbust-capture111.txt`. `ps2n-debug` confirmou a recusa anterior,
`ps2n-analyze` analisou o ELF, e o novo `DiagnosticRecorder` registra um
anel de 512 eventos após 7,28 bilhões em `superbust-menu111-diagnostic`.
Hipótese **explícita** UV triangular racional em 10.4; a precisão física
não está comprovada e o modo de referência mantém a recusa. Seis testes
focados e BIOS original passaram. Continuar até menu/navegação, corrigindo
cada nova recusa e registrando o PC. Não afirmar sucesso com replay,
limpeza de framebuffer ou hashes alternados do mesmo Acclaim. Manter as
hipóteses experimentais do diagnóstico explícitas e o modo BIOS original.
Evidências e identidades em [FIRST-GAME-STATUS](FIRST-GAME-STATUS.md).

Entrada local ativa baixa: `FFFF` solto, `BFFF` Cross, `FFF7` Start,
`FFBF` Down, `FFEF` Up. `10003` reproduz Cross 60 campos, solto 30,
depois Start 30 ligado/90 solto. Cancelar com `FFFF` após a abertura e
aplicar entradas manuais no menu. `10000` para no próximo VBLANK conhecido
em idle verificado. Capturas locais, nenhuma escrita host em RAM convidada.
Diagnóstico limitado a 90 minutos/16 bilhões e sempre supervisionado.
Captura econômica por padrão; habilitar todos os campos perto da fronteira
7,18 bilhões. Não consumir as 256 capturas com Acclaim alternado.
Sem save-state completo: backing de RAM não restaura cache/dispositivos.

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

## Ferramentas disponíveis para a investigação corrente

Usar `ps2n-dev init/run/replay` para perfis locais novos, e `ps2n-debug log`
para resumir os logs existentes sem repetir a execução. O build gerenciado
é exclusivo por perfil; preservar os builds/processos ativos do jogo.
Comandos e garantias em [DEV-TOOLS](DEV-TOOLS.md). Checkpoint completo
continua indisponível; backing de RAM não é uma retomada válida.
