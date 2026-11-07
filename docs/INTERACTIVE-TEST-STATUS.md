# Modo de Teste Experimental — 05/11/2026

## Play pelo boot original — 07/11/2026

O botão **Jogar**, nos detalhes de cada disco, agora tenta o boot original
com a BIOS ativa e esse disco montado. Não usa checkpoint ou injeção ELF.
A janela identifica interpretador de referência e GS por software e exibe
o PC/motivo da primeira recusa. O Native ainda é recusado explicitamente.
Selecione uma BIOS válida antes de iniciar; NVM anexada precisa ser válida
para o perfil selecionado. ROM0/NVM são conferidos por hash no worker;
o disco é reidentificado e seu BOOT2 conferido contra a biblioteca.

O build local atual fica em `out/launcher-build/ps2-native.exe`, Release
MSVC `/O2`, Qt 6.8.3. As DLLs requeridas ficam ao lado do executável;
`qt.conf` aponta os plugins/QML à instalação local de Qt já existente.
Não é um pacote portátil. A instalação tem metadados antigos que impedem
`windeployqt`; as dependências foram copiadas da instalação efetiva.

Cinco testes `launcher-*` passaram. O teste da sessão usa a biblioteca e
`startGame`, compara a recusa/PC com execução independente do reset original,
recusa hash de BIOS divergente e confere nova tentativa/parada. Os testes
anteriores preservam input, RGBA, PCM, checkpoint e UI offscreen. Não houve
sessão humana, execução comercial pelo Play, medição de velocidade ou CI.

O texto abaixo descreve o caminho de checkpoint e evidências históricas.

O launcher Qt existente implementa `RuntimeSession` em uma thread própria,
retomando um checkpoint completo verificado. Não inicia um segundo emulador,
não injeta ELF ou serviços no convidado e não habilita Jogar/Browser originais.
Não há seleção por nome de jogo, serial, revisão ou PC no caminho da sessão.

## Uso local

Abra `ps2-native`, escolha **Modo de Teste Experimental** (ou Ctrl+T), abra
um manifesto JSON local e clique em **Iniciar**. O painel também pode ser
aberto com `--experimental-session=CAMINHO`; essa opção não inicia o núcleo.
O último manifesto escolhido é lembrado. A troca de runtime de checkpoints
anteriores exige opt-in explícito na janela e continua sujeita às migrações
já revisadas pelo leitor comum. As hipóteses preservadas aparecem no painel.

Nesta máquina, o executável é `out/launcher-msvc/ps2-native.exe`. Para chegar
direto ao menu, abra `out/game-first/interactive-menu.json`; o manifesto
`interactive-session.json` mantém a retomada anterior no título. Marque a
troca revisada de runtime para os checkpoints antigos, clique em Iniciar e
depois na imagem. Esses manifestos e todo o conteúdo comercial são locais.

Clique na imagem para controlar: setas, Enter/Start, Z/Cross, X/Circle e
Backspace/Select. Perda de foco solta os controles e descarta entradas pendentes.
Os eventos press/release são enfileirados (limite 256) e amostrados somente
quando o convidado envia um poll digital SIO2 01/42 válido; um toque breve
permanece visível em um poll mesmo que o host seja mais rápido que o runtime.
Pacotes já iniciados conservam seus bytes. O callback é removido em reset/restauro.

Windows usa XInput quando disponível, consultado no host a cada 10 ms,
somente com foco na imagem. Start, Back, D-pad, A/B/X/Y e shoulders têm
mapeamento digital inicial; não há emulação de stick analógico. O campo
opcional `gamepad_mapping` contém 16 máscaras XInput inteiras, uma por bit
PS2: Select, L3, R3, Start, Up, Right, Down, Left, L2, R2, L1, R1,
Triangle, Circle, Cross, Square. Zero desabilita o vínculo. L3/R3 continuam
soltos no pad digital. Outros hosts informam gamepad indisponível.

**Pausar** pede uma fronteira de orçamento do dispatcher comum (100.000
instruções por lote); a interface continua respondendo. **Continuar** retoma
os mesmos clocks/estado. **Parar** e fechar solicitam cancelamento, acordam
esperas de buffering/pausa e juntam a thread antes de destruir a sessão.
Validação/hash de dependências em andamento termina antes do encerramento.

**Salvar checkpoint** só fica disponível após a pausa ser reconhecida. O
salvamento usa o `BootReport` da última fronteira válida e o escritor atômico
comum; recusa sobrescrever arquivos e recusa estado após bloqueio EE/IOP.
Gera também `NOME.ps2ncp.json`, manifesto para retomada com hash do novo
checkpoint e cenário derivado da política de entrada ao vivo. Os checkpoints
anteriores são preservados. Se a escrita do manifesto falhar depois do
checkpoint, o checkpoint válido permanece e o erro é mostrado.

## Manifesto e dependências

`format` é 1. Todos os caminhos relativos são resolvidos contra a pasta do
manifesto, sem procura por basename ou fallback para outro perfil.

| Campo | Conteúdo obrigatório |
|---|---|
| `rom0`, `rom0_sha256` | Dump e SHA-256 fixado |
| `console` | Perfil `MODELO:PLACA` conhecido pelo núcleo |
| `nvm`, `nvm_sha256` | NVM real desse console e SHA-256 |
| `rom1` | Dump pareado explicitamente |
| `rom1_identity` | Objeto com `rom0_sha256`, `romver`, `model`, `mainboard`, `sha256`, `build`, `dvd_version`, `provenance` |
| `disc`, `disc_sha256`, `disc_medium` | Disco original, hash fixado e `ps2-cd` ou `ps2-dvd` |
| `checkpoint`, `checkpoint_sha256`, `scenario_sha256` | Checkpoint completo, hash do arquivo e cenário revisado |
| `hypotheses` | Descrição explícita das hipóteses do experimento |
| `aot_elf`, `aux_aot_elf` | ELFs correspondentes aos módulos AOT compilados, quando usados |

