# Arquitetura técnica e contratos — M0

## Diretriz

**Decisões vigentes do mantenedor.** Aqui só o contrato e o estado; medições de ensaio ficam nas mensagens de commit e nas evidências locais.

- **10/11/2026 — execução (substitui o runtime próprio descrito adiante).** PS2Recomp fixado em `deps.lock.json` pelo submódulo `external/ps2recomp`, com patches versionados e verificados por hash, no modelo ReXGlue do x360-native. `ps2xAnalyzer`/`ps2xRecomp` preparam cada jogo; `ps2_runtime`/`ps2xIOP` o executam (HLE de kernel/IOP, GS, SPU2, controles, memory cards). `ps2n prepare|run|play <disco>` extrai, gera `config.toml`, recompila, compila `ps2n-game-<id>` e executa; destinos ausentes viram `entry_points` e o jogo é recompilado. O launcher executa `ps2n play`. Patches: `scripts/apply-sdk-patches.ps1 [-Check|-Revert]`.
- **12/11/2026.** Recompilar a rotina original `sceIpuInit` em vez do HLE upstream de endereços fixos (corrompe a pilha do Simpsons). MFIFO e VIF parciais pertencem ao estado único de `PS2Memory`, processados pelo executor EE existente, sem worker extra nem mudança de layout público. O HLE IOP fornece o contrato USB de nenhum periférico conectado; `LGDEVW` continua executando o IRX original; USB/OHCI físico é lacuna explícita.
- **13/11/2026 — meta.** Consolidar um SDK PS2 equivalente ao ReXGlue sobre o PS2Recomp fixado, com os módulos e o ciclo de vida do x360-native. Módulos AOT EE/IOP registrados por identidade do conteúdo, com endereços relativos no loader, reutilizando traduções após relocação; não invalidar objetos EE sem mudança de conteúdo/contrato. Prioridade: Super Bust jogável com som e boa performance.
- **13/11/2026 — heap IOP.** Recompilar `sceSifInitIopHeap` original e atender seus RPCs no mesmo heap físico de sysmem/módulos/HLE, reutilizando blocos liberados.
- **13/11/2026 — Browser.** Preparar e iniciar o OSDSYS original da ROM0 pelo AOT, com recursos da mesma revisão e perfil/NVM correspondentes, no único runtime. Bootstrap IOP por HLE lendo a configuração original; BOOTEND só após conclusão real. O supervisor atenderá transições para jogos; reset integral de hardware não é pré-requisito.
- **13/11/2026 — áudio e vídeo.** SPU2: componente do PCSX2 (via `ext/kzspu2` do KillzoneRecomp) ligado ao mesmo IOP/relógio. Vídeo padrão: GS de CPU do PS2Recomp. O GS do PCSX2 foi aprovado e depois retirado pelo mantenedor após perdas repetidas de dispositivo; não repetir esse experimento como pré-requisito. paraLLEl-GS é um dispositivo Vulkan opcional (`PS2N_PARALLEL_GS`, OFF por padrão; `PS2N_GS_BACKEND=parallel`, `PS2N_GS_MODE=fidelity|enhanced`), sem troca silenciosa de renderer. Fidelidade/Enhanced são modos separados da API; D3D12 exige implementação própria no mesmo contrato.
- **14/11/2026 — SPU2 portável.** Um único SPU2 (kzspu2/PCSX2 fixados) compilado com MSVC, GCC e Clang nas seis plataformas (SSE4/AVX/AVX2 no MSVC x64, SSE4.1 no GCC/Clang x64, NEON no ARM64), com PCM bit-exato verificado por teste em todas. Saída pelo SDL3 do mesmo commit do ReXGlue: S16 estéreo 48 kHz sem conversão, fila ~43 ms com ritmo pelo relógio de áudio, underrun e descarte contados.
- **13/11/2026 — armazenamento.** Supervisor/launcher escolhem uma raiz persistente explícita por console, compartilhada entre Browser, jogos e modos GPU, com o HLE/formato atual. Diretórios existentes não são movidos, mesclados nem descartados automaticamente.
- **13/11/2026 — estrutura de desempenho e vídeo.** Três fases aprovadas: (1) GS em thread própria, com fila ordenada e espera apenas onde estado do GS é lido ou exposto; (2) paraLLEl-GS como GS padrão depois de corrigir a imagem, mantendo o GS de CPU como referência de testes; (3) microprogramas VU1 por AOT com cache por hash, no lugar do interpretador.

**Estado implementado — patches 0010–0047 (13/11/2026).**

- IOP AOT (0024–0030): registro por conteúdo, emissor offline R3000A, verificação de relocação e do código em RAM, despacho paginado, IRX embutidos em ELF EE e API `ps2x_add_iop_aot_module`. Os IRX do Super inicializam sem instruções decodificadas; os do God passam pela preparação, sem execução certificada.
- EE (0031–0041): tabela de funções compacta e preparação incremental por hash de entradas; módulos EE planos por console (`ps2x_add_ee_aot_module`); ExecPS2 para imagem preparada e verificada; recusa explícita de LoadExecPS2/ExecPS2 não preparados; handshake SIF nativo; grupos unity estáveis.
- BIOS: `ps2n prepare-bios|bios <ROM0> <perfil> <NVM>` valida revisão/perfil/NVM, captura e verifica o OSDSYS descompactado e o executa. Chega a `SifIopSync` (RESET `rom0:UDNL rom0:OSDCNF`) aguardando BOOTEND, sem comandos GS: **não há tela do Browser**.
- GS CPU (0042–0044): conversão única para circuitos idênticos e pipeline de pixel inline no MSVC Release; ganho modesto.
- paraLLEl-GS (0046–0047): testes Vulkan sintéticos passam na RTX 4070; no Super a VRAM ainda diverge do GS de CPU (11–12 de 16 blocos de 256 KiB iguais em vsync 127/239). Experimental.
- SPU2 (0022): PCM não silencioso pelo RSPU2DRV original, sem descarte com backpressure na fila existente.
- Memory card (0045): falha de `fflush` em `sceMcWrite` agora retorna erro.
- GS (0048–0049): endereçamento de textura de sprite por coluna/linha (VRAM sintética idêntica) e GS em thread própria (`PS2N_GS_SYNC=1` mantém o controle síncrono). Na cena pesada do Super, 12,9 s → 8,0 s de host nos segundos IOP 27–33; do segundo 30 em diante, tempo real.

**Lacunas abertas.** A imagem do Super está errada nos dois GS (listras, entrelaçamento e metade da altura no CPU; lixo no paraLLEl), o que aponta para o fluxo de comandos antes do rasterizador; gameplay e som contínuo não foram validados. God of War bloqueia no índice de recursos; Killzone teve a compilação interrompida. Raiz persistente compartilhada, VU AOT, overlays EE, D3D12, a origem da divergência do paraLLEl-GS e as demais plataformas continuam pendentes.

Uma base C++20 compartilhada para Windows x64/ARM64, Linux x64/ARM64 (inclui Steam Deck) e macOS Intel/Apple Silicon. Em 07/11/2026, o mantenedor validou a migração gradual para os sete componentes do x360-native: launcher de gerenciamento; conteúdo/catálogo; preparação e execução nativa/AOT; runtime de compatibilidade com HLE explícita; GPU Original/Native; sistema original e jogos; supervisor de sessões e adaptadores de plataforma.

A preparação produz artefatos AOT verificados e reutilizáveis para EE/R5900, IOP/R3000A e VU0/VU1, com identidade, relocação e invalidação. Em 07/11/2026 o mantenedor determinou a remoção completa dos interpretadores, mantendo a arquitetura comum do x360-native. Launcher, CLIs e testes usam agora o mesmo `ps2n_core` AOT/nativo; `ps2n_core_native` é apenas um alias de compatibilidade, não outro runtime. Código sem tradução válida para e informa o artefato faltante. Não há fallback interpretado nem JIT. Fixtures sintéticas de regressão usam palavras constantes compiladas, não um decodificador executável alternativo. Contadores legados de interpretação permanecem para compatibilidade diagnóstica e devem ficar em zero.

Os executáveis/checkpoints históricos em `out/` são evidência preservada; não demonstram o comportamento deste build. O menu anteriormente medido em 7,92–8,91 FPS usava o caminho híbrido antigo. Traduções EE, IOP e VU necessárias devem estar ligadas antes de certificar novamente esse percurso no executável atual.

