# Próxima execução — Super Bust-A-Move

## Fronteira atual e retomada rápida

**Parado a pedido do mantenedor.** Não iniciar outro ensaio sem novo pedido.
Últimos ajustes concluídos: identidade numérica entre cópias e transporte
de proveniência por cache/writeback/refill. Quatro testes focados passaram;
sem avanço na fronteira real. CI final pendente; `cad88c0` passou no núcleo
e launcher nos seis alvos. Nenhum ensaio ou observador deve ficar ativo.

Usar `out/game-first/superbust-final-safe.ps2ncp`: checkpoint completo
verificado em `0x80002ACC`, 7.759.586.676 instruções totais,
7.759.588.054 ciclos EE, 7.727.170.000 instruções do jogo. Carrega em
0,859 s na última verificação, apenas 2.060 instruções até o bloqueio.
Preservar o build compatível em `out/replay-build`, fontes fixadas em
`out/replay-source-78973bb` (`cad88c0` mais ajustes finais), com hashes em
`superbust-final-build-provenance.json`. A `main` continua recebendo outras
mudanças; revisar esquema e recompilar tudo junto antes de trocar runtime.
Não repetir desde o boot para diagnosticar essa recusa.

**126 concluído**, com hipótese genérica de carry escalar explícita,
somente após provar que o prefixo completo de 7,125 bilhões tem zero
origens numéricas incertas. Cenário separado `499f9eaf33cc3468a1fa1e189bde88cbfd152fb8405511be2c363fe93ce5b493`.
Log `superbust-menu-126.log`; entrada `superbust-pad126.txt` solta (`FFFF`).
Terminou na mesma recusa em 292,052 s; não demonstrou ganho. O 127,
estrito e com identidade corrigida, também reproduziu a recusa. A última
verificação limitada salvou o checkpoint final acima e encerrou.

O 125 voltou à mesma recusa, Cross por 35 campos sem mudança visual.
A escrita inicial foi isolada em `SW 0x1CD120` / 7.727.167.542 do jogo;
`superbust-before-unknown-xy.ps2ncp` para uma instrução antes, e
`superbust-coordinate-prefix7727.ps2ncp` para 172.060 antes do DMA.
O domínio conserva 32 coordenadas Y distintas; não escolher bits zero.
Se necessário, esses estados reproduzem o caso sem a execução longa.

Checkpoints anteriores às novas flags escalares têm migração explícita,
com opt-in de runtime e SHA-256 dos campos antigos revalidado. Experimento
escalar desativado; ativação tardia recusada. Sete testes focados passaram.
O 123 executou de seis bilhões até 6,325 bilhões e salvou seu estado;
125 retomou desse checkpoint e testou a prova estrita antes dos cálculos
gráficos. CI `78973bb` e `052ae60` aprovada nos seis alvos aplicáveis.

Recusa atual: DMA gráfico VIF1 lê XYZ2 incerto em payload REGLIST;
EE reportado `0x1CE460`, 7.727.172.060 do jogo, IOP ativo `0xB46C`
/ 969.948.764. Tag conhecida em `0xF70C00`, MADR `0xF70C20`;
`0xF70C28` tem máscara `0x00FF0000` na coordenada Y, outras coordenadas
também incertas. Não substituir pelos bits zero do hull ou ignorar o DMA.
Preservar proveniência por cache/writeback/DMA e provar os efeitos gráficos,
ou completar a precisão aritmética antes de consumir esses bits.

**122 concluído**, continuação de 7,4 bilhões com Start aplicado antes
da animação. Pressionado em 7.434.819.087 ciclos, solto em 7.587.315.837:
31 campos. Os 57 frames comparáveis com o 117 tiveram hashes idênticos;
retornou à mesma recusa, sem menu/resposta visual demonstrados.
Log `superbust-menu-122.log`, entrada `superbust-pad122.txt` solta (`FFFF`).
Nenhum ensaio ou observador ficou em execução. Ainda faltam menu,
Up/Down/Cross e partida. Para reconstruir certificados já descartados
antes do checkpoint de fronteira, há também `superbust-menu117-latest.ps2ncp`
em 7,725 bilhões, anterior aos flushes do pacote atual.
Dados completos em [FIRST-GAME-STATUS](FIRST-GAME-STATUS.md).
Revisão de código `9113b7a`: sete testes focados e BIOS original passaram;
CI do núcleo e launcher aprovada nos seis alvos. Último registro documental
não altera código nem exige repetir essa matriz.

## Histórico imediato

Pedido ativo: continuar do aviso de Memory Card até o menu principal e
comprovar resposta visual aos controles e iniciar partida. **110 terminou**
em `0x1CE45C` / 7.284.393.384 por precisão DDA de UV triangular no GS.
IOP ativo `0xAE98` / 914.601.294. Superou as recusas VU do 108/109,
mas só aviso, créditos e Acclaim foram observados; scanout final preto.

**111 concluído; 112 retomado por checkpoint**, novas ferramentas locais e núcleo em
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
Checkpoint completo disponível: `out/game-first/superbust-live-7200.ps2ncp`
preserva 7,2 bilhões do jogo (7.232.416.676 totais), PC `0x1D2F0C`.
Carregamento real em 0,857 s, duas continuações de 1.000 instruções com
mesmo estado completo. **Não repetir o prefixo desde o boot.** Restaurar
com os mesmos BIOS/ROM1, hardware, NVM e disco verificados; recompilar
núcleo/AOT/harness juntos e autorizar explicitamente alteração de runtime
quando a correção preservar o esquema. O 111 acabou em CVT.W.S
`0x186DFC` / 7.323.722.395 do jogo, IOP ativo `0x47A40`.
O backing de RAM antigo continua insuficiente para retomada.
O 112 superou CVT por prova de resultado inteiro único e encontrou DIV.S
`0x186E60` / 7.323.722.420, IOP ativo `0x47888` / 919.517.436.
Usar `superbust-menu112-latest.ps2ncp` (7,3 bilhões do jogo), não o estado
parcial após a recusa. Faltam cerca de 23,7 milhões até a fronteira atual.
Diagnóstico em `superbust-menu112-diagnostic`; entrada manual
`superbust-pad112.txt`. A retomada anterior levou 60,72 s até a nova recusa.

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
Comandos anteriores em [DEV-TOOLS](DEV-TOOLS.md); o limite de checkpoint
daquele relatório foi superado por `ps2n-checkpoint inspect`, pela API
`save_checkpoint/load_checkpoint` e pelos runners com `--resume=FILE`,
`--checkpoint-out=FILE`, `--checkpoint-every=N` e
`--checkpoint-scenario=SHA256`. Captura de processo legado Windows x64
requer `ps2n-capture` e adaptador de salvamento com ABI/imagem idênticas;
não aplicar adaptador de outra revisão. Evidência no relatório do jogo.
