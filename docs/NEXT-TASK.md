# Próxima execução — Simpsons, Shadow of the Colossus & Super Bust-A-Move

## Atual — Super Bust jogável no PS2Recomp fixado (13/11/2026)

As seções abaixo descrevem o runtime próprio removido e são histórico.
Contrato e estado vigentes: `docs/ARCHITECTURE.md`, seção Diretriz.

1. Super Bust no caminho padrão (GS de CPU + SPU2 do PCSX2): validar menu,
   partida, entrada e som contínuo, depois atacar a lentidão medida
   (~2 s de host por 1 s IOP na cena seguinte ao menu).
2. paraLLEl-GS opcional: localizar o primeiro comando/estado que faz a VRAM
   divergir do GS de CPU em vsync 127/239, sem raster duplicado nem scanout CPU.
3. BIOS: completar o bootstrap IOP por HLE (RESET `rom0:UDNL rom0:OSDCNF`)
   e sinalizar BOOTEND só após conclusão real.
4. God of War bloqueia no índice de recursos; Killzone SCUS-97402 tem objetos
   preservados de uma compilação interrompida. Reutilizar builds existentes.

## Pré-requisito após a remoção do interpretador — 08/11/2026

As seções abaixo partem de checkpoints e janelas do executável híbrido
anterior. Antes de retomá-las, preparar os títulos pelo PS2Recomp
(`ps2n-dev prepare-sdk`) e confirmar que EE, IOP e VU da janela-alvo têm
tradução ligada em `ps2n_core`; onde faltar, registrar o artefato ausente
em vez de continuar por interpretação.

## Shadow — fronteiras distintas verificadas em 07/11/2026

Preservar a recusa estrita de `VSUB` em `0x0122DBC0`, EE 1.980.746.550:
o intervalo certificado cruza zero/sinal. O refinamento tardio de histórico
foi recusado; não escolher bits para continuar esse checkpoint.

O experimento separado `7355f195...` selecionou o carry VU de referência
antes do reset e injeta ELF após firmware parcial. Manifesto, binários,
hashes e checkpoints em `out/sotc-investigation/`. `VSUBq` em `0x1BDEEC`
foi implementada em `c5939dc`; `vu-reference-after-subq.ps2ncp` contém
1.985.757.156 instruções e mantém o cenário próprio. Continuar a investigar
o primeiro bloqueio desse cenário sem promovê-lo a boot original ou gameplay.
As capturas mostram somente o indicador de carregamento; AOT comercial
limitada ao startup, janela final interpretada. Verificar menu, entrada e
áudio antes de afirmar que Shadow roda. Evidências em FIRST-GAME-STATUS.

## Super Bust — prioridade explícita: velocidade original

Concluir a integração visual de `source-frozen-v7` / `collision-core-build`
com AOT local ampliada do menu; a compilação da janela está em andamento.
`45d740e` e `740d56f` preservaram estado, RGBA, PCM e entrada no checkpoint
comercial, mas não demonstraram velocidade original ou áudio contínuo.
O usuário fecha manualmente as janelas: não atribuir isso ao runtime e não
fechar ou reiniciar sessões existentes para medir.

Agrupar mudanças e executar apenas verificações necessárias ao contrato e
medições de lotes relevantes, conforme preferência do mantenedor. Amostrar
novamente o executável integrado antes de escolher outra otimização estrutural.
O laço observado de DMA VIF1 é evidência local, não uma regra por endereço
no núcleo; qualquer agrupamento precisa preservar IRQ, DMA, código, áudio e
fronteiras visíveis. Logs, todos os pares e resultados negativos estão em
RUNTIME-PERFORMANCE e `out/reconstruction/superbust/`.

## BIOS — MAC superado, fechar o Browser

O cenário experimental passou pelos resets FIFO/MAC, descritores, configuração,
PHY e pacote em loopback. `out/bios-browser/loopback-fixed.log` chegou à EEPROM
SPEED sem conteúdo fornecido. A hipótese separada de EEPROM ausente percorreu
seu erro; reset IRQ limpo/linha desconectada permitiram avançar até SCMD 22.

`dev9-irq.log`: IOP `0x24F84` consome o segundo de alarme desconhecido em
`0x1E88E2`. Conectar a opção já existente de alarme desativado e o RTC externo
explícito pela CLI, retomar o ensaio desde o reset e seguir a primeira recusa
até obter uma imagem original do Browser. Usar o novo checkpoint só com suas
identidades/runtime/cenário; preservar os prefixos e registrar todas as hipóteses.
O idle tem oito words AOT verificados, demais regiões usam fallback de referência.