A **BIOS/Browser original, System Configuration e Memory Card continuam obrigatórios**, como a dashboard original no x360-native. Sua interface deve executar código original traduzido e usar recursos da revisão correspondente. HLE pode fornecer kernel, syscalls e serviços IOP; não autoriza telas inventadas, sucesso falso ou efeitos convidados omitidos. Não é necessário executar cada instrução do kernel original para executar a interface original. GS, DMA, SPU2, controles, memória e temporização continuam no núcleo compartilhado.

A opção validada é híbrida e seletiva: definir contratos de syscalls/kernel, threads/eventos e serviços IOP de arquivos, CDVD, entrada, áudio e armazenamento, substituindo cada serviço somente com equivalência demonstrada. HLE e firmware usam o mesmo `EEState`, `Memory`, IOP, dispositivos, relógios, buffers e persistência. Firmware integral reutiliza o diagnóstico, mas exige maior cobertura de hardware; HLE abrangente reduz essa dependência e aumenta os contratos a validar. Não há HLE completa implementada ou seleção de serviços específicos certificada por esta decisão.

A sessão HLE assistida deve ser identificada separadamente do reset de hardware e dos checkpoints experimentais. BIOS e jogos são alvos do produto; concluir o boot de hardware não deve bloquear trabalho independente no caminho de um jogo. Original e Native compartilham estado e contratos GS; aprimoramentos exigem equivalência de efeitos, lógica e temporização. Esta diretriz define o contrato exigido, não certifica que a implementação já o cumpra.

## Implementação e migração

### Fluxo obrigatório aprovado pelo mantenedor — 07/11/2026

O fluxo de produto é **Importar ISO → Preparar → Jogar**, pelo launcher:

1. Identificar automaticamente o ELF principal (`BOOT2`), módulos IRX,
   código EE/IOP/VU, dependências e recursos, preservando os originais.
2. Analisar e descobrir automaticamente o código e seus destinos de execução.
3. Usar PS2Recomp como SDK-base, no mesmo papel do ReXGlue no x360-native,
   para AOT genérico de R5900, IOP e VU0/VU1 para C++ nativo.
4. Compilar e manter cache por SHA-256, reutilizando artefatos verificados,
   recompilando apenas o que mudou e suas dependências invalidadas, sem
   duplicações desnecessárias.
5. Executar no PS2Runtime central com HLE, integrando os componentes próprios
   superiores no mesmo estado do console, através de GS Original ou Native.

A preparação é automática e universal como mecanismo: não exige ports nem
criação manual de um projeto AOT por título. Isso não garante compatibilidade
com qualquer jogo. Quando a análise não comprovar um destino de execução,
deve informar a limitação; não pode inventar código, sucesso ou recorrer a
interpretação em produção, inclusive para IOP e VU.

HLE atende aos serviços necessários sem depender da execução integral da
BIOS, preservando o Browser do PS2 e sua interface autêntica. A diferença
entre os consoles fica nas instruções, serviços e hardware específicos,
dentro dos mesmos módulos, fronteiras e ciclo de vida do x360-native.
Este contrato orienta a migração; as lacunas de implementação descritas
abaixo continuam explícitas e não representam o fluxo completo funcionando.

### Estado da integração

Em 09/11/2026, o mantenedor validou o SPU2 nativo no SDK, reaproveitando estado e síntese existentes, com drivers IRX AOT, RAM/vozes/relógio únicos e integração ao IRQ/DMA do console. LIBSD HLE não foi selecionada; o tocador VAG do SDK não substitui o dispositivo.

Em 10/11/2026, o mantenedor validou substituir o executor IOP local por AOT no próprio SDK, derivado da semântica upstream. A migração preserva memória/dispositivos/relógio únicos, delay slots, exceções e invalidação de código, sem interpretador. A implementação local só sai quando o substituto correspondente estiver ligado e validado; mover o executor antigo para um patch não satisfaz esta decisão.

O patch `0016` implementa esse executor em `native/iop_cpu.h`, derivado de `ps2xIOP/src/emulator/core/iop_cpu.cpp`. `iop_ops.hpp` fica restrito à identidade da instrução e à chamada AOT; o switch e os auxiliares aritméticos locais foram removidos. Registradores e barramento continuam canônicos, com observações temporárias de conhecimento/proveniência. Regressões nativas cobrem ADDIU parcial, traps/EPC/BD, alinhamento, multiplicação/divisão e JALR para `$zero`, além de IOP/SPU2 e serviços HLE. Isso não certifica o Super ou os demais caminhos ainda locais do EE/GPU.

Direção determinada pelo mantenedor em 09/11/2026: seguir a integração do
x360-native, consumindo o PS2Recomp como implementação de recompilação,
runtime e serviços, com correções genéricas em patches reproduzíveis do SDK
fixado em `deps.lock.json`. O projeto mantém as camadas de produto e os
adaptadores de integração/host; implementações locais concorrentes não devem
substituir o SDK. Apenas transferir código local para um patch não realiza
essa migração. Permanecem obrigatórios AOT offline, estado único do console,
interface original e os contratos comuns Original/Native.

O estado atual ainda não atende a essa direção: `0004-native-aot-emission`
seleciona o emissor local e `src/ps2recomp_runtime.cpp` executa por
`run_ee_boot`; FPU e SPU2 locais originam os bloqueios comerciais registrados.
O mantenedor autorizou iniciar a correção após preservar o trabalho existente
em commit. O primeiro incremento consome a emissão original do SDK para
ADD/SUB/MUL/DIV/SQRT escalares e operações do acumulador sobre uma vista dos
registros canônicos, sem cópia de contexto. O patch
`0009` expõe as operações escalares existentes sem dependência de SIMD e
corrige SQRT.S para ler FT; `0010` isola declarações do backend integrado
para preservar a compilação da saída padrão do SDK. A sessão HLE também
usa essa aritmética no despacho existente; dados de origem desconhecidos
continuam recusados. Isso substitui as provas numéricas locais nesse caminho,
mas não certifica precisão física da FPU do SDK ou compatibilidade comercial.
As permutações PEXEW/PROT3W também usam o emissor do SDK sem depender do
decodificador local, com correção da ordem das palavras no SDK. MOVZ/MOVN usam
o emissor do SDK com a correção low-64 do patch 06 de gow-native-pc e preservam
os certificados e bits desconhecidos dos registradores canônicos. O dispositivo
SPU2 do SDK possui o estado único e a síntese ADPCM/ADSR; latches, integração
DMA/IRQ e avanço do relógio continuam conectados pelo IOP existente. Isso é
uma migração parcial: efeitos audíveis ainda não implementados continuam
recusados. A configuração ESA/EEA é consumida na borda de síntese. Com ESA/EEA desconhecida, somente ganhos de efeitos zero, IRQs desligadas e ausência de transferências permitem avançar, invalidando a cada borda o conhecimento de toda a RAM potencialmente alterada; nenhum valor de registrador ou byte zero é inventado.
Metadados `discovery` são publicados como objeto; recibos antigos com esse
campo em formato de array refazem a descoberta preservando o binário verificado.
A descoberta permite os limites existentes de 64 módulos IOP e 8 regiões EE,
sem o antigo corte intermediário de 25 tentativas. O lock do preparador usa
posse do sistema operacional, liberada ao encerrar o processo, inclusive por
cancelamento forçado; a presença do arquivo de lock não indica posse ativa.
Inteiros, demais operações FPU, VU, dispositivos e o laço `run_ee_boot` ainda precisam migrar para
as implementações/contratos do SDK. As decisões abaixo documentam a integração
anterior e continuam descrevendo os componentes ainda não substituídos.

Decisão do mantenedor em 08/11/2026: substituir o kernel HLE derivado local
pelo HLE e escalonador compilados diretamente do PS2Recomp fixado. Adaptar
somente interfaces ao estado único e ao despacho AOT; remover `third_party`
após migrar seus consumidores e preservar licenças/créditos. A migração
ainda está em implementação e não certifica execução comercial.

Decisão do mantenedor em 07/11/2026: PS2Recomp é o SDK central, com revisão
fixada em `deps.lock.json`. `PS2Runtime` será o núcleo principal e proprietário
do estado único do console. Os componentes próprios de AOT EE/IOP/VU,
hardware/DMA, GS Vulkan/D3D12 e checkpoints serão integrados nesse núcleo;
o runtime antigo completo fica temporariamente isolado para comparação.
A execução principal permanece AOT, sem fallback interpretado. A autorização
de interpretação exclusivamente para validação não introduz esse executor.

