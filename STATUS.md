# STATUS — ps2-native

**Registro vivo de progresso e evidências.** Atualizar este arquivo quando houver avanço **verificado**; não reescrever os READMEs, `AGENTS.md` ou `CLAUDE.md` a cada marco. Relatórios detalhados continuam em `docs/M*-STATUS.md`; o trabalho seguinte fica em `docs/NEXT-TASK.md`.

## Estado funcional verificado — 03/11/2026

**Protótipo experimental. Não executa o Browser original até o fim, não possui launcher público nem declara jogos comerciais jogáveis.**

| Componente | Evidência até este ponto | Limite |
|---|---|---|
| AOT MIPS/R5900 | ELF32 sintéticos, CFG conservador, relocations como evidência, geração C++ e testes end-to-end | Cobertura incompleta de jogos e código dinâmico |
| Boot EE | ROM0 reais, COP0/TLB, memória, RDRAM, início do kernel em RAM e AOT híbrido com trace | CPU e dispositivos ainda parciais |
| Direct ELF | BOOT2 de ISO pelo loader comum, ambiente bare-metal, Count/Compare, SYSCALL/BREAK para handler convidado, usuário/supervisor/ERL e overlays AOT verificados | Não instala kernel/serviços; ELF comercial executado, sem imagem ou jogabilidade |
| IOP | Inicialização R3000A, NVM externa, MECHACON, timers/VBLANK, SIF e EE→IOP SIF1 com bytes reais | Sem linker IRX, SIF0, RPC ou serviços completos; DMA/timing ainda parciais |
| GS/GIF | VRAM 4 MiB, CT32/24 e índices T4/T8, profundidade, sprites UV/CLUT/blending, PATH2/3 e scanout; primeiras capturas reais de Super Bust-A-Move no diagnóstico experimental | Boot original e gameplay não comprovados; hipóteses explícitas e formatos/efeitos/timing parciais |
| Launcher desktop | `ps2-native` (Qt 6 opcional): biblioteca persistente, identificação de BIOS/NVM pelo núcleo e de discos ISO/BIN/CUE, diagnóstico `ps2n-boot` assíncrono; janela real validada no Windows | Jogar e Browser bloqueados (sem runtime); sem release; diálogos e arrastar e soltar não automatizados |
| Distribuição | Builds/testes sintéticos nas seis arquiteturas de CI | Builds não comprovam funcionamento do firmware/jogo em cada host; sem release |

## Continuação experimental do jogo — 04/11/2026