Cinco regressões passaram na fonte isolada. Confirmar CLI atualizada e RTC,
e conferir imagem/estado antes de declarar boot; nenhum teste sintético ou
contador EE substitui o Browser. O caminho padrão ainda mantém o encoder `0x7E`.

## God of War — execução em andamento, 07/11/2026

Usar `out/gow/continuation/source`/`build` e probes com os mesmos headers.
ELF + syscall Sony ExecPS2 7 apresenta a tela Sony em campos 512x223/224.
`1acd0ed` corrigiu XORI no IOP; `7ac61f7`, o recorte entrelaçado ímpar.
Oito testes focados passaram. Menu, gameplay, entrada e áudio ainda pendentes.

`3358cf0` superou GetToc/ReadDvd da primeira camada. Dez testes focados e
quatro integrações em fonte limpa passaram, incluindo checkpoints antigos.
A geometria OTP é entrada diagnóstica, não medição física. Nenhuma regra do
God of War foi inserida no runtime.

`ee-boundary.ps2ncp` é o prefixo válido de 432.416.676 EE. A recusa posterior
foi DADDIU no delay slot em `0x284588`, não ANDI no PC já avançado. `de1710d`
superou-a; cinco integrações limpas, incluindo checkpoints antigos, passaram.

Fronteira de `daddu.log`: MULT `0x283EC8`, total 437.051.708 EE. R4 parcial,
R3 zero conhecido. A candidata de produto zero passou sete testes focados;
verificar `mult-zero.log` e a suíte limpa antes de registrá-la. Continuar até
menu/gameplay; PCM produzido ainda é silêncio. Preservar máscaras/LO/HI e
recusas quando o produto não pode ser provado.

O antigo bloqueio SPU2 em 82 milhões não se reproduz com ABI consistente.
Não corrigir reset com base naquele probe. Boot original continua no encoder
0x7E; no caminho sem injeção, verificar bytes antes de ligar AOT em 0x100008.

## Continuar o cenário reconstruído e medido — 07/11/2026

Há checkpoints novos identificados em `out/reconstruction/superbust/`; não usar
os antigos caminhos do tabuleiro apagado. A otimização IRQ `9aaf0d8` passou
equivalência e cinco pares (mediana −14,24%, um par desfavorável conservado).
As fontes congeladas v1/v3 isolam essa mudança das alterações concorrentes.

As quatro raízes reais adicionais já foram preparadas: 29.795.610 de 30 milhões
agora são AOT. A combinação tem cinco pares favoráveis, 7,14204 → 1,83249 s,
com estado/saídas iguais. Continuar o carregamento headless a partir do novo
checkpoint `scene-expanded-1170m.ps2ncp`; resolver a cena ainda preta antes
de medir a partida. Manter firmware parcial/injeção e hipóteses explicitamente
separados do boot original.
O sampling após expandir o AOT tem apenas 76 amostras e destaca latches/espera
do IOP e timers: obter uma coleta maior na cena alvo antes de selecionar outra
mudança do núcleo. Capturar o próximo produtor GIF e o buffer selecionado;
o scanout foi aceito, mas a imagem continua preta apesar das escritas em VRAM.
Revalidar fontes atuais, saídas e restauração ao integrar os incrementos
concorrentes; este resultado não certifica a HEAD completa nem o launcher.

## Após conectar Play — 07/11/2026

O botão Jogar tenta o reset original com a BIOS ativa e o disco selecionado,
na janela compartilhada, com interpretador/GS software explícitos e recusa
visível. O executável local está em `out/launcher-build/ps2-native.exe`.
Próxima validação: testar os títulos locais por esse fluxo e registrar o
primeiro PC/motivo real; resolver a fronteira compartilhada observada.
Não confundir a tentativa com boot concluído ou gameplay. Não foram retomadas
as sessões comerciais históricas; manifests/checkpoints continuam separados.

## Após reutilização do leitor IPU — 07/11/2026