Primeiro incremento: o build consome `ps2xAnalyzer` e `ps2xRecomp` na revisão
fixada, com dependências verificadas e patches reproduzíveis em
`patches/ps2recomp`. Reutiliza-se um checkout via `PS2N_PS2RECOMP_SOURCE_DIR`
(o submódulo reutiliza `out/ps2recomp-reference`). Em uma cópia nova, executar
`git submodule update --init out/ps2recomp-reference` e aplicar os patches
listados em `deps.lock.json`, na ordem; o CMake recusa revisão/patch incompatível.
`ps2n-dev prepare-sdk <ELF|ISO> <diretório> [--disc]` valida a entrada com
os parsers existentes, analisa e gera C++ pelo SDK, sem adotar automaticamente
patches, funções omitidas ou stubs sugeridos pelo analisador. A identidade
inclui entrada, lock do SDK e hashes das saídas para reutilização verificada.
Em 08/11/2026, o mantenedor validou um backend integrado exclusivo na classe
`PS2Runtime` do SDK (`PS2N_SDK_NATIVE_BACKEND`). Ele possui EE, memória,
NVM e a ligação ao kernel HLE do SDK; supervisor, launcher e CLIs usam referências a
esse estado. IOP/VU AOT, hardware/DMA, GS, áudio e controles permanecem nos
componentes existentes dentro da memória compartilhada. O backend standalone
do SDK não é ligado a esse executável. Os checkpoints mantêm os campos atuais;
o contrato do SDK e seu lock participam da identidade de runtime.

No perfil AOT, `R5900Context` é um alias do `ExecutionContext`
existente, que referencia EE e memória canônicos. Nas fontes do kernel SDK,
o contexto usa snapshots `EEState` do escalonador; COP0, relógios e dispositivos
continuam globais ao console, preservados pelas interfaces de troca de contexto. Orçamento e observação
usam o despachante AOT comum; os relógios continuam únicos.
O emissor do SDK usa as instruções que ele decodificou e o emissor nativo
compartilhado do projeto, preservando guards por instrução, delay slots,
exceções e invalidação. A saída standalone fica isolada por perfil de compilação.
Os campos `PS2N_ELF_AOT_SOURCE` e `PS2N_SESSION_AOT_SOURCE` também aceitam
`<preparação>/sdk-prepare.json`; o CMake verifica SDK, backend e todas as saídas
antes de compilar, sem copiar traduções. O recibo inclui a identidade do backend
local e `runtime_ready=false`: geração de fontes não certifica binário, cobertura
ou execução de Browser/jogos. Instruções sem tradução seguem a preparação
parcial estrita validada abaixo. Serviços upstream
restantes ainda exigem adaptação.
O interpretador e a memória privados de `ps2xIOP` não estão ligados;
carregamento/relocação IRX e serviços RPC precisam usar AOT IOP antes de
habilitar esse caminho. GPU e checkpoints atuais permanecem preservados.

Em 08/11/2026, o mantenedor validou módulos AOT carregáveis no PS2Runtime
existente para o fluxo Importar → Preparar → Jogar. A preparação deve compilar
offline e manter cache por hash, validando ABI, revisão e invalidação antes
do carregamento. O módulo não possui outro runtime nem estado de console;
usa HLE, EE/memória e dispositivos da sessão existente. Recompilar o launcher
por conteúdo ou criar outro processo de execução não são as opções escolhidas.
Esta decisão não certifica compatibilidade comercial nem módulos já implementados.

Também em 08/11/2026, o mantenedor validou preparação AOT parcial com recusa
estrita. O SDK lista PCs/palavras sem tradução no recibo e não os registra
como código nativo: o bloco retorna antes da lacuna e o despachante para nesse
PC, sem executar, pular ou interpretar a instrução ausente. Instruções válidas
posteriores podem estar traduzidas, mas não autorizam atravessar a lacuna.
Hashes e ABI continuam obrigatórios; preparação parcial não certifica execução.
Fontes traduzidas usam identidade de conteúdo/SDK/emissor; binários acrescentam
ABI, headers e configuração do compilador. Serviços importados da biblioteca
compartilhada podem evoluir sem recompilar o código do jogo quando esses
contratos permanecem iguais. Checkpoints conservam sua identidade de runtime.
As fontes nativas do SDK reutilizam as partições por endereço e o manifesto
verificado `.sources` do emissor AOT comum.

Após nova validação do mantenedor em 07/11/2026, o kernel HLE do PS2Recomp
foi inicialmente adaptado localmente; a migração direta aprovada acima
substitui essa versão, com GPL e créditos em `licenses/PS2Recomp` preservados
(ver `LICENSE-DECISION.md`). `HleKernel` conecta os serviços ao despacho comum
AOT/nativo e à mesma instância de EE/memória. `ps2n-run --hle` / `ps2n-run-native --hle`
e `SessionSupervisor::prepare_hle_elf` iniciam uma sessão assistida explícita,
com acesso COP1/COP2 habilitado pelo ambiente HLE. O reset original e o
diagnóstico bare-metal mantêm seus próprios estados e não ativam HLE.

Em 08/11/2026, o mantenedor validou ampliar o HLE seletivo no runtime
compartilhado para alcançar os menus de Shadow of the Colossus e God of War,
com serviços reais e testes, preservando código original e execução AOT.