O leitor comum confere novamente esquema, payload e bindings de firmware,
perfil, NVM e disco antes de publicar o estado. A sessão não acrescenta
refinamentos GS nem reconstrói hipóteses de reset por conveniência.

O build padrão oferece interpretador explícito. Para usar AOT local offline,
configure `PS2N_SESSION_AOT_SOURCE` e, opcionalmente,
`PS2N_SESSION_AUX_AOT_SOURCE`, com as traduções do mesmo build do núcleo.
Os módulos devem exportar `aot_module` (o auxiliar também aceita o símbolo
histórico `idle_aot_module`). A sessão confere SHA-256 do ELF e bytes das
entradas contra a RAM restaurada, recusa sobreposição e mantém a guarda
de cada instrução gerada. Não compila nem baixa código durante a execução.
Traduções comerciais, manifests privados e checkpoints ficam fora do Git.

## Imagem, áudio e métricas

`BootOptions::on_frame` fornece scanout novo nas bordas VBLANK modeladas.
Uma caixa de último frame limita memória e evita acumular eventos Qt.
`QQuickPaintedItem` apresenta o RGBA original: tela 4:3 por padrão (os campos
640x224 têm amostras não quadradas), opção de proporção dos pixels GS,
letterbox e nearest-neighbor, sem filtro temporal, troca de linhas ou mistura
de campos. O GS continua responsável por renderizar e recusar
formatos não modelados; não há renderer substituto no launcher.

O sink SPU2 existente fornece o mix final do core 1 (que já inclui core 0
conforme MMIX), estéreo Int16/48 kHz. Windows envia blocos PCM de 20 ms
por `waveOut`, com até oito buffers preparados. Linux/macOS usam QAudioSink
quando Qt Multimedia está instalado; ausência/erro é informado, sem mudar
o convidado. Uma fila host limitada a aproximadamente 250 ms aplica
backpressure ao worker. Pausa conserva a posição dos buffers; parada os
descarta no host. Não há resampling, time stretch ou alteração da síntese.
Emulação lenta produz intervalos de silêncio entre blocos de áudio reais.

O painel mede imagens novas pintadas por segundo, avanço de ciclos EE por
segundo de host dividido por 294.912.000 (clock modelado), cobertura AOT
acumulada, polls SIO2 e PCM enviado/com sinal. O contador de faltas de buffer
observa a saída host vazia a cada 10 ms, sem alegar precisão de underruns físicos.
Não promete 60 FPS, tempo real, áudio contínuo ou gameplay.

## Validação local

Build Release MSVC 19.44 / Qt 6.8.3, com partições AOT estáveis de 64 KiB e
auxiliar idle recompilados com o núcleo. Dez testes focados passaram:
launcher core e sessão experimental, SIO2 pad, SPU2 synthesis, checkpoint
completo, launcher UI clean/import/restore e dois testes GS (AA1/STQ).
O novo teste da sessão gera firmware, ROM1, NVM, disco e checkpoint próprios,
inicia o worker real, recebe RGBA do GS e verifica Cross no buffer SIO2 lido
pelo programa IOP. Pausa/salva duas vezes sem alterar o payload, retoma e
encerra, recusa cenário errado e encerra durante validação. Não usa runtime
simulado. Compara 300 mil instruções contínuas e três lotes de 100 mil:
estado completo, ciclos, DMA, interrupções, frames e PCM sintetizado idênticos.
O hash de esquema continuou
`c649550347ab1c003a8b8e441e3241a8917c893acb09a3a5262c2ad2a6fc2ee6`:
callbacks host e opções de relatório não alteram os campos serializados.

Sessão visual aberta no Windows pelo launcher real. Checkpoint anterior ao
menu: `superbust-spu2-game7-11673868840.ps2ncp`, cenário GS 13 preservado.
O título original apareceu; Enter enviado pelo teclado Windows iniciou a
animação GAME MENU. Em 175.200.000 instruções da sessão: 36 polls SIO2,
28.516 quadros estéreo com sinal e 27.840 enviados à saída host. Janelas
observadas: 0–1 FPS pintados, aproximadamente 0,75–0,88% do clock EE e
98,73–98,83% AOT. Essas são medidas desta sessão lenta, não benchmark pareado.
O mantenedor confirmou **“Ouço som do jogo”** e relatou lentidão excessiva.
Depois interrompeu Computer Use com Escape e informou ter fechado o launcher;
nenhuma entrada de janela foi enviada após a interrupção.

A sequência manual Up/Down/Cross, a resposta dos direcionais na partida,
pausa/retomada pelo usuário e um gamepad físico **não foram comprovados**
nesta execução. O teste automatizado de ciclo de vida não substitui isso.
Não há alegação de gameplay nem de usabilidade em tempo real.

AA1 e STQ existentes foram revalidados sem alterar o GS. Uma continuação
de um milhão desde `superbust-before-game-shading.ps2ncp`, com as hipóteses
GS 61 explicitamente revisadas, chegou a 12.335.692.440 instruções totais,
EE `0x1D2F00`, IOP ativo `0xAE98`, sem recusa. Isso supera a antiga AA1,
mas não prova partida. O checkpoint direto no menu foi convertido pelo leitor
comum com revalidação dos campos legados e salvo sob o runtime atual.

Referências de API: [PCM Windows](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutwrite),
[pausa Windows](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutpause),
[QAudioSink](https://doc.qt.io/qt-6/qaudiosink.html). Implementação original;
nenhum código GPL foi incorporado.