`SETIQ` com FB/BP não alinhados e `SETVQ` com BP não alinhado já foram
integrados e testados, com código do ps2-web-recomp. Não repetir esta etapa.
O restante da decodificação MPEG continua ausente: selecionar a próxima parte
a reutilizar a partir de uma recusa observada no cenário comercial disponível.
Esta entrega não substitui a recuperação da cena e a nova medição de desempenho
descritas abaixo. Evidência em [REFERENCE-AUDIT](REFERENCE-AUDIT.md#reutilização-efetiva-do-leitor-ipu--07112026).

## Após o cache do interpretador — 07/11/2026

O cache por execução foi implementado e medido no boot experimental reconstruído:
32 milhões interpretadas em 3,20387 → 2,78142 s, cinco pares favoráveis e estado
completo igual, inclusive continuação 10 + 22 milhões. Cinco regressões passaram.
O snapshot e as hipóteses estão em
[RUNTIME-PERFORMANCE](RUNTIME-PERFORMANCE.md#cache-de-decodificação-do-interpretador--07112026).

Para otimizar novamente, partir do novo perfil e do cenário efetivamente
reconstruído; fetch/TLB, execução EE e observação continuam relevantes.
Este checkpoint de boot não substitui o tabuleiro comercial perdido. Restaurar
a cena do jogo com suas identidades antes de atribuir ganho à partida ou ao
launcher. As alterações concorrentes posteriores ao snapshot precisam de
validação própria; preservar seus fontes, cenários e dados.

## Prioridade após exclusão de out — 07/11/2026

O mantenedor confirmou a exclusão de `out/`: os checkpoints/manifestos e perfis
comerciais citados abaixo são históricos e não estão disponíveis. Não iniciar
benchmarks apontando para esses caminhos nem atribuir seus números à HEAD.
A descoberta `observed=` está integrada a `ps2n-dev`, e 120 casos ALU do
ps2autotests já têm comparação independente executável; não repetir esses
itens como implementação ainda inexistente.

Próxima ação: reconstruir um cenário headless válido com as CLIs existentes,
firmware/disco identificados e hipóteses explícitas, salvando checkpoint e
manifesto juntos. O boot original ainda tem a fronteira do encoder 0x7E;
não inventar um estado para saltá-la. Com a cena restabelecida, congelar
Release/AOT, validar saídas, recolher sampling separado e só então escolher
a otimização CPU. Simpsons e Shadow também precisam de seus cenários
restabelecidos antes de alegar avanço nas fronteiras históricas.

## Prioridade após o estudo RECOMP — 07/11/2026

Seguir o [plano priorizado](REFERENCE-AUDIT.md#ordem-de-execução-proposta),
preservando as fronteiras e hipóteses de cada título abaixo. Primeiro integrar
e identificar um baseline único do tabuleiro com as entradas observadas, validar
estado/RGBA/PCM/input e coletar sampling Release com símbolos correspondentes,
em execução separada da medição primária. Os resultados já existentes foram
conferidos: 1,5625226 → 1,5051987 s / 30 milhões em cinco pares; 99,4917% AOT
ainda fica cerca de 15× abaixo do tempo original nessa janela.

Escolher uma mudança estrutural pelo maior custo atual: guard/memória, despacho
ou IOP. Consultar os doadores identificados antes de reimplementar. Em seguida,
adaptar um primeiro lote executável de `ps2autotests` e integrar a descoberta
observada em `ps2n-dev`, reutilizando parser, gerador e partições atuais.
Não repetir otimizações rejeitadas sem nova evidência. Validar o executável e
os módulos efetivamente usados pelo launcher antes de atribuir-lhe ganho.
Nenhuma sessão gráfica foi retomada por este estudo.

## Desempenho de Super Bust-A-Move, sem launcher — 07/11/2026

Prosseguir até medir velocidade original, sem launcher e sem retomar
Computer Use. O conjunto de CPU/GS registrado em `d36a53d`/`0fc491e` passou
26/26 regressões e a janela comercial curta preserva RGBA, PCM e entrada.
Os últimos pares na fonte congelada ficaram perto de 1,55 s para 0,1 s
convidado; ainda não há entrega de tempo real.

A preparação com 1.006 entradas adicionais foi medida: cinco pares
1,5625226 → 1,5051987 s, preservando estado completo, RGBA, PCM e entrada.
A partida roteirizada de 600 milhões/122 campos também preservou essas
saídas; ainda faltam aproximadamente 15 vezes para tempo real. A revisão
9b1a20f com guard normal e traduções reconstruídas passou 26/26 regressões
e comparação comercial de RGBA/PCM/entrada/GS; os hashes completos entre
esquemas diferentes não são comparáveis.
Selecionar a próxima mudança pelo perfil totalmente recompilado em
out/realtime-current-profile-*: sincronizações, interrupções, observação e
GS ainda dominam. A maioria das recusas do horizonte decorre do IOP em
execução. A experiência de observação agrupada piorou e foi retirada.
Preservar todas as amostras negativas, checkpoints e dados do usuário.
Evidências em [RUNTIME-PERFORMANCE](RUNTIME-PERFORMANCE.md).

## Simpsons — pedido atual, 07/11/2026

MFIFO/fromSPR e BC0F/T/FL/TL estão implementados; 34 testes focados passaram
também em fonte isolada. Ainda não existe frame/menu jogável do SLUS-20624.
Preservar as evidências de Shadow e Super Bust abaixo; elas não certificam
o Simpsons.

1. Retomar a investigação em `out/game-first/simpsons-current-before-gif.ps2ncp`
   (EE 293.097.310, PC `0x37DB20`). O início do DMA GIF escreve CHCR=0x105,
   TADR=0x80003000, mas D2_QWC nunca adquiriu valor conhecido. Localizar
   evidência de reset/initialização ou uma entrada completa que o programe;
   não substituir o estado desconhecido por zero no núcleo compartilhado.
   Um cenário separado com QWC=0 fornecido externamente processou um pacote
   GIF/1.048.576 escritas de pixels de limpeza, sem scanout. Depois de 134.279
   instruções, BC0F em `0x37C9F8` depende de CPC5/CPC7 desconhecidos:
   D_PCR valor 0/máscara `0x03FF00E0`, D_STAT `0x00A84047`. Não desabilitar
   a espera ou simular conclusão de canal.
2. O boot convidado sem injeção do ELF tem um cenário separado, com as
   hipóteses explícitas existentes de reset SIO2, controles/energia DEV9 e
   wrap normal SPU2. A janela estrita dentro desse cenário parou em
   EE `0x20A258`, 211.513.413 instruções, ao consumir o bit 7 desconhecido
   de uma resposta em RAM; IOP sem recusa. O último SCMD foi 0x15
   ForbidDVDP, provável produtor a confirmar; seu acknowledgement continua
   não medido. Checkpoints válidos em
   `out/game-first/simpsons-guest-boot5-{100000000,200000000}.ps2ncp`:
   retomar com o contexto do cenário v5, sem repetir o prefixo. Rastrear
   o produtor antes de fixar bits. Evidências `simpsons-dma-verified-boot-wrap.log` e
   `simpsons-dma-boot-origin.log`; fonte e contexto em
   `out/game-first/simpsons-guest-boot-probe.cpp`. Não misturar esse cenário
   com o checkpoint de injeção ou o boot sem hipóteses.
3. Comparar com `simpsons-exec-argv-latest.ps2ncp`: o handler Sony ExecPS2
   e argv0 de BOOT2 foram exercitados, mas a execução ficou no laço do
   kernel após 220 milhões de instruções. Investigar SIF/estado de threads
   antes de atribuir essa fronteira ao DMA observado na injeção direta.
4. Boot original continua dependendo de evidência do encoder 0x7E e
   SYNCV de reset. Validar imagem, entrada e som reais antes de declarar
   jogabilidade; AOT startup, teste sintético e contagem EE não bastam.

Método, hipóteses e limites em [FIRST-GAME-STATUS](FIRST-GAME-STATUS.md).

## Shadow of the Colossus — após 1.011M instruções e checkpoint 1000m — 07/11/2026

- **Estado atingido**: 1.011.216.676 instruções EE (mais de 1 Bilhão!), 126.402.084 instruções IOP, 204 pacotes GIF, mais de 15.403.008 escritas de pixels na VRAM do GS. Novos quadros não-pretos capturados em `out/sotc/frame-1.bmp` a `frame-11.bmp`. Checkpoints válidos em `out/sotc/sotc-700m.ps2ncp`, `out/sotc/sotc-800m.ps2ncp`, `out/sotc/sotc-900m.ps2ncp` e `out/sotc/sotc-1000m.ps2ncp` (59,3MB).
- **Próximo passo operacional**:
  1. Continuar a execução a partir do checkpoint `out/sotc/sotc-1000m.ps2ncp` em direção a 1.100.000.000 e 1.200.000.000 de instruções.
  2. Monitorar os fluxos de áudio ADPCM/SPU2 e novos formatos gráficos conforme a engine avança na introdução cinematográfica do jogo.
  3. Preservar rigorosamente a fidelidade de hardware da Sony PS2 sem atalhos ou hacks de título.

## Desempenho após eventos/TLB — 05/11/2026

`c23188e`: cinco trios do menu mediram 11,341572 → 7,776684 s por
30 milhões; 1,838974 → 1,254745 s por campo. Estado completo, RGBA,
PCM/entrada e ciclos preservados, incluindo interpretação pura, checkpoint
10 + 20 milhões e os dois backends. Vinte testes focados passaram;
`out/launcher-msvc/ps2-native.exe` foi recompilado. Ainda sem tempo real.

Próximo passo operacional: autorização específica para publicar os commits
em `origin/main`, bloqueada pela revisão automática, e conferir a CI dos
seis alvos. Depois, coletar perfil após TLB para selecionar o próximo custo
de memória/execução por instrução. Não reutilizar as antigas proporções
instrumentadas nem ampliar GS sem nova evidência. Preservar os checkpoints
e a fronteira funcional abaixo. [Medições e limites](RUNTIME-PERFORMANCE.md).

## GS GPU — após os sprites texturizados comerciais

`659ab18` completou o caminho comercial do menu com 222 sprites T4, 60 T8
e 12 limpezas rasterizados por Vulkan e D3D12, com CLUT e Z16S reais do
estado capturado. Zero fallback nessa janela; estado completo, RGBA, PCM,
entrada, ciclos/IRQ e continuação 10 + 20 milhões iguais ao controle.
Sprites CT32/24, STQ/filtros comprovados e hipóteses explícitas também têm
regressões. Não registrar texturas ou CLUT como ausentes dessa entrega.

O próximo trabalho de desempenho precisa partir do gargalo medido e repetir
uma coleta estável no cenário atual: a cobertura GPU aumentou, mas as cinco
rodadas tiveram interferência externa e não demonstram ganho total robusto.
O perfil existente apontou sincronização/EE/memória/IOP; investigar suas
chamadas antes de ampliar rasterização, preservando efeitos/entrada/áudio
e medindo campos originais. Triângulos texturizados, AA1/Gouraud e apresentação
direta continuam fora desta entrega; só ampliar após evidência do cenário.

A primeira partida continua não demonstrada; a fronteira interativa é o
menu em `out/game-first/interactive-menu.json`. AA1/STQ já existem no CPU
experimental e não devem ser registrados como bloqueio ainda não implementado.
Preservar os checkpoints e as anotações anteriores abaixo.
A autorização de Computer Use desta conferência
terminou ao fechar o launcher; não retomar outra sessão sem novo pedido.
Evidência e limites em [GS-GPU-TEXTURES](GS-GPU-TEXTURES.md).

## Fronteira ativa — sessão interativa, 05/11/2026

`feed1d8` conecta o launcher Qt ao runtime compartilhado, com checkpoint
completo, GS ao vivo, teclado/XInput e PCM do SPU2. `210c48b` corrige a largura
do painel no Qt 6.4. No Windows, o título apareceu, Enter/Start iniciou a
animação GAME MENU e o mantenedor confirmou ouvir som do jogo. A execução
observada ficou em 0–1 FPS e aproximadamente 0,75–0,88% do clock EE modelado.
**A sessão ainda não oferece velocidade adequada para jogar.**

O mantenedor interrompeu Computer Use com Escape e depois fechou o launcher.
Não retomar controle da janela sem novo pedido. Pausa/retomada/parada,
salvamento, entrada SIO2, RGBA/PCM e equivalência de estado foram exercitados
pelo teste `launcher-experimental-session`; isso não substitui a sequência
manual completa nem comprova gameplay. Nenhum ensaio comercial deve ficar ativo.

Ponto local mais próximo para teste pessoal:
`out/game-first/interactive-menu.json`, com checkpoint completo
`superbust-interactive-menu.ps2ncp`, EE `0x1D2F00`, 12.281.266.633 instruções,
cenário GS 61 já revisado. Originais preservados; ROM0/ROM1/NVM/disco fixados
pelo manifesto. Esse ponto deriva do menu anterior ao Cross, com uma
instrução executada e a hipótese GS existente explicitamente refinada.
O manifesto antigo `interactive-session.json` continua disponível no título.

Próximo trabalho: completar a sequência humana Up/Down/Cross, verificar a
primeira partida por imagem e resposta dos direcionais, e isolar a próxima
recusa real se ocorrer. AA1/STQ já estão na main: dois testes gráficos e uma
continuação de um milhão desde `superbust-before-game-shading.ps2ncp` passaram,
sem recusa EE/IOP. Não registrar AA1 como bloqueio atual nem declarar gameplay
apenas porque essa janela avançou. Não adicionar otimizações sem nova medida
e comparação de estado. Uso, medidas e limites em
[INTERACTIVE-TEST-STATUS](INTERACTIVE-TEST-STATUS.md).

## Fronteira anterior — menu e AA1, 05/11/2026 (histórico)

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

## Fechamento da residência GS

Concluir o job Linux x64 do launcher de `4fdd7dd` (núcleo já aprovado nos seis
alvos) e repetir cinco pares finais de
pequenas cargas/preenchimento, além da janela comercial com campos originais,
sem compilação ou emulação concorrente. A amostragem intermediária identificou
uma regressão CPU; o ajuste final passou nos testes, mas ainda precisa de
certificação de desempenho. Preservar os executáveis congelados e comparar
VRAM, máscaras, estado completo, PCM e entrada antes de anunciar ganho.
Não atribuir aceleração à GPU nas janelas em que todas as primitivas usam
fallback. Resultados por revisão em [GS-GPU-RESIDENCY](GS-GPU-RESIDENCY.md).
Os incrementos paralelos posteriores de cobertura exigem sua própria
comparação e não entram nas medições congeladas desta etapa.

## AOT — continuação independente da compatibilidade

`3c3a5b5` integra partições estáveis de 64 KiB com reuso por hash no
gerador/CMake/`ps2n-dev`. Cinco rodadas controladas: +19,24% de throughput
em 30 milhões sobre a referência anterior ao fetch/eventos; +5,39% sobre
a TU única já otimizada. Estado completo, RGBA/PCM e ciclos idênticos,
inclusive retomada de 10 + 20 milhões. Evidência e custos de build no
relatório estrutural abaixo. Próximo gargalo a medir: tradução/cache de
dados e as fronteiras explícitas do interpretador, que ainda executa
14,020133% dessa janela. Não deduzir os custos posteriores dos tempos
instrumentados anteriores à otimização.

Os dois primeiros incrementos estruturais estão medidos em
[AOT-STRUCTURAL-PERFORMANCE](AOT-STRUCTURAL-PERFORMANCE.md): fetch
especializado e eventos VU/VIF/DMA, +17,62% de throughput na janela longa
pareada. O pacote anterior continua em
[AOT-PERFORMANCE-STATUS](AOT-PERFORMANCE-STATUS.md).
Preservar o trabalho ativo do jogo e os checkpoints originais. Antes de
outra otimização, recolher o perfil após essas mudanças; o perfil anterior
não autoriza atribuir custos iguais ao novo caminho. Priorizar apenas
candidatas com ganho isolado, estado completo/RGBA/PCM equivalentes e
custo de código/build registrado. A descoberta automática das raízes
explícitas continua pendente; o particionamento já foi integrado.
Invalidação por páginas exige primeiro cobrir e testar todas as escritas,
DMA, cache/writeback e restauração. Não retirar polling, verificações ou
efeitos para aproximar tempo real; repetir a janela comercial pareada.

## Continuação independente — VU1 AOT, 05/11/2026

A primeira versão funcional está em `d6f0bd3`: geração offline pelo AOT
existente, seleção por identidade, invalidação, fallback explícito e
fixture própria com geometria GS, IRQ e checkpoints equivalentes. A tarefa
interativa do jogo acima permanece preservada; nenhum ensaio foi deixado ativo.

O próximo trabalho VU1 exige uma captura válida de invocação comercial:
observar MPG e raízes de início com `--vu1-capture=out/<perfil>/vu1`, gerar
`ps2n-aot --vu1` com todas as entradas observadas, compilar pelo manifesto
existente e repetir a mesma janela com `--vu1-interpreter`. Conferir estado
completo, ciclos/IRQ, RGBA e PCM; registrar tempo por campo e custo de build.
O checkpoint atual de Super Bust-A-Move tem apenas sentinelas VU1 e zero
instruções dessa unidade em 30 milhões EE; não executar esse conteúdo
artificialmente para anunciar validação comercial.

Priorizar a regressão do cenário integrado curto antes de ampliar cobertura:
a fixture isolada ganhou 17,40%, mas a integração curta consumiu 15,28% mais
tempo. P/EFU, controles I/M/D/T e prova de alvos indiretos ainda faltam;
não remover recusas ou pipelines. Mudanças na imagem integral recompilam
suas partições; reuso parcial entre imagens distintas ainda não existe.
Evidência e limites em [VU1-AOT-STATUS](VU1-AOT-STATUS.md).