A cobertura inicial inclui criação/início/saída/prioridade/espera de threads,
semáforos e eventos, SetupThread, SetupHeap/EndOfHeap, GetMemorySize e FlushCache
(com writeback/invalidação no modelo compartilhado), além do registro e execução
AOT de handlers via SetSyscall, com retorno ao chamador e chamada recursiva ao
serviço original. O registro de handlers usa a própria tabela da sessão HLE
em RAM reservada, sem um segundo mapa de endereços no host. Esse layout HLE
não é apresentado como o kernel de uma revisão de BIOS.
SIF SetReg/GetReg usa registradores físicos existentes e três registradores
de software na mesma RAM; SetDChain arma o canal real. SetDma valida o lote
antes de publicar tags e usa os motores SIF/DMA existentes, sem completar
transferências por antecipação. DMA Stat consulta o estado efetivo do canal.
Callbacks DMA nativos preservam contexto/GP/SP, ordem e máscaras; o ponteiro
opcional de contexto salvo continua desconhecido e seu consumo é recusado.
O escalonador do SDK também observa prazos em ciclos durante o AOT, incluindo
alarmes em laços sem syscalls; o retorno nativo de uma thread torna-a dormente.
As interfaces de inicialização publicam os argumentos `BOOT2` no ABI `sargs`,
habilitam o DMAC real, estabelecem VIF MODE e conectam o perfil NTSC/FIELD inicial
do SDK ao GS e às bordas compartilhadas de EE/IOP. VBlank alimenta os eventos,
flags e contadores do escalonador original; alarmes podem acordar a única thread
em espera, com avanço até o próximo evento físico e limite explícito de um segundo
de tempo guest por espera nativa. Máscaras de IRQ pertencem aos controladores
físicos, inclusive após escrita MMIO. O reset de hardware não herda
esse perfil de compatibilidade; o soft reset GS restaura o perfil da sessão HLE.
O perfil também aplica os contextos e latches iniciais de `GS::reset()` do SDK,
seu armazenamento integral de SMODE2 e a VRAM zerada de `PS2Memory::initialize()`
somente no início; soft reset preserva a VRAM e a contagem de pixels enviados.
O teste headless de God of War USA (`ec9397d2…`) em 08/11/2026 alcançou o texto
de apresentação da Sony em 19.728.260 instruções AOT, sem intérprete. Parou no bind
RPC `0x80000593`. A integração seguinte carrega CDVDMAN e CDVDFSV originais da
BIOS/IOPRP pelo loader do SDK e prepara seus intervalos executáveis em AOT.
Exports originais têm precedência sobre HLE; o transporte SIF consulta o registro
RPC do próprio SDK, executa seu handler nativo e devolve os bytes por SIF0 físico.
O teste comercial passou desse bind e executou o reboot IOPRP com as novas
traduções. RTC é fornecido pelo host (runner também aceita epoch explícito);
perfil/NVM validados chegam ao IOP, sem deduzir MECHACON da BIOS. Com
SCPH-70004:GH-032-32 e a NVM validada do conjunto, a descoberta alcançou GetToc
DVD-9. Os dois PVDs confirmam layer0=2.080.544 e 4.161.520 setores totais;
OTP/PTP não é comprovado pela ISO. O mantenedor autorizou o teste diagnóstico
OTP dessa geometria, que permanece explícita, sem padrão por título.
O RPC usa sua thread/fila original do SDK para suspender/retomar waits; IRQs
físicas alcançam o intrman original. O próximo bloqueio comprovado era thbase:35:
upstream retorna zero em SetAlarm/iSetAlarm/CancelAlarm/iCancelAlarm sem serviço.
O mantenedor validou em 08/11/2026 completar esses quatro serviços no próprio
kernel do SDK, em patch versionado, com callbacks AOT e o relógio/escalonador
únicos, sem kernel paralelo. Os quatro serviços foram implementados e os testes
nativos cobrem repetição, cancelamento e IRQ mascarada. DMACMAN original substitui
o dispatcher sem DMA do SDK; MODLOAD/LOADFILE originais também entram pelo loader
do SDK em AOT. A descoberta comercial em 09/11/2026 passou dos RPCs CDVD e LOADFILE
e parou em `ioman:4` (Open): o IopIoman upstream só oferece AddDrv/DelDrv. Em
09/11/2026 o mantenedor validou completar Open/Close/Read/Lseek no próprio SDK,
chamando os drivers originais em AOT e mantendo memória/kernel únicos.
Essas operações passaram nos testes de ABI/erros e Read bloqueante na thread
original; o adaptador de strings valida até NUL sem exigir todo o limite de strlen.
EnableIntr/DisableIntr de DMA agora usam DICR/I_MASK físicos e entregam IRQ35 em
AOT; a recepção SIF é reconhecida antes de um RPC bloquear, sem antecipar sua resposta.
O jogo passou da espera de CDVD, leu setores do IRX e parou em `loadcore:22`
(ProbeExecutableObject), ainda ausente no SDK.
O mantenedor validou completar no SDK a análise/carregamento de IRX, o vínculo
de imports e o registro de módulos, reutilizando o loader e o catálogo existentes,
sem outro loader ou estado de console. Essa cobertura passou nos testes nativos;
o carregamento comercial avançou até a inicialização MemoryCard via SIO2/DMA,
cuja temporização ainda não está validada. Em 09/11/2026, o mantenedor aprovou
usar o MCSERV HLE existente do PS2Recomp para MCMAN/MCSERV, adaptando transporte
e armazenamento à sessão única, preservando saves e erros. A integração está
validada sinteticamente com leitura/gravação/persistência, diretórios paginados
e RPC IOP/EE distintos. No GOW, MCMAN ativou o backend; a execução avançou de
6.913.046 para 7.096.617 ciclos IOP e parou no poll digital `01 42` do PADMAN
(`ds2u_d`), ainda sem transporte serial validado. MCSERV RPC passou no teste
sintético, mas o jogo ainda não chegou à ativação desse módulo.
Em 09/11/2026, o mantenedor aprovou completar o PADMAN RPC no próprio SDK,
reaproveitando seu HLE libpad, adaptado à entrada real e ao SIF da sessão única.
PADMAN passou nos testes sintéticos de RPC/ABI antigo e novo, entrada real,
modos, pressão, desconexão e saída de atuadores; isso não certifica controle físico.
No GOW, a execução alcançou o streamer original e exigiu SifSetDma/SifDmaStat:
o SDK agora usa o SIF0 físico, com fila limitada, conclusão pelo DMA e payload
opaco, em vez de copiar diretamente para o EE. O reconhecimento da conclusão
continua progredindo durante RPC nativo bloqueante. A regressão transferiu 4 KiB
e outro pedido enfileirado; o diagnóstico comercial avançou ao IRX `ck01`,
preparado offline, e parou em CACHE 0x10 no EE (leitura de tag D-cache).
Em 09/11/2026, o mantenedor aprovou ativar na sessão HLE genérica o modelo
funcional de D-cache existente, mantendo memória, AOT e DMA compartilhados.
A ativação preserva Config convidado, tags desconhecidas e recusas existentes;
não inventa conteúdo de cache nem torna o DMA coerente por cópia adicional.
Passaram a cobertura funcional existente de cache e o teste HLE de DXLTG,
visibilidade de linha suja e writeback via FlushCache do SDK. No teste comercial,
o primeiro bloqueio mudou para DHWBIN no alias uncached 0x20327AC0, antes da
etapa anteriormente alcançada. Em 09/11/2026, o mantenedor aprovou usar no teste
o modo diagnóstico existente de procurar a linha pelo endereço físico. A opção
explícita `--diagnostic-cache-uncached-hits` no runner e na descoberta AOT seleciona
essa hipótese e entra na identidade da descoberta; o padrão mantém a recusa.
Essa hipótese não tem comprovação de hardware.
A sessão HLE fria inicializa tags inválidas por DXSTG no cache compartilhado,
como estado de software do kernel, preservando os dados e bits reservados
desconhecidos. Reset físico mantém PFNs indefinidos; cache pré-configurado não
é reinicializado, preservando linhas sujas existentes.
Com a tradução física diagnóstica e essa inicialização, GOW executou
19.912.349 instruções EE AOT e 377.906 IOP nativas, zero interpretadas, e chegou
à escrita SPU2 0x7CA=8 pelo driver original. Seus efeitos permanecem desconhecidos;
em 09/11/2026 o mantenedor aprovou usar o modo diagnóstico existente de retenção
por escrita de 16 bits, sem afirmar efeitos de áudio desconhecidos. A opção
`--assume-spu2-7ca-latch` do runner e da descoberta AOT seleciona essa hipótese;
leituras, larguras inválidas e o modo padrão continuam recusados. A seleção
é registrada na identidade e no recibo da descoberta. A regressão SPDIF passou
em execução exclusivamente AOT, incluindo reset, leituras e larguras inválidas;
o corpus sintético foi completado e o teste recusa tradução nativa ausente.
No teste com essa opção, o driver passou por 0x7CA e avançou de 10.820.420
para 10.828.264 ciclos IOP (385.750 instruções nativas), parando ao habilitar
o SPU2 sem estado inicial de vozes/entradas estabelecido. A hipótese existente
de reset silencioso foi aprovada pelo mantenedor em 09/11/2026 para o próximo
diagnóstico. A opção explícita `--assume-spu2-quiescent-reset` ativa o estado
existente somente na sessão HLE ainda não executada, antes da configuração de áudio,
e entra na identidade/recibo da descoberta. Não altera transferências, DSP ou
recusas de efeitos não implementados, nem comprova o reset no hardware.
O teste HLE de seleção antes da execução e recusa após configuração passou.
No GOW, o driver avançou até 10.828.503 ciclos IOP (385.980 instruções nativas),
parando no FIFO PIO por ausência de um intervalo explícito por palavra.
Em 09/11/2026, o mantenedor aprovou usar no diagnóstico comercial 1 ciclo
IOP por palavra PIO de 16 bits, o mesmo parâmetro dos testes existentes.
A opção `--assume-spu2-pio-word-cycles=1` seleciona a transferência FIFO/RAM
existente e entra na identidade/recibo da descoberta; não é medição de hardware.
Passaram a regressão FIFO→RAM em um ciclo, a recusa de seleção tardia e os
controles de intervalos inválidos. No GOW, PIO avançou até o contrato síncrono
do RPC 104 do streamer: 19.916.845 instruções EE AOT, 2.317.600 IOP nativas,
zero interpretadas. A medição temporária contou 1.953.465 instruções do worker
em 2.000.056 totais; as demais threads não explicam a maior parte do limite.
A tabela consultada tinha 234 entradas legíveis. A instrumentação foi removida
após preservar a medição compacta. A proposta de continuação RPC no próprio
escalonador SDK, com resposta SIF apenas na conclusão, foi aprovada em 09/11/2026.
Em 09/11/2026, o mantenedor aprovou expor no runner HLE e na preparação
o diagnóstico SPU2 existente de latches não atribuídos para investigar Super
Bust-A-Move. A seleção é explícita, entra no recibo e não valida áudio;
leituras e acessos inválidos continuam recusados. O ensaio chegou à leitura EEA
em 0x1F90033C. O mantenedor aprovou testar o diagnóstico inicial existente
com EEA 0xE/0xF; a opção explícita usa decimais 14:15 e integra o recibo.
Esses valores são hipótese de inicialização, não medição de hardware. O ensaio
chegou a 138.361.795 instruções EE AOT e 253.053 IOP nativas, zero
interpretadas; parou ao habilitar efeitos sem ESA inicial conhecido. Somente
a limpeza da VRAM foi observada, sem menu, gameplay ou FPS medido.
A continuação mantém o callback na thread original, preserva o quadro RpcLoop
e usa as mesmas fatias/relógio; não aumenta o antigo limite de chamadas síncronas.
Cada chamada pendente conserva seu pacote na RAM IOP canônica; filas RPC
distintas progridem no escalonador existente sem reusar esse pacote. A mesma
fila recusa uma segunda chamada inacabada. Transferências SIF0 submetidas
pelo callback precedem sua resposta, preservando a ocupação real do transporte. A sessão e a espera limitada do escalonador EE continuam supervisionadas. O modelo numérico existente de vozes
não dispensa validação desse estado nem implementa automaticamente todo o DSP.
Em 09/11/2026, o mantenedor aprovou unificar as reservas de imagens, pilhas,
metadados e sysmem no alocador existente do SDK, na RAM IOP única.
A faixa gerenciada passa a 0x10000–0x1F0000; buffers baixos do kernel e pilhas
altas de chamadas permanecem reservados fora dela. Imagens relocáveis exigem
nova preparação AOT. O limite anterior de 832 KiB recusava o pedido real de
0x11ADF8 bytes do streamer; o ponteiro zero precedia a corrupção do receptor SIF.
A regressão HLE passou com imagem reservada, alocação grande sem sobreposição,
liberação de falha de carga e proteção das reservas baixas. Onze regiões IOP
foram preparadas novamente, mantendo ABI/cache EE. No teste comercial limitado,
o pool recebeu 0xD5200 e o RPC concluiu; o receptor SIF permaneceu em 0x3277C0.
O jogo passou de 49.111.082 para 108.694.210 instruções EE AOT, com 7.245.364
IOP nativas e zero interpretadas. O novo bloqueio em 0x283ED4 consome incerteza
originada em MUL.S 0x1187F4 (1.0 × 0x39C90FD8); o diagnóstico escalar existente
foi aprovado pelo mantenedor em 09/11/2026 para o próximo teste. A opção explícita
`--assume-ee-early-digit-carry` reutiliza o modelo escalar existente, entra na
identidade/recibo da descoberta e permanece hipótese de referência externa,
sem medição no console. Os demais produtos incertos continuam recusados.
A regressão FPU e quatro controles negativos dos CLIs passaram. O teste
comercial passou do bloqueio escalar para 113.419.490 instruções EE AOT e
7.247.722 IOP nativas, zero interpretadas. O bloqueio seguinte é cdvdman:28
(sceCdStatus): o driver original entrega o byte de status 0x0A com bits 6–7
MMIO desconhecidos; SLTIU em 0x82954 exige o valor inteiro. A seleção proposta
do serviço HLE existente do SDK com estado real do drive foi aprovada em
09/11/2026. Apenas cdvdman:28 usa essa fronteira: consulta o estado canônico
do drive, sem retornar pausa constante nem definir bits MMIO desconhecidos.
As demais exportações seguem o provedor original AOT, com DMA/IRQ e erros
na sessão única; ausência de mídia e IOP parado continuam recusados.
A regressão HLE passou com exportação original sentinela, busca→pausa e IRQ,
sem relaxar o MMIO. No GOW, cdvdman:28 avançou para 118.145.244 instruções EE
AOT e 7.250.244 IOP nativas, zero interpretadas, ainda no logo. A nova fronteira
é a incerteza de MUL.S no índice 2099 da tabela inicial. Comparação isolada dos
4096 produtos com o oráculo inteiro PS2Float de parallel-ps2 deixou cinco
incertos no diagnóstico atual, zero divergências entre os 4091 aceitos. Os
cinco resultados do oráculo coincidiram com a multiplicação normal do SDK;
nenhum vetor físico consultado cobriu esses casos incertos. A proposta de um
diagnóstico separado, com escolha SDK limitada ao intervalo certificado,
aguarda validação. O oráculo não foi incorporado ao runtime.
O framebuffer permanece no mesmo logo,
SHA-256 c6d81d717994f47e6a2ff12c7b4b55e9d05685cb2094269c5ae6cecccd235fc9.
A imagem permaneceu no logo (671.760 pixels escritos); não há áudio/gameplay
comprovado.
Os demais IRX continuam pelo loader/kernel do SDK em AOT. Metadados persistentes
e formatação do cartão ainda param explicitamente; não retornam sucesso fictício.
Não há menu/gameplay comprovado.
O mantenedor autorizou em 08/11/2026 usar diretamente o loader e o kernel IOP
do PS2Recomp com módulos IRX preparados offline em AOT, adaptando as interfaces
à memória e ao relógio únicos, sem outro kernel local ou fallback interpretado.
Isso não certifica apresentação, áudio, menu ou gameplay.
Arquivos/RPC, callbacks específicos de vídeo,
cobertura integral de dispositivos e inicialização completa de jogos/Browser
continuam pendentes. Um
serviço desconhecido ou ausência de thread executável produz recusa explícita.
O formato v1 de checkpoints recusa sessões HLE, inclusive exportação de hash
de estado completo, até incluir os objetos do kernel. Desligar HLE não torna
esse estado serializável: é necessário um novo reset/preparo. Saves antigos não mudam.

