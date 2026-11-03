# Launcher desktop — arquitetura, contratos e validação

O executável `ps2-native` é o launcher desktop de desenvolvimento: organiza os dumps de BIOS, a NVM do console e as imagens de disco **do próprio usuário** e executa o diagnóstico de boot existente (`ps2n-boot`). Ele **não substitui** o Browser, a System Configuration nem o boot de disco do firmware: essas telas pertencem ao sistema original e só aparecerão quando o runtime puder executá-lo. Nenhuma tela do sistema é simulada; ações indisponíveis aparecem bloqueadas com o motivo.

## Camadas

| Camada | Local | Dependência | Papel |
|---|---|---|---|
| `ps2n_frontend` | `include/ps2native/frontend/`, `src/frontend/` | só `ps2n_core` | identificação, biblioteca persistente, contratos do runtime; testada em todos os hosts (`launcher-core`) |
| Launcher Qt | `launcher/` | Qt 6 (opcional) | janela QML, modelos, processos, automação |
| I/O comum | `src/io.cpp` | — | `write_file_atomically`, `InputFile` (leitura por faixa, 64 bits), `display_path`, `regular_file_size`; Unicode e caminhos longos do Windows |

A camada `ps2n_frontend` não tem dependência de GUI. Todo parser é do núcleo: ROM0 por `load_bios_rom0` (ROMVER, ROMDIR, EXTINFO, SHA-256), NVM por `load_nvm_image` contra o `ConsoleHardware` escolhido, perfis por `known_console_hardware`/`find_console_hardware`. Não há segundo parser de BIOS ou NVM no launcher.

### Identificação

- **BIOS:** arquivo de 4 MiB validado pelo núcleo; versão, região, tipo (console/devel/test), data, arquivos do ROMDIR e perfis de console compatíveis **pela ROMVER**. Sem perfil na base, o diagnóstico roda sem perfil e as leituras de registradores de placa/NVM são recusadas — nunca inventadas.
- **NVM:** anexada a uma BIOS e validada contra o perfil selecionado; um perfil de outra ROMVER é recusado.
- **Discos:** ISO 9660 com setores de 2048 bytes ou brutos de 2352 (Mode 1 e Mode 2 Form 1); descritor primário, `SYSTEM.CNF` (`BOOT2` → PlayStation 2, `BOOT` → PlayStation), serial, região, `VMODE`, mídia, aviso de truncamento. Leitura por faixa: imagens acima de 4 GiB são lidas sem carregar o arquivo. **CUE:** a folha é um disco, identificado pela primeira faixa de dados; as faixas que ela referencia não viram entradas próprias. CSO/ZSO/CHD/gzip são reconhecidos e recusados explicitamente (formato não suportado).
- **Capas:** apenas imagens do usuário (`<serial>.png/.jpg/.jpeg/.webp` numa pasta escolhida, ou imagem ao lado do disco). Nada é baixado.

### Compatibilidade

`Compatibility` é derivada da identificação: `pending`, `missing`, `invalid-image`, `unsupported-format`, `not-ps2`, `runtime-unavailable`. Os estados `boots`, `menus`, `in-game` e `playable` estão **reservados** para evidência futura do runtime e nunca são atribuídos hoje. `needs_attention()` marca apenas problemas do arquivo (ausente, ilegível, comprimido); um disco de PlayStation identificado não é um problema.

### Persistência

- `library.ps2nlib` e `settings.cfg` no diretório de dados (`--data-dir`, `PS2N_DATA_DIR` ou `AppDataLocation` do Qt). Formato texto versionado (`ps2-native library 1`), blocos `[entry]…[end]`, valores escapados, caminhos em UTF-8.
- Escrita atômica (arquivo temporário + rename) por um único worker. Arquivo de versão maior ou corrompido deixa a biblioteca **somente leitura**; ele nunca é sobrescrito.
- Entradas referenciam arquivos no lugar: ISOs, firmware e NVM **nunca são copiados**. A identidade de uma entrada é o SHA-256 do caminho normalizado (sem distinção de maiúsculas no Windows); `FileStamp` (tamanho, mtime) decide quando identificar de novo.

### Diagnóstico de boot

`DiagnosticRequest` → argumentos de `ps2n-boot` (`--expect-sha256`, `--console`, `--nvm`, `--max-instructions`, `--trace=events|full`), executado por `QProcess` sem bloquear a janela, com cancelamento, saída limitada a 100 mil linhas e gravação do log. `parse_diagnostic_line` (núcleo) lê `# result`, `# block point`, SIO da BIOS, `# romver`, `# console`, `# engine` e `# IOP:`. `ps2n-boot` é procurado ao lado do executável (ou no bundle macOS) e pode ser apontado nas configurações.

