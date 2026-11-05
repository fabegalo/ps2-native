# Próxima execução — Super Bust-A-Move

## Fronteira ativa — menu e AA1, 05/11/2026

O KON audível foi superado por síntese real no SPU2 existente. O diagnóstico
alcançou o título e o GAME MENU original: Start abriu o menu, Down selecionou
CPU BATTLE e Up retornou a 1P PUZZLE. Cross iniciou a transição para a partida.
**Primeira partida e gameplay ainda não demonstrados.** Não trocar de título.

Bloqueio atual: GS triangle fan, PRIM 0xC5 (197), AA1=1, ABE=1, FGE=0,
IIP=0. Recusa em EE 0x1CE458, 12.334.693.440 instruções totais /
12.302.276.764 do jogo, 12.334.708.186 ciclos EE. IOP 0xAE94,
contador 1.541.838.523, sem recusa. AA1 exige cobertura de borda, seleção
de alfa e controle de escrita Z; não aceitar simplesmente remover esse bit,
ignorar os triângulos ou declarar a partida iniciada pelo contador.

Retomar de `out/game-first/superbust-before-game-shading.ps2ncp`:

- EE 0x1CE45C; 12.334.692.440 instruções totais / 12.302.275.764 do jogo;
  12.334.707.186 ciclos EE. IOP ativo 0xAE98, contador 1.541.838.398.
- Somente 1.000 instruções até a recusa. Payload SHA-256
  `d36bca69377f93e139b67c4de8ca86d3f672165738de065470b34f81bcb74e5a`.
- Esquema `4d32f9c6e62746ff39398d75d0efb1ae80a7a653a7abb8fb4f4c3a6933ede047`;
  cenário `69cd4ee9ac13c10fb6234909b0bb543c105ec2bffda9fae62901fedda16c43bb`.
  Esse cenário inclui as hipóteses anteriores, UV racional de sprites e
  upload linear desalinhado (modo GS 13); não é boot original validado.
- Preservar os pontos anteriores, ROM0/ROM1, console, NVM e disco fixados.
  Troca de runtime exige revisão e opt-in explícito. Recompilar núcleo,
  AOT e auxiliar juntos: os cabeçalhos inline VU fazem parte do executável AOT.

Próximo incremento:

1. Isolar os vértices e a configuração completa desse fan a partir do ponto
   seguro. Implementar AA1 genericamente a partir da especificação, incluindo
   alfa/cobertura, testes e Z somente nos pixels de cobertura completa.
   Precisão de borda não comprovada deve permanecer hipótese identificada;
   preservar recusa no modo original enquanto faltar prova.
2. Retomar com limite e supervisão após cada correção. Salvar apenas estado
   válido do sistema inteiro e verificar a primeira partida com controles,
   áudio real e frames originais. O menu já tem evidência visual dos botões;
   não repetir bilhões de instruções desde o boot.
3. Preservar equivalência AOT/interpretação com o estado numérico SPU2,
   além de PCM, DMA e IRQ. Noise, sweep, reverb/ADMA audível e ausência de
   IRQ com tráfego não modelado continuam limites reais, sem retorno falso.
4. Investigar a regressão medida de 3,15% na janela curta sem profiling.
   Cobertura continua 99,0864%/85,9795% nas janelas anteriores; no novo
   trecho de um milhão até o menu foi 96,1243%, com estado idêntico.
   A otimização antiga de polling permanece, mas o ganho histórico de
   7,17% não foi integralmente conservado no novo tempo medido. Não
   otimizar trocando semântica ou retirando síntese/verificações.

WAV real: `out/game-first/superbust-spu2-game9.wav`, 106.516 quadros a
48 kHz, 104.306 com sinal. Frames de Down/Up: `superbust-spu2-game9-vblank-768.bmp`
e `-778.bmp`. Proveniência e medidas locais em `spu2-final-evidence.json`.
O controle original da BIOS manteve a recusa do encoder 0x7E em EE
0x80007D20 / 17.690.079 instruções. Não ligar hipóteses nesse controle.
Detalhes em [FIRST-GAME-STATUS](FIRST-GAME-STATUS.md#spu2-audível-e-menu-original--05112026).
Nenhum ensaio deve permanecer ativo ao concluir a tarefa.

## Fronteira anterior — 04/11/2026 (histórico)

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

## Última verificação curta — Simpsons, 05/11/2026

ReadRTC superado com UTC externo explícito. Janela de 120 milhões de
instruções, sem imagem; nova recusa IOP `0x267DC` / 17.733.070 em CDVD
HOWTO `0x1F402006`, fora do modo ocioso 2048/`0x80` suportado.
Ensaio encerrado, sem checkpoint novo. Não altera a tarefa ativa de
Super Bust-A-Move. Evidência no relatório do primeiro jogo.

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