Em 08/11/2026, o mantenedor autorizou inicializar a TLB do kernel de 32 MiB
na sessão HLE, mantendo a mesma memória, ASID, permissões, cache e recusas
de regiões não mapeadas. A RAM e o scratchpad HLE passam pela TLB existente;
alterações convidadas invalidam também os guards de código nativo. O reset
do Browser original continua separado e não herda essas entradas.

Com essa inicialização, Super Bust-A-Move (USA) retirou 4.484.175 instruções
AOT e parou em `SifSetDChain` (`0x78`), ainda sem implementação HLE. Shadow
of the Colossus (USA) retirou 1.397.118 e God of War (USA), 1.578.074,
antes de recusar leitura em `0x00000004` após a busca de handler na RAM.
O espelho da tabela de syscalls continua uma lacuna. Todos mantiveram
interpretação em zero e não escreveram no GS; não demonstram gameplay ou
boot original. Em 08/11/2026, o mantenedor validou a tabela de syscalls na
RAM canônica e serviços SIF ligados aos dispositivos EE/IOP existentes,
com layout verificado e recusas estritas de serviços/execução AOT ausentes.
A alternativa de inicializar kernel/IOP pelo firmware AOT não foi escolhida
para este incremento; memória, dispositivos e relógios continuam únicos.
O mantenedor também validou descoberta AOT limitada em Preparar: execução
nativa para no código ausente, captura/verifica a região, compila/cacheia
offline e reinicia dentro de limites explícitos. Jogar apenas carrega esses
overlays quando os bytes correspondem à RAM; não compila nem interpreta.
O incremento cobre regiões EE com escrita convidada observada, até oito
regiões de no máximo uma página de 4 KiB, sem promover padding não escrito
a código. Cada início tem orçamento de 50 milhões de instruções; a descoberta
verifica o limite de 240 segundos entre etapas/fatias de execução, e cada
compilação de região possui limite próprio de 180 segundos.
ELF físico e alias KSEG permanecem distintos; conteúdo, ABI e alias identificam
o módulo. A ativação confere a região inteira pela visão de fetch antes do
primeiro bloco; os guards existentes recusam alterações posteriores.
Em 08/11/2026, o mantenedor validou serviços de sistema IOP HLE com relógio,
RAM e dispositivos canônicos: bootstrap real de buffers/handlers, SIF CMD/RPC
e arquivos/CDVD. Código convidado de módulos originais continua exigindo AOT,
com recusa de serviços/traduções ausentes. A inicialização integral do kernel
IOP pelo firmware AOT não foi escolhida para este incremento. Essa autorização
não certifica a cobertura desses serviços nem o boot de jogos/Browser.
O primeiro incremento inicializa buffers/registradores na RAM IOP e o DMA
SIF1 canônico, despacha CMD de endereço/inicialização/registradores e devolve
RPCINIT por SIF0 real. O relógio avança sem contabilizar instruções IOP
inexistentes; reset original e carga bare-metal removem esse modo. Arquivos,
servidores RPC restantes e execução de módulos ainda são lacunas explícitas. Os callbacks
EE INTC/DMAC compartilham preservação de contexto e executam código AOT.
As regressões sintéticas de HLE, posse SDK e sessão passaram; isso não
estabelece execução de jogos nem boot original.
O mantenedor reafirmou reutilização máxima do PS2Recomp, incluindo serviços,
escalonadores e ferramentas, com adaptação às fronteiras já aprovadas.
O carregador ELF/IRX e as rotinas de relocação de `ps2xIOP` são compilados
diretamente do SDK fixado: o patch 0006 troca somente sua interface de memória
por um adaptador que referencia a RAM IOP canônica. Parser de caminhos também
vem do SDK. Regressões cobrem carga, recusa de ELF inválido, início de IRX e
threads pelo kernel original, prioridade dos exports e RPC sobre RAM IOP única,
sem intérprete. `prepare-native` prepara também os intervalos IRX ausentes e
o recibo carrega suas DLLs com verificação de conteúdo/ABI e vida útil registrada.
Na espera EE por SIF, a adaptação de `EeScheduler::waitForEvent` avança somente
o relógio compartilhado até IRQ real, limitada a 65.536 ciclos IOP por espera;
sem evento ou thread executável, a recusa continua explícita.
Kernel IOP e CDVD também são compilados do `ps2xIOP`, com adaptador para a
imagem montada e RAM existentes. O servidor `0x80000592` faz bind e inicializa
CDVD pela implementação upstream; resultado e recibo retornam pelo SIF0 real.
O heap RPC usa o alocador upstream sobre a mesma RAM; reset HLE valida o IOPRP
solicitado antes de reinicializar os serviços, preservando estado EE e relógio.
Respostas de versão usam ROMDIR/EXTINFO do IOPRP ativo ou da BIOS validada,
compartilhada por referência em Preparar/Jogar. O hash dessa dependência invalida a descoberta,
sem recompilar código de jogo apenas por mudar a BIOS. As regressões cobrem
resposta de 4/16 bytes e recusa de metadados ausentes, sem certificar gameplay.
O Super Bust ainda para no bind CDVD `0x80000596`, após corrigir a validação
comum de descritores de semáforo; não há imagem ou gameplay comprovados.