- **Simpsons avançou para 93.117.072 instruções:** MUL.S da classe potência
  de dois/dígito Booth inicial negativo, leitura NCMD 06 de dados em DVD
  e descarte simbólico da FIFO MECHACON vazia passaram. IOP `0x25E34` /
  15.691.717 bloqueia agora no SCMD 08 ReadRTC; EE `0x80005F00`.
  Setores reais transferidos, SIF1/SIF0 576/448 bytes; ainda zero gráficos.
  Quatro testes focados passaram e modo BIOS original conservou PCs,
  contagens e hash canônicos. Evidência, referências e limites no
  [relatório](docs/FIRST-GAME-STATUS.md#continuação-do-simpsons--fpu-dvd-e-fifo-mechacon).
  CI: `79bb6d6` aprovado nos seis alvos do núcleo; código `6328bb7`
  aprovado nos seis do launcher. Híbrido e interpretador reproduziram
  a parada ReadRTC e a mesma RAM; nenhum ganho de desempenho demonstrado.
- **Reensaio de The Simpsons: Hit & Run (SLUS-20624):** com o núcleo que
  produziu as capturas do Super Bust-A-Move, híbrido e interpretador puro
  chegaram às mesmas 849.776 instruções e RAM, parando em `0x001681B0`:
  `MUL.S` com arredondamento Booth ainda ambíguo. IOP ativo em `0x2F28`,
  4.158.306 instruções. Zero pacotes GIF, pixels ou frames. Ensaio por
  injeção de ELF, ISO própria montada como DVD e hipóteses explícitas;
  nenhum subsistema ou modo BIOS original alterado. Detalhes no relatório
  [primeiro jogo](docs/FIRST-GAME-STATUS.md#reensaio-de-simpsons--04112026).
- **Primeiros quadros não pretos do jogo, ensaio 101:** três capturas no
  VBLANK, campos nativos 640x224 com 64.478 pixels não pretos cada,
  mostram a tela original de Super Bust-A-Move informando Memory Card ausente.
  Pixels vêm de DMA/GIF, paleta, textura, sprites e blending executados pelo
  jogo; nenhum desenho host. EE `0x81FDC` / 1.300.000.000, IOP `0xAE94` /
  166.552.087 ativo, 326 pacotes novos. Parada voluntária após as capturas,
  sem recusa. Não comprova gameplay ou boot original de disco: usa injeção
  de ELF e hipóteses registradas, incluindo SCANMSK=0 no reset. Slots vazios,
  nenhum save alterado. Capturas e traces permanecem locais/ignorados.
  CI de `d7588b0` e `767fc94` aprovada nos seis alvos de núcleo e launcher.
- Super Bust-A-Move transferiu setores reais do CD, incluindo os LBAs
  15637–15691 de IOPRP202, por ReadCd/DMA3. Após a tag SIF1 NEXT,
  alcançou IOP `0x1F458` / 31.828.183 instruções, consulta MECHACON `03:00`;
  1.920 bytes SIF1 e 1.696 SIF0. A consulta passou usando a versão medida do
  perfil; o CDVD depois chegou ao LBA 15824, final de RSPU2DRV.IRX.
  CAV nominal é hipótese explícita de diagnóstico; não houve loader host.
- DMA/IRQ SIF nos dois sentidos, pacotes parciais, PLZCW e salvamento simbólico
  de BPC/PCCR exercitados por código convidado. Nenhuma resposta RPC fabricada.
- Modo original repetido após ROM1/CDVD: mesmos PCs `0x80007D20`/`0x80005F00`,
  contagens e RAM SHA-256 `cead2f7c0c4e38113e2b445bb0f751fa94a17203549cab3e5639c7e4def9c354`.
- **Primeiros pacotes gráficos reais no ensaio 42:** 16 pacotes GIF PATH3,
  1.048.576 escritas de pixels pela transferência IMAGE comandada pelo jogo.
  É uma limpeza da VRAM, sem frame exibido/menu/gameplay. Novo bloqueio EE
  `0x1CDF74`, leitura GS CSR, após 338.172.827 instruções. O ensaio usa injeção
  de ELF e hipóteses explícitas, incluindo SPU2 inativo no reset; não comprova
  boot original. O modo original mantém PCs/contagens/hash anteriores.
- Continuação até ensaio 84: CSR, IMR, aritmética inteira com overflow,
  FPU, montagem e multiplicação das matrizes VU superados. EE em espera `0x81FD0` /
  360.000.000 instruções, IOP parado `0x47850` / 47.580.787. A fronteira Booth
  de 15 zeros baixos foi resolvida por perda do carry da coluna 15; 16 ou
  mais zeros não atingem colunas descartadas. Demais casos ambíguos recusam.
  VSQRT/VDIV/Q e VMOVE passaram. A hipótese explícita SPU2 7CA permitiu
  investigar as janelas não atribuídas, também isoladas por hipótese explícita. PIO enviou 16 halfwords reais para RAM SPU2; parâmetros, KON/KOFF simbólicos, ENDX, ESA/EEA e configuração master passaram. ENVX zero foi provado por release linear limitado; efeitos de ganho zero preservam RAM desconhecida. ESA inicial foi isolado por hipótese; o ensaio 84 completou 31.584 palavras reais por DMA normal e alcançou uma transferência SIO2. FIELD e os bits de timing RECV2 usados pelo PADMAN avançaram sob hipóteses estreitas e explícitas. O barramento usa throughput explícito não medido; bytes opacos permanecem desconhecidos. Sem novos pixels além da limpeza;
  PMODE ainda não escrito. O modo BIOS original mantém seu hash/contagens.
  Commits `d4d98a1` e `8410973` aprovados na CI dos seis alvos (núcleo e launcher);
  `94230de` e a correção de fixture FPU `dd6ed41` aprovados nos seis do núcleo.
  ADD.S/SUB.S (`1ddd0d1`) também aprovado nos seis do launcher.
  BSD e modo original preservados.
- Ensaio 89: controles digitais completaram polls/timeouts e IOP segue
  executando (`0xAE94` / 48.228.327). D-cache funcional opcional superou
  DXLTG com tags/dados reais e escrita de volta; sem latência adicional e
  com hipótese separada para hit via alias uncached. EE `0x108AD8` /
  353.409.922: próximo bloqueio é DMA VIF1 source-chain/TTE. Seis testes
  focados passaram, incluindo coerência observável pelo DMA/GIF. Ainda
  16 pacotes de limpeza. No ensaio 90 o DMA VIF passou e o jogo escreveu PMODE/
  circuito 2; EE `0x1024C0` / 358.037.955 para em escrita de profundidade
  PSMZ16S. Profundidade e scanout INT+FFMD implementados: ensaio 91 no mesmo PC, agora SCANMSK não escrito; scanout 640x224 preto da limpeza real, sem menu/gameplay. Bits SCANMSK/FBA desconhecidos passam a propagar incerteza, sem valor de reset inventado. Seis testes GS/VIF/frames AOT focados passaram; CI d033f16 e 0570023 aprovada nos seis alvos de núcleo/launcher. Sete testes
  focados DMA/cache/GS/AOT passaram. CI dos controles `938ed35` e
  `833004c` aprovada nos seis alvos de núcleo e launcher.
- Ensaios 92–97: uploads PSMT8/PSMT4, DMA GIF source-chain pelo motor
  compartilhado do VIF e PSUBW passaram. O ensaio 97 alcançou
  1.260.000.000 instruções EE (`0x81FDC`), 246 pacotes GIF e
  1.819.120 escritas comprovadas; scanout 640x224 ainda inteiramente preto.
  IOP `0x47850` / 159.520.606 recusou uma sondagem de Memory Card cuja
  presença não estava configurada. O ensaio seguinte usa explicitamente
  slots vazios na API diagnóstica existente; nenhum save é montado.
  Captura no VBLANK está ativa, mas ainda sem imagem não preta.
  CI de `f6344e4`, `950cfd8`, `df1203c`, `b6ea81f` e `259346e`
  aprovada nos seis alvos de núcleo e launcher. Regressão BIOS original
  mantém PCs, contagens e hash canônicos após essas mudanças.
- Ensaios 98–100: slots de Memory Card explicitamente vazios e
  VIF FLUSH/DIRECTHL passaram; sprites UV, nearest, CLUT CSM1 CT32,
  PSMT4/8 e alpha blending implementados com aritmética inteira.
  Ensaio 100: EE `0x1CE468` / 1.268.129.288, IOP ativo, 265 pacotes GIF,
  2.077.200 escritas comprovadas por transferências. SCANMSK não escrito
  propaga incerteza aos desenhos; profundidade e scanout recusam bits
  indeterminados. Ainda nenhum frame não preto. O ensaio 101 configura
  SCANMSK=0 como hipótese isolada de reset, desligada no modo original.
  Oito testes gráficos focados e regressão BIOS original aprovados.
- Sequência, hipóteses, testes e referências:
  [primeiro jogo](docs/FIRST-GAME-STATUS.md#continuação-experimental--subsistemas-exercitados-pelo-jogo).

## Última evidência: sincronização GS/IOP — 04/11/2026

- Corrigida a observação de mudanças do timing GS antes da próxima decisão de interrupção
  do IOP. Regressão própria falha antes da correção e passa depois; uma parada anterior
  continua preservada. Não altera valores de reset do GS nem respostas do encoder.
- **Super Bust-A-Move repetido no interpretador e no híbrido:** 4.486.087 instruções,
  PC `0x80005F00`, IOP `0x000040B0`, RAM idêntica; **zero pacotes GIF do jogo e zero pixels**.
  O trace confirma `CSR.RESET` → `SMODE1` → encoder desconhecido, sem escrita de `SYNCV`.
  `SetGsCrt` Sony repetido também para em `0x80007D20`; não há inicialização comprovada.
- Cinco testes focados de IOP/SIF aprovados. A correção não demonstrou avanço do jogo
  nem ganho de desempenho. Evidências e dependência de dados reais de hardware:
  [relatório do primeiro jogo](docs/FIRST-GAME-STATUS.md#sincronização-gsiop-e-reexecução-do-checkpoint).

## Evidência anterior: primeiro ELF comercial

- **The Simpsons: Hit & Run (USA), SLUS-20624:** escolhido entre os novos discos pelo ELF monolítico.
  Até a primeira syscall: 214.945 instruções, 214.944 AOT/1 interpretada, PC `0x80000180`, zero pixels GS.
  Tetris Plus local é PS1 e não é candidato ao runtime EE. **Nenhum menu, controle ou gameplay.**
- **Correções genéricas executadas:** contexto SA/HI-LO/FCR31, packing e PSUBB, cargas/escritas
  desalinhadas, MOV/conversões FPU, divisão SRT e multiplicação esparsa. Casos restantes falham;
  a cobertura exclui a instrução que não aposentou. Nenhum serviço HLE substitui a BIOS.
- **Diagnóstico separado, sem boot concluído:** ELF injetado no estado parcial do firmware executou
  handlers Sony reais até 627.894 instruções; interpretador/híbrido conferem RAM. Parou em MUL.S
  geral (`0x002DEAF4`). Isso não comprova inicialização normal. Boot ROM0/NVM original repetido mantém
  o bloqueio externo no ID não medido do codificador (`0x80007D20`), sem respostas assumidas.
- **Regressões:** contexto/FPU, falhas e equivalência AOT/interpretador com fixtures próprios;
  nenhuma alteração no launcher. Identidades, logs, testes/CI e limites: [primeiro jogo](docs/FIRST-GAME-STATUS.md).

## Evidência anterior: incremento M3.0

- **Três ELF próprios pelo mesmo runtime:** chamadas/memória/interrupt de Count, comunicação SIF1 e frame
  VIF1 DIRECT→GIF PATH2→GS. Interpretador e AOT conferem memória, registradores, eventos e framebuffer.
- **SIF1 real dentro do modelo:** 256 bytes transferidos para a RAM IOP, checksum calculado no R3000A,
  backpressure e flags/IRQ conferidos; não é RPC/HLE nem prova de timing físico.
- **AOT genérico:** descoberta de funções por JALR constante, `--hybrid` para ELF e snapshot SHA-256 de
  região dinâmica para regeneração offline; overlay compilado confere bytes e invalida código modificado.
  No teste de CPU, interpretação caiu de 30.017 para 17 instruções e trocas de motor de 20.009 para 9.
- **Frame Direct ELF:** 64×32, digest RGBA8 `40231620…b6f436a3`, igual nos dois motores. Modo original
  de boot permanece separado; nenhum firmware ou jogo real foi reexecutado nesta etapa.
- **Escopo M3.0 ainda parcial:** IRX/linking, execução VU0/VU1 e INTC/serviços completos não foram
  implementados. O frame atual não atravessa VU. Evidências, medições, comandos e CI: [M3.0](docs/M3.0-STATUS.md).
- **Validação:** 36 verificações locais e CI do núcleo nos seis alvos aprovadas para `e80c6f2`;
  a workflow separada do launcher tem falhas de instalação Qt no Windows e de teste QML no macOS ARM64,
  registradas no relatório, sem alteração da GUI neste incremento.

## Evidência anterior: M2.9

- **Primeiro frame GS sintético:** 256×160, gerado por instruções R5900/GIF/GS; digest RGBA8 `43564b22…2e3f54`, igual no interpretador e no híbrido. API de framebuffer/eventos compartilhada e exportação BMP disponíveis, sem dependência gráfica obrigatória.
- **BIOS real repetida nos dois perfis SCPH-70004:** mantém 17.690.079 instruções EE, parada em `0x80007D20`; diagnóstico com IDs assumidos mantém 17.690.770 e parada em `0x8000E628`. Zero escritas de pixels em ambas as rotas; não houve avanço do Browser. As máscaras de IMR após reset foram estabelecidas pelo manual; fase vertical e INTC seguem pendentes.
- Relatório, comandos, contratos, testes e limites: [M2.9](docs/M2.9-STATUS.md). A evidência anterior de identificação de firmware permanece abaixo.

## Evidência anterior: M2.8

- **Firmware com NVM fornecidos localmente:** arquivo ROM0 SCPH-70004 (ROMVER `0200EC20040614`) e NVM Dragon de 1.024 bytes, compatíveis por identidade verificada; ambos **fora do Git**. Foram exercitados dois perfis possíveis de placa GH-032 que a NVM não permite distinguir.
- **Boot interpretado e híbrido:** 17.690.079 instruções EE e 2.210.789 IOP, traces equivalentes (desconsiderando marcadores do motor), com mensagens `Initialize Start.` e `Initialize GS ...`. A parada do EE é a leitura do ID `0x7E` do codificador de vídeo (I²C endereço `0x21`, controlador em `0x1A000000` na ROMVER 0200); o valor real não foi medido.
- **Diagnóstico separado, não boot comprovado:** assumir explicitamente o ID do codificador via opção de diagnóstico permitiu prosseguir até a leitura de `I_STAT` do EE após o envio de pacote GIF PATH3. **Não** utilizar esses resultados como confirmação de hardware.
- **IOP após reset do GS:** VBLANK fica não determinado até ser esclarecido o estado do `SYNCV`/PCRT; não inventar eventos nem interrupções.
- **Outras ROM0 reais** (SCPH-70012, SCPH-77008 e firmware japonês de 2006) continuam sem NVM correspondente validada; os resultados anteriores devem ser consultados em `docs/M2.7-STATUS.md`.
- **Testes:** a documentação M2.8 relata 25 testes CTest locais aprovados no Windows x86-64. CI do commit M2.8 deve ser conferida em [GitHub Actions](https://github.com/fabegalo/ps2-native/actions); a suíte remota só é aprovada quando todos os seis jobs terminarem com sucesso.

Detalhamento de endereços, perfis, SHA-256, suposições, componentes e bloqueios: [M2.8](docs/M2.8-STATUS.md).

## Launcher desktop (fundação da interface)

- **Executável `ps2-native`** (Qt 6 Quick, LGPL-3.0 dinâmico; `PS2N_LAUNCHER=AUTO|ON|OFF`): Início, Biblioteca, BIOS, Configurações e Sobre. Gerencia o conteúdo do usuário e **não substitui** Browser, System Configuration nem boot de disco; ações sem runtime aparecem bloqueadas com o motivo.
- **Núcleo compartilhado `ps2n_frontend`**, sem GUI: BIOS por `load_bios_rom0`, NVM por `load_nvm_image` contra o perfil da mesma ROMVER, discos ISO 9660 (2048/2352 bytes, Mode 1/2), CUE multifaixa, leitura por faixa acima de 4 GiB; biblioteca versionada com escrita atômica, sem copiar ISOs ou firmware; contratos futuros do runtime (`RuntimeSession`, renderizadores, controle, áudio) declarados e indisponíveis.
- **Testes:** `launcher-core` (headless, também na CI do núcleo) e `launcher-ui-import`/`launcher-ui-restore` (janela real offscreen: importação, páginas, diagnóstico, persistência e restauração) aprovados no Windows x86-64 com Qt 6.8.3, sem avisos de QML. Novo workflow `launcher.yml` para os seis alvos (Qt 6.8.3 oficial no Windows, Qt 6.4 do Ubuntu, Qt do Homebrew); conferir o resultado por commit no GitHub Actions.
- **Validação gráfica local** (plataforma `windows`): todas as páginas capturadas em 1360×860, 1600×1000 e 1000×660; pasta local com 60 ROM0 reais validadas, uma ISO de DVD de PS2 e um CD de PlayStation em CUE/BIN identificados; diagnóstico numa ROM0 real até o limite de instruções. Conteúdo local fora do Git.
- Limites: jogos, Browser, controle, áudio e renderização dependem do runtime; imagens comprimidas não são lidas; diálogos nativos e arrastar e soltar não foram exercitados automaticamente. Contratos e detalhes: [LAUNCHER.md](docs/LAUNCHER.md).

## I/O do host: caminhos longos e Unicode

- Leitura de BIOS/NVM/ELF, escrita de AOT/traces/fixtures, diretórios e verificações centralizados em `src/io.cpp`. Argumentos UTF-16 preservados desde `wmain`; sem mudanças no Registro ou cópias para contornar limites de caminho.
- Regressão permanente `io-long-paths`: 260, 500 e 1.000 unidades nativas, caminhos relativos/absolutos/estendidos, Unicode, parâmetros FILEPATH do CMake, escrita idempotente e recusas explícitas.
- Windows x86-64/MSVC 19.44: os 25 testes existentes e o novo teste de I/O passaram. UNC real aprovado num compartilhamento local já existente, com caminhos de 277, 511 e 1.019 unidades UTF-16. A CI sem compartilhamento informa explicitamente que UNC real não foi validado.
- Compilação AOT profunda corrigida: o CMake referencia o diretório original por uma junction local ao build, verifica identidade do arquivo e conserva as dependências do Ninja, sem copiar o fonte ou alterar Registro. O runner com fonte Unicode de 983 unidades compilou; a regressão `aot-long-path-build` aprovou compilação/execução, no-op e rebuilds por mudanças no fonte/header em 260/500/1.000 unidades no Windows x86-64. A captura exata do prefixo localizado de `/showIncludes` também foi corrigida. O adaptador requer alvo local e filesystem com junctions; detalhes e limites no relatório de caminhos.

Auditoria, reprodução e limites: [caminhos do host](docs/WINDOWS-PATHS.md). A validação dos seis alvos é acompanhada por commit no [GitHub Actions](https://github.com/fabegalo/ps2-native/actions).

## Pendências principais

1. Obter evidência real dos IDs do codificador de vídeo e do estado GS/IMR/CSR após reset, sem respostas inventadas.
2. Reproduzir VBLANK/INTC EE e manter o sincronismo EE↔IOP após reconfiguração do GS.
3. Validar e implementar a comunicação SIF e DMA com transferência de bytes real e tags/FIFO corretos.
4. Avançar o boot do firmware original até o Browser, sem falsificar dados NVM, periféricos ou compatibilidade.
5. Ampliar o caminho AOT genérico e, depois, os modelos GS/VU/VIF/SPU2, medindo correção e desempenho.

Ver [próxima tarefa](docs/NEXT-TASK.md). Esta lista é dinâmica; não deve ser duplicada no README.

## Histórico de marcos (detalhes nos relatórios)

| Marco | Principal evidência |
|---|---|
| M0–M1.3 | Fundação ELF32, código nativo sintético, indiretos, relocations e semântica R5900 parcial |
| M2.0–M2.2 | Loader ROM0, execução real até a configuração GS/RDRAM, temporização inicial e SIO |
| M2.3–M2.5 | Protocolo MCH/RDRAM, perfis de placa e progresso da BIOS até a memória do IOP |
| M2.6 | Carregamento da NVM do MECHACON, boot IOP, timers e exceções |
| M2.7 | VBLANK, registradores SIF e sincronização dos acessos EE/IOP |
| M2.8 | Primeira ROM0/NVM real pareada; codificador de vídeo e caminho GIF PATH3 |

Revisar a evidência original e os limites de cada etapa nos relatórios [docs](docs/README.md). Não interpretar o número de instruções retiradas como certificação de boot, vídeo ou jogos.

## Launcher — ícone Windows e idiomas

Ícone Windows embutido no executável; seleção Automático/English/Português (Brasil), persistente e com troca imediata. Catálogo com 334 textos da interface; saída técnica original preservada. Quatro testes focados do launcher aprovados no Windows x64, incluindo alternância de idioma e persistência. CI do novo commit local pendente; detalhes e limites em [LAUNCHER](docs/LAUNCHER.md).

## CI do launcher corrigida

`bfa70f2`/`67aed98`: leitura textual do checksum oficial Qt, extração em prefixos separados x64/ARM64 e atalho QML com todas as combinações padrão. **Launcher e núcleo aprovados nos seis alvos para `67aed98`**. [Execução do launcher](https://github.com/fabegalo/ps2-native/actions/runs/41758760249); causas e evidência em [LAUNCHER](docs/LAUNCHER.md). Sem alteração nas capacidades de Browser/jogos.

- **Super Bust-A-Move (SLUS-20115):** ELF em diagnóstico com kernel Sony parcial até 4.486.087 passos (4.483.572 AOT), PC `0x80005F00`; SIF0/IRQ IP3 implementados e regressão com transferência real. Zero frames/menu/gameplay; IOP sem timing GS conhecido impede inicialização. Evidência em [FIRST-GAME-STATUS](docs/FIRST-GAME-STATUS.md).

- Continuação GS, ensaio 92: EE `0x1CECA0` / 387.565.264, IOP `0xD628` /
  52.497.745 ativo. 24 pacotes GIF novos, 1.048.832 escritas comprovadas;
  scanout ainda preto. PSMT8 208x128 é o próximo upload; transferência e
  swizzle de 8 bits implementados com seis testes focados aprovados.
  Modo original repetido após GS conserva PCs/contagens/hash canônicos.