### Contratos futuros (não implementados)

`runtime.hpp` declara `Capability` (diagnóstico, Browser, boot de disco, controle, áudio, memory cards, GS fiel, renderizador Native), `RendererMode`, `SessionRequest`/`SessionState` e a interface abstrata `RuntimeSession`. Só o diagnóstico de boot está disponível; cada capacidade indisponível carrega o motivo exibido na interface. O launcher não deve ganhar caminhos alternativos para essas funções.

## Interface

Qt Quick/QML com estilo Basic personalizado (tema escuro preto/azul baseado no banner do projeto), páginas Início, Biblioteca, BIOS, Configurações e Sobre; barra lateral recolhível abaixo de 1120 px; detalhes da biblioteca acoplados em janelas largas e sobrepostos nas estreitas; arrastar e soltar arquivos ou pastas; atalhos `Ctrl+1…5` e `Ctrl+O`. Ícones são traços SVG originais renderizados por `QSvgRenderer`. Toda E/S de arquivos roda em `QThreadPool`; erros viram mensagens recuperáveis.

Automação para testes e validação visual: `ps2-native --automate --data-dir <dir> [--import <arquivo|pasta>]… [--expect-entries N] [--expect-valid N] [--run-diagnostic] [--screenshots <dir>] [--window-size LxA]`. Ela importa, espera a identificação, visita cada página, opcionalmente captura PNG e executa o diagnóstico, e confere a biblioteca gravada. No modo automação, avisos do QML fazem o teste falhar.

## Dependências e licenças

- **Qt 6.4 ou superior**, módulos Core, Gui, Qml, Quick, QuickControls2, Svg e o plugin de formatos de imagem (WebP do banner), todos sob **LGPL-3.0, com ligação dinâmica**. Módulos apenas GPL (Charts, Graphs, Quick 3D, Virtual Keyboard…) não são usados. Decisão registrada em [LICENSE-DECISION.md](LICENSE-DECISION.md).
- Sem exigência de AVX; nenhum código específico de CPU no launcher.
- `PS2N_LAUNCHER=AUTO` (padrão) compila o launcher só quando o Qt é encontrado; `ON` exige o Qt; `OFF` desativa. Builds headless e a CI do núcleo não dependem de Qt.

## Validação

- `launcher-core` (headless, roda na CI do núcleo nas seis plataformas): ISO, Mode 1/2 brutos, truncamento, sem `SYSTEM.CNF`, PlayStation, CSO, ruído, ausência, capas, CUE (dados + áudio, faixa ausente, só áudio, vazio), `SYSTEM.CNF` além de 4 GiB num arquivo esparso, BIOS/NVM pelo núcleo, persistência num caminho Unicode com mais de 300 unidades, configurações, busca em pastas, contratos e argumentos do diagnóstico.
- `launcher-ui-import` e `launcher-ui-restore` (plataforma offscreen, backend software): janela real, importação de ROM0 e ISO sintéticas, todas as páginas, diagnóstico com `ps2n-boot`, persistência e restauração pela segunda execução. Workflow [launcher.yml](../.github/workflows/launcher.yml): Windows x64/ARM64 (Qt 6.8.3 oficial), Linux x64/ARM64 (Qt 6.4 do Ubuntu 24.04), macOS Intel/Apple Silicon (Qt do Homebrew).
- Validação gráfica local (Windows 11 x64, plataforma `windows`, Qt 6.8.3): capturas de todas as páginas em 1360×860, 1600×1000 e 1000×660; importação de uma pasta local com 60 ROM0 reais (todas validadas pelo núcleo), uma ISO de DVD de PS2 e um CD de PlayStation em CUE/BIN multifaixa; diagnóstico numa ROM0 real até o limite de instruções, com a mensagem SIO da BIOS. Conteúdo do usuário referenciado no lugar, fora do Git.

## Limites

- Jogos, Browser, controle, áudio, memory cards e renderização não estão disponíveis: dependem do runtime. O botão Jogar fica bloqueado.
- Imagens comprimidas (CSO/ZSO/CHD/gzip), MDF/MDS e folhas CUE que referenciam subpastas não são lidas.
- Sem traduções (interface em inglês) nem pacote de distribuição; o launcher é ferramenta de desenvolvimento, sem release.
- A automação importa pelo mesmo caminho dos diálogos e do arrastar e soltar (`Launcher::importPaths`), mas **não** abre os diálogos de arquivo nem simula o mouse: diálogos nativos, arrastar e soltar real e leitores de tela não foram verificados. A janela real foi exercitada só no Windows; nos demais hosts a evidência é build + testes offscreen da CI.