| Componente | Base existente e fronteira restante |
| --- | --- |
| Launcher | Qt/QML em `launcher/`: catálogo, configuração e início; não substitui o Browser. O worker mantém a adaptação Qt e os comandos de pausa/parada. |
| Conteúdo/catálogo | `frontend/content` e `library`: identificação, caminhos, perfis e persistência sem copiar originais. A seleção de todas as dependências AOT antes de Play ainda não é automática. |
| Preparação/AOT | `ps2n-dev`, `ps2n-aot`, análise e partições CMake: traduções EE/IOP/VU. O launcher prepara/cacheia módulos carregáveis por hash e ABI; módulo existente não prova cobertura do conteúdo. |
| Runtime + HLE | `boot`, `Memory`, EE/IOP e dispositivos conservam estado e semântica compartilhados. O kernel HLE inicial está conectado às syscalls; cobertura de sistema/IOP e equivalência comercial ainda são parciais. |
| GPU Original/Native | `gs_*`, shaders e backends software/Vulkan/DirectX. API gráfica de host não equivale ao modo Native aprimorado, ainda indisponível. |
| Sistema e jogos | ROM/NVM, disco, reset, ELF diagnóstico e checkpoints têm identidades distintas. Browser completo e gameplay não são estabelecidos por builds ou fixtures. |
| Supervisor/adaptadores | `frontend::SessionSupervisor` possui EE/memória/NVM e prepara/executa a mesma sessão para boot e restauração. `ExperimentalSession` adapta esse núcleo a Qt, entrada, áudio e apresentação. Retorno autêntico sistema↔jogo e contratos completos de serviços/adaptadores continuam pendentes. |

Primeira migração: retirar do worker Qt a preparação do boot original e a posse do estado do console, preservando validações de revisão, módulos, checkpoints e recusas. Não se cria outro runtime nem outro estado para GPU/HLE. A restauração experimental usa a mesma instância; sua leitura de manifesto e a seleção de módulos ainda estão no adaptador Qt.

## Contratos registrados nos marcos anteriores

As tabelas e seções históricas abaixo descrevem implementações e evidências de cada marco. Não impõem boot integral, ausência de HLE ou política permanente de fallback; prevalece a diretriz validada acima.

| Componente | Entrada | Saída | Garantias / falhas |
|---|---|---|---|
| `read_file(path)` / `write_text_if_changed(path, text)` | caminho nativo Unicode do host | leitura binária / escrita idempotente | I/O centralizado, 128 MiB de limite de entrada; no Windows CLI `wmain` para paths UTF-16 e prefixo Win32 estendido para long paths, sem editar Registro |
| `parse_ee_elf(span<uint8_t>)` | bytes não confiáveis | `ElfImage` com `PT_LOAD`, entrada, símbolos ELF `STT_FUNC` verificados, evidências de relocation (`R_MIPS_32`, `R_MIPS_26`, pares `HI16/LO16`), contagem de tipos não usados, bytes próprios | valida ELF32 LE/MIPS/ET_EXEC, tabelas de símbolos, limites, alinhamento, sobreposições, faixa EE física e `e_entry`; relocations são lidas como evidência e **nunca aplicadas**; pareamento `HI16/LO16` do ABI; erros explícitos para metadados inconsistentes |
| `Memory::load(ElfImage)` | segmentos validados | 32 MiB RAM carregados, zero BSS; acessos de 8/16/32/64 bits | modo `ElfProgram`: sem alterações ao ELF de entrada; só RAM física e seus espelhos KSEG0/1; desalinhamento e MMIO falham explicitamente |
| `Memory::reset_for_boot(rom0)` | ROM0 validada | modo `HardwareReset`: RAM, ROM0 em `0x1FC00000`, scratchpad, TLB de 48 entradas, registradores do EE | KSEG0/1 pelo barramento físico; kuseg/KSEG2/3 só pela TLB escrita pelo convidado (V/D/G/ASID; entrada S cobre os 16 KiB do scratchpad; entradas nunca escritas não correspondem); regiões de dispositivo nomeadas e `BusError` tipado para toda recusa; observador de acessos para o trace |
| `EeRegisters` (`ee_registers.hpp`) | endereço físico `0x10000000..0x1000FFFF`, GS `0x12000000..0x12001FFF` ou janela do IOP `0x1F800000..0x1F9FFFFF`, tamanho, relógio do EE | timers T0–T3 (`COUNT/MODE/COMP`), latch `0x1000F500`, EE SIO (TX, `SIO_ISR`, configuração), latches DEV9 `0x1F801470/2`, *tool board id* `0x1F803204` (byte `0xFF`, hipótese de varejo), delegação ao `GsRegisters` e ao `MchRegisters` | relógio de 1 ciclo por instrução retirada (hipótese), mais stalls modelados de Q, compartilhado com `COP0 Count`, o GS e o MCH; HBLANK vem do relógio de linha do GS; tudo que não é modelado é recusado com motivo (`DeviceRefusal` → `BusError`) |
| `GsRegisters` (`gs_registers.hpp`) | endereço físico do GS, tamanho, valor, ciclo do EE, guarda de retemporização | `SMODE1/SMODE2/SRFSH/SYNCH1/SYNCH2/SYNCV` retidos (64 bits), escritas de `CSR` (`RESET`, reconhecimento), relógio de linha (NTSC/PAL, `SINT` suspende) | independente de backend, nada é desenhado; bits reservados, outras larguras, leituras e retemporização com timer em HBLANK recusados; recusa não altera o estado |
| `MchRegisters` (`mch.hpp`) | endereço físico `0x1000F400..0x1000F4FF`, tamanho, valor, ciclo do EE | `MCH_RICM` (`0x1000F430`): valida e inicia uma transação serial (layout dos auxiliares da BIOS), cujo efeito na RDRAM só vale na conclusão; bit 31 ocupado por comprimento em SCK × 1000 ns (hipótese); `MCH_DRD` (`0x1000F440`): `SD` de escrita e do último `SRD` | `F400`, `F410`, `F420`, `F450–F4C0` recusados com o registrador nomeado; escrita durante a transação, `CLRR` antes de 16 SCK após `SETR` e outras larguras recusadas; recusa não altera o estado |
| `RdramChain` (`rdram.hpp`) | requisição serial (`SOP`, `SBC`, `SDEV`, `SA`, `SD`) | cadeia Direct RDRAM do datasheet de 64/72 Mbit: `SETR`, `CLRR`, `SETF`, `SWR`/`SRD` de `INIT`, `TFRM`, `TRDLY`, `TCYCLE` e `TEST77`, repetidor SIO (`SRP`) e seleção por `SDEVID` | `SOP` reservados (`0xB`/`0xE` da BIOS), `TCEN`/`TCAL`, outros registradores, acesso antes do `SETR` e `SRD` sem resposta ou com várias recusados sem alterar estado |
| `EEState` | estado por execução | 32×`Register128`, LO/HI (pipeline 0 e 1), 32 FPRs crus, PC, pendência de branch e de transferência indireta, COP0, PC da instrução em curso | `ee_reset_state()` dá o estado de reset do EE; `$zero` imutável; operações de 64 bits preservam a metade alta, `LQ/SQ` movem 128 bits; sem global; sem alegar cobertura total R5900 |
| `analyze_control_flow(ElfImage, entry_roots, tables)` | entrada ELF, funções simbólicas, evidências de relocation, raízes e tabelas opcionais | mapa de instruções, blocos, funções, entradas indiretas verificadas, jump tables provadas e `JR` não resolvidos com motivo | fluxo direto, alvos `JAL` como funções, tabelas provadas por `SLTIU` + `BEQ $zero` e `JR` constantes provados por `LUI` + `ORI`/`ADDIU`, ambos em janela linear sem entradas externas; ponto fixo e reverificação final; nada é aceito por semelhança |
| `generate_aot_cpp(ElfImage, AotOptions)` | CFG e raízes verificadas | TU C++20 com funções por bloco, tabela de despacho e tabela de entradas indiretas; cabeçalho com proveniência | rejeita opcode/delay slot não suportado; **sem** JIT/interpretador implícito; com `interpreter_boundaries` (só `--bios`) lista as fronteiras para o interpretador em vez de falhar; módulo exporta tipo e SHA-256 da entrada |
| `load_bios_rom0(bytes, expected_sha256)` | dump de ROM0 do usuário | `BiosImage`: SHA-256, ROMDIR, EXTINFO, ROMVER (versão, região, tipo, data) | exatamente 4 MiB; ROMDIR/EXTINFO autoconsistentes; recusa qualquer inconsistência; nunca embute firmware |
| `ee::exec<Op>` (`ee_semantics.hpp`) | estado, memória, pc e palavra | efeito R5900 de uma instrução inteira, incluindo MULT/DIV do pipeline 1 (LO1/HI1) e `PADDUW` | fonte única para AOT e interpretador |
| `interpret_instruction(ctx)` | estado no PC atual | uma instrução, inclusive COP0/TLB/ERET/EI/DI/CACHE | para com `GuestStop`, `BusError` ou instrução não implementada; nada é ignorado |
| `prepare_ee_boot` / `run_ee_boot` | `BiosImage`, módulo AOT opcional | `BootReport` (trace, cauda de instruções, ponto de bloqueio) | estado de reset do EE; bloco AOT só no PC exato, senão interpretador registrado; recusa módulo de outra ROM0 |
| `run_recompiled(EEState&, Memory&, stop, budget)` | TU C++ já compilada e estado | número de instruções executadas | delay slots (incluindo anulação de branch-likely), `JR/JALR` só para entradas verificadas, valida instrução não modificada em RAM; rejeita PC dinâmico não coberto, limite esgotado e memória inválida |
| `ps2n-aot` | ELF (ou ROM0 com `--bios`) e destino do `.cpp`, `--entry`/`--table`/`--expect-sha256` opcionais | código traduzido e resumo de evidências | reusa `.cpp` idêntico; nunca copia o ELF/jogo/firmware inteiro |
| `ps2n-boot` | ROM0 do usuário, orçamento, nível de trace | identidade da BIOS, ROMDIR, trace, cauda de instruções, ponto de bloqueio | interpretador puro, ou híbrido quando compilado com a tradução da mesma ROM0 (`PS2N_BIOS_AOT_SOURCE`) |

## Direct ELF e regiões dinâmicas verificadas

`prepare_direct_elf` é um ambiente de desenvolvimento bare-metal explícito, separado de `prepare_ee_boot`.
Carrega `PT_LOAD`/BSS pelo loader comum, inicia kernel mode sem interrupções, SP alinhado e GP explícito;
não instala kernel Sony, serviços HLE ou respostas de syscall. `Memory::DirectElf` mapeia RAM baixa e
scratchpad, enquanto KSEG0/1 usam os mesmos dispositivos do boot ROM0. O IOP fica parado até um payload
R3000A explícito, carregado por `load_iop_program`; isso não é um loader/linker IRX.

Ambos usam `run_ee_boot`, semântica R5900, clocks, IOP, GIF, GS e framebuffer comuns. Count/Compare
sinaliza IP7 e entrega a exceção nos dois motores, com EXL/EPC/BD/BEV e ERET; mantém a hipótese de
um ciclo por instrução, acrescido dos stalls modelados. INTC do EE e timing superscalar continuam incompletos.

`snapshot_ee_code` exporta uma região explícita de RAM Direct ELF somente após conferir um SHA-256
independentemente conhecido; produz ELF determinístico para regeneração AOT offline. `VerifiedAotOverlay`
confere ELF, módulo compilado e bytes já carregados, vincula a prova à instância de memória e permite
retomar o dispatcher comum. Cada instrução AOT continua conferindo seus bytes; modificação posterior
falha explicitamente. Não há JIT, execução de compilador em runtime ou cache de firmware compartilhado.

SIF1 implementa o subconjunto EE canal 6 normal → FIFO limitada → IOP canal 10 destination chain,
com transferência real e backpressure. VIF1 implementa NOP/DIRECT → GIF PATH2; outros comandos e
arbitragem concorrente falham. Contratos, métricas, instruções e limites em [M3.0](M3.0-STATUS.md).

## Organização futura (interfaces propostas, NÃO implementadas)

- **disc/importer:** detectar disco, serial, região, hash e preservar caminho original; biblioteca indexada sem copiar ISOs.
- **EE ISA/dispatch:** cobertura R5900/MMI/FPU/COP0, descoberta de funções, ELF relocável, overlays, saltos indiretos, syscalls e cache de tradução. Preservar os alvos AOT e de referência existentes durante a migração; mudanças da política de fallback/JIT exigem validação.
- **EE memory/bus:** tradução de endereços, scratchpad, DMA, dispositivos, interrupções, atomicidade, proteção e invalidação de código modificado.
- **IOP:** R3000A, IRX, SIF/RPC, CDVD e sincronização.
- **VU0/VU1 + VIF/GIF/DMA:** macro/micro, tempos e semântica vetorial reais.
- **GS (fiel):** interfaces de comandos VRAM/texturas, provas de imagem e backends modernos; **Native** renderizará através de contratos compartilhados, nunca alterando física/temporização.
- **SPU2, controles e memory cards:** I/O persistente e seguro, áudio e sincronização.
- **BIOS e HLE:** executar a interface original traduzida AOT com recursos do usuário e serviços HLE nativos quando necessários; preservar revisões/regionais e não distribuir firmware. (M2.0 já valida a ROM0 e inicia o EE; a M2.6 carrega a NVM do MECHACON do dump do usuário, validada contra o perfil; ROM1/ROM2/EROM e a maior parte dos dispositivos seguem pendentes.)
- **launcher:** gerenciador gráfico, importação sem duplicar, AOT automático, diagnóstico, salvamento e execução nativa da BIOS original e dos jogos, com serviços HLE quando necessários.
- **platform:** filesystem Unicode, paths longos, SIMD, memória executável, GPU, áudio e input isolados por backend; API multiplataforma única.

## Posição sobre referências

- **PS2Recomp**: kernel HLE incorporado por adaptação da revisão `e5636981…`, conforme validação acima. GPL-3.0 e proveniência preservadas; limites e stubs do upstream não são prova de compatibilidade. O restante permanece disponível para integrações revisadas, sem substituir silenciosamente o runtime compartilhado.
- **PCSX2**: `pcsx2/Interpreter.cpp`, `pcsx2/Memory.cpp`, diretórios R5900, GS, SPU2, IOP: referência principal para especificação observável e testes futuros; não reaproveitar código GPL-3.0+.
- **Play!**: HLE do sistema é candidato aos serviços nativos autorizados; executar a interface original ainda exige seus próprios artefatos AOT e recursos. Licença tipo BSD 2 cláusulas, mas conteúdo deve ser revisado caso a caso antes de integrar.
- **N64Recomp**: modelo útil de separação compilador/runtime (licença MIT); arquitetura de N64 não é compatível com o PS2 sem projeto específico de hardware.

Ver `LICENSE-DECISION.md` para a decisão completa.

## M1.1 — Critérios verificados

Consultar [M1.1-STATUS.md](M1.1-STATUS.md) para casos de teste, limitações e contrato de despacho. O modo GS/Native e o sistema original do PS2 continuam **não implementados**.

## M1.3 — Evidências, provas e entradas indiretas

Fluxo de evidência, do mais forte ao mais fraco, sem heurística de semelhança:

1. **Metadados ELF verificados:** `e_entry`, `STT_FUNC`, relocations `R_MIPS_32`/`R_MIPS_26`/`HI16+LO16` conferidas contra os bytes vinculados (`src/relocations.cpp`).
2. **Fluxo direto:** alvos de desvios, `J`, `JAL` (raiz de função) e continuações de chamada.
3. **Provas no código:** jump tables com limite `SLTIU` + guarda `BEQ $zero` em janela linear sem entradas externas, iteradas até ponto fixo e reverificadas ao final (`src/control_flow.cpp`).
4. **Indicações explícitas do desenvolvedor:** `--entry`, `--table` (validadas, nunca embutidas por jogo).

Em runtime, `JR/JALR` só alcançam `indirect_entries` (raízes, continuações, alvos de tabelas provadas); o resto falha fechado. Detalhes, testes e limitações: [M1.3-STATUS.md](M1.3-STATUS.md).

## M2.0 — Boot do EE a partir do vetor de reset

```
ROM0 do usuário ──load_bios_rom0──► BiosImage (SHA-256, ROMDIR, EXTINFO, ROMVER)
        │                                   │
        │ ps2n-aot --bios                   │ prepare_ee_boot (recusa módulo de outra ROM0)
        ▼                                   ▼
AotModule (blocos + SHA-256) ──► run_ee_boot ──► bloco AOT no PC exato ──┐
                                      │                                  ├─► ee::exec<Op> (semântica única)
                                      └──► interpret_instruction ────────┘     + COP0/TLB/ERET/CACHE no interpretador
                                                   │
                       Memory (HardwareReset: RAM, ROM0, scratchpad, TLB, dispositivos nomeados)
                                                   │
                         BootReport: trace reproduzível, cauda, ponto de bloqueio explícito
```

No marco M0/M2.0 histórico, o interpretador era o fallback **explícito**: cada troca de motor aparece no trace e nada que não seja modelado é ignorado. Detalhes, testes e limitações: [M2.0-STATUS.md](M2.0-STATUS.md).

## M2.1 — Guiado pela BIOS real

Cada bloqueio da ROM0 do usuário vira o menor modelo sustentado por evidência (`EeRegisters`, MMI, `Count`, correções da TLB), com teste sintético próprio; o firmware e seus traces ficam fora do repositório. Estado, ponto de bloqueio atual e hipóteses: [M2.1-STATUS.md](M2.1-STATUS.md).

## M2.2 — GS privilegiado e relógio de linha

`GsRegisters` guarda só o estado de controle do CRTC (nenhum backend gráfico). `SMODE1` define o relógio de linha que gera o HBLANK dos timers do EE, a partir do mesmo relógio do EE usado por `COP0 Count`. A análise de fluxo prova `JR` com destino constante (`LUI`/`ORI`), o que leva a tradução AOT da BIOS real além do vetor de reset. Estado, ponto de bloqueio (MCH) e hipóteses: [M2.2-STATUS.md](M2.2-STATUS.md).

## M2.3 — MCH e cadeia Direct RDRAM

`MchRegisters` modela só a interface serial do controlador de memória (`MCH_RICM`/`MCH_DRD`). Por trás dela, `RdramChain` executa as transações de controle da RDRAM como o datasheet da Rambus as define. O tempo ocupado deriva do mesmo relógio do EE. Os registradores de comando do MCH sem documentação (`F410` e vizinhos) continuam recusados, por isso a BIOS real ainda para no mesmo ponto. Evidência, ordem dos acessos da BIOS e decisão pendente: [M2.3-STATUS.md](M2.3-STATUS.md).

## M2.4 — Conclusão das transações e registradores de temporização

`MchRegisters` guarda o estado da cadeia antes e depois da transação em andamento. A cadeia (`chain(now)`) e o dado do `SRD` só mudam quando o bit 31 zera, mas a validação acontece no `SW` que inicia a transação. `RdramChain` modela os registradores de temporização que a BIOS escreve antes da enumeração. Evidência sobre `F520`/`F410` e insumos pendentes: [M2.4-STATUS.md](M2.4-STATUS.md).

## M2.5 — Perfis de hardware e controle do MCH

`ConsoleHardware` (`console.hpp`) guarda o que a BIOS lê do próprio hardware e nenhum ROM0 contém, por modelo, placa e ROMVER: hoje `D_ENABLER`, com a evidência de cada valor. `prepare_ee_boot` recusa um perfil de outra revisão de BIOS; `EeRegisters` lê `0x1000F520` do perfil ou recusa com nome. `MchRegisters` acrescenta os latches de controle `F400`–`F4C0` e a operação do `F410`, além da inicialização dos pinos SIO (`SOP 1110`), que chega à `RdramChain` como `sio_pin_initialization()`. Hipóteses e evidência: [M2.5-STATUS.md](M2.5-STATUS.md).

`Iop` (`iop.hpp`) é o R3000A do caminho de boot, dono da própria RAM de 2 MiB, ROM0 e registradores de I/O (INTC, DMA, CDVD/MECHACON). `EeRegisters` o contém e o executa até o tempo do EE (`sync_iop`, razão 8:1) sempre que o EE lê a RAM do IOP em `0x1C000000`; o IOP nunca altera estado do EE, por isso a execução preguiçosa é determinística e igual para interpretador e AOT. Valores desconhecidos são rastreados por registrador e RAM; uma leitura do EE de byte não escrito, ou depois de o IOP parar, é recusada com a razão do IOP.

## M2.6 — NVM do MECHACON e núcleo do IOP

`NvmImage` (`nvm.hpp`) é a NVM do próprio console do usuário: `load_nvm_image` valida tamanho, SHA-256 opcional e a identidade contra `ConsoleHardware::mechacon` (layout, ModelID, ConModelID) e o nome do modelo. Ela chega ao IOP por `prepare_ee_boot` → `Memory::reset_for_boot` → `EeRegisters::reset` → `Iop::reset` (ponteiro não proprietário; sem NVM, as leituras dela param o IOP). `ConsoleHardware::dev9_revision` dá o `DEV9_R_REV` medido.

No `Iop`, um registrador desconhecido carrega a máscara dos bits conhecidos (`ANDI` que só usa bits conhecidos produz valor conhecido); os timers 0–5 avançam por ciclo do IOP e geram `I_STAT`; exceções (`SYSCALL`, `BREAK`, interrupção) seguem o R3000A, e a linha de interrupção que depende de fonte não modelada para o IOP. Tudo continua interno ao IOP, executado de forma preguiçosa até o tempo do EE. Detalhes: [M2.6-STATUS.md](M2.6-STATUS.md).

## M2.7 — VBLANK, ordem EE/IOP e SIF

`GsRegisters::vertical_clock()` deriva o campo do `SYNCV` sobre o relógio de linha, ou diz por que o tempo é desconhecido, e `timing_generation()` muda a cada alteração possível. `Iop::run_until(ciclo, &gs)` levanta `I_STAT` 0/11 nas bordas.

Regra de ordem: o IOP nunca roda à frente do EE, e `EeRegisters` chama `sync_iop()` antes de cada leitura de estado do IOP (RAM, SIF) e de cada escrita que o IOP observa (GS, SIF). Assim, o resultado depende só da sequência de acessos do EE, igual para interpretador e AOT. Um escalonador por eventos com IRQs do IOP para o EE (INTC do EE) fica para quando o firmware precisar deles.

Os registradores do SIF são estado do `Iop`, acessados pelo EE por `ee_sif_read`/`ee_sif_write`. O DMA do SIF ainda não é modelado. Detalhes: [M2.7-STATUS.md](M2.7-STATUS.md).

## Ferramentas de desenvolvimento e observabilidade

`ps2n-dev` orquestra as CLIs existentes em um build CMake exclusivo, sem
copiar conteúdo ou duplicar o runtime. Identidades de fontes/cabeçalhos
participam dos comandos de compilação gerenciados, incluindo Visual Studio;
análise/AOT são reutilizados por conteúdo, opções e identidade das ferramentas.
Perfis de firmware não compartilham seus artefatos originais.

`DiagnosticRecorder` é opcional em `BootOptions`. O `Tracer` comum observa
as mesmas tentativas de instrução/barramento nos dois motores; os históricos
IOP/GIF/vídeo podem ser limitados e observados sem mudar efeitos de hardware.
O anel registra só sua janela; streaming explícito declara truncamento e
o comparador exige sequências completas dentro da cobertura declarada.
Inspeção de estado é passiva e limitada, sem API de restauração. Capturar
RAM e registradores não satisfaz o contrato de checkpoint completo.

`ps2n-analyze` reutiliza `analyze_control_flow`; símbolos e mapas externos
não autorizam patches ou limites inferidos. IRX é identificado separadamente
como metadado IOP, sem decodificação R5900. Contratos e limites em
[DEV-TOOLS](DEV-TOOLS.md).
