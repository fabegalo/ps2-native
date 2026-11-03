# CLAUDE.md — contexto persistente do ps2-native

Leia também `README.md` / `README.pt-BR.md`, `docs/ARCHITECTURE.md` (contratos
implementados e interfaces propostas), o status do marco mais recente
(`docs/M2.8-STATUS.md`, em andamento, com `docs/M2.7-STATUS.md`, `docs/M2.6-STATUS.md`, `docs/M2.5-STATUS.md`, `docs/M2.4-STATUS.md`, `docs/M2.3-STATUS.md`, `docs/M2.2-STATUS.md`, `docs/M2.1-STATUS.md`, `docs/M2.0-STATUS.md`, `docs/M1.3-STATUS.md`, `docs/M1.2-STATUS.md`,
`docs/M1.1-STATUS.md` e `docs/M0-STATUS.md` como histórico) e `docs/NEXT-TASK.md`. Licenças e referências estão em
`docs/LICENSE-DECISION.md` e `docs/REFERENCE-AUDIT.md`. Atualizar status e
READMEs (inglês e português) somente com o que foi demonstrado por teste.

## Objetivo

Executar jogos e o sistema original do PlayStation 2 nativamente em PC por
**recompilação estática AOT**, com compatibilidade e desempenho iguais ou
superiores ao PCSX2 como metas de longo prazo, nunca como afirmação. Projeto
independente que compartilha a *visão* do x360-native; **não modificar o
x360-native** nem herdar decisões específicas do Xbox 360 que não sirvam ao
hardware da Sony.

AOT é o caminho principal para código estático identificado com segurança.
Overlays, módulos dinâmicos e partes do firmware poderão usar fallback
interpretado/dinâmico **explícito e verificável**. Desde a M2.0 existe um
interpretador do EE usado só no caminho de boot (`run_ee_boot`), sempre
registrado no trace; os runtimes AOT puros (`run_recompiled`) continuam
falhando com erro claro para o que não foi recompilado.

Uma única base C++20/CMake para seis alvos: Windows x86-64/ARM64, Linux
x86-64/ARM64 (Steam Deck incluído) e macOS Intel/Apple Silicon. Isolar apenas o
que depende da plataforma (SIMD, GPU, áudio, memória executável, toolchain,
ABI); não criar cópias do projeto, de ISOs, SDKs ou builds por sistema.

**Performance-first, correctness-preserving** é política permanente: toda
otimização exige medição antes/depois em condições equivalentes, sem reduzir
fidelidade, ocultar erros, remover sincronização necessária ou alterar
semântica R5900, delay slots, memória e temporização. O modo Native nunca altera
física, lógica, temporização ou renderização para ganhar FPS; o modo GS fiel é a
referência de correção.

Marcos: M0 fundação → M1 Generic AOT → M2 sistema PS2 (EE, IOP, DMA, VU,
firmware) → M3 GS/SPU2 e primeiros frames → M4 Browser original → M5 jogos →
M6 multiplataforma e otimizações. Reavaliar conforme dependências técnicas
reais; não forçar Browser ou jogos com stubs.

## Estado atual (2026-11-03)

- M0, M1.1, M1.2, M1.3, M2.0, M2.1, M2.2, M2.3, M2.4, M2.5, M2.6 e M2.7 concluídos; M2.8 em andamento. 25 testes CTest (ver `docs/M2.8-STATUS.md`).
- M2.8: primeiro boot com BIOS + NVM reais (SCPH-70004, ROMVER `0200EC20040614`, perfis `SCPH-70004:GH-032-11`/`:GH-032-32`; arquivos locais em `bios/`, ignorada pelo Git): EE 17.690.079 e IOP 2.210.789 instruções, interpretador = híbrido; para na leitura do registrador `0x7E` (ID do chip) do codificador de vídeo. O "controlador não documentado" é o barramento serial (estilo I²C, escravo `0x42`/`0x43`) do codificador de vídeo dos slim: `VideoEncoderBus` + `VideoEncoderMaster` (16 bits em `0x1A000000`, 8 bits em `0x1F801900`; papéis por hipótese). Leituras do escravo só com `--assume-video-encoder=` (diagnóstico marcado). GIF PATH3 PACKED A+D (`gif.hpp`) escreve os registradores gerais do GS; `SQ` chega inteiro à FIFO (`Memory::write128`). Diagnóstico com ID assumido: o EE para no `I_STAT` (estado do GS após o reset desconhecido). VBLANK após o reset do GS e DMA do SIF continuam sem evidência.
- M2.7: tempo vertical do GS (`GsRegisters::vertical_clock`, campo do `SYNCV`, bordas no *epoch* do relógio de linha por hipótese) gera VBLANK início/fim no `I_STAT` do IOP (IRQ 0/11). O IOP é sincronizado antes de leituras do EE e de escritas do GS/SIF (ordem determinística). IRQ 3 (DMA) é fonte conhecida e nunca sobe. Registradores do SIF `MSCOM`/`SMCOM`/`MSFLG`/`SMFLG` são compartilhados. `SIF_BD6` < `0x1000` (hipótese). `AND`/`BEQ`/`BNE` com bits parciais. EE: `GIF_CTRL`/`GIF_MODE`, `DEV9_R_REV`, timers em HBLANK através do reset do GS. BIOS reais sem NVM: contagens da M2.6. Diagnóstico com NVM sintética: IOP ~2,2 milhões de instruções no laço do `SIFMAN`; o EE para no controlador não documentado `0x1F801900`/`0x1A000000` depois do reset do GS. Sem DMA do SIF.
- M2.6: NVM externa do MECHACON (`nvm.hpp`, `--nvm=`/`--expect-nvm-sha256=`; 1024 bytes no formato do PS2Ident, layout Dragon, validada contra nome de modelo/ModelID/ConModelID do perfil) servindo `ReadNVM`/`OpenConfig`/`ReadConfig`/`CloseConfig`. IOP: *tool board id* compartilhado, `DEV9_R_REV` por perfil (medido), `0xB600000A`, i.LINK `0x847C`/reset do PHY (hipóteses), bits conhecidos em valores desconhecidos (`ANDI`), timers 0–5 com `I_STAT`, exceções R3000A. BIOS reais sem NVM: contagens da M2.5, parada explícita no `ReadConfig`. Diagnóstico com NVM sintética local (não é boot): IOP ~436 mil instruções até o VBLANK do IOP.
- M2.5 (IOP): `Iop` (R3000A, `iop.hpp`) roda o caminho de boot do mesmo ROM0 (PRId do perfil, EE/8, execução preguiçosa até o tempo do EE, valores desconhecidos rastreados), com INTC, DMA (sem transferências), status N do CDVD e comandos S do MECHACON. EE: `I_MASK`, `PAND`/`PXOR`/`POR`/`PNOR`, blocos não documentados `0x1F801900`/`0x1A000000`. BIOS reais: 17.685.811 (SCPH-70012), 17.685.893 (SCPH-77008), 15.816.643 (dump J) instruções; kernel em RAM, "Initialize GS"; bloqueio no `LW` de `0xBC0003C0` porque o `EECONF` pede `ReadConfig` (`S 0x41`) da NVM de cada console.
- M2.5: bloqueio do `F410` superado. Perfis `ConsoleHardware` (`--console=MODELO[:PLACA]`) fornecem `D_ENABLER` por placa e ROMVER (SCPH-77008 `0x4111` medido; demais por mesma revisão de chip, hipótese). Latches de controle `F400`–`F4C0` e operação do `F410` (hipótese), `SOP 0xE` = inicialização dos pinos SIO (hipótese), `CLRR` = `0xB`, registradores da RDRAM programados antes do `SETR` e mantidos por ele (hipótese), `SRD` sem resposta lê 0 (hipótese). BIOS reais: 1.845.848 (SCPH-70012), 1.845.866 (SCPH-77008) e 1.111.255 (dump J de 2006-09) instruções, bloqueio no `LW` de `0xBC0003F0` (RAM do IOP, `QueryBootMode(2)`, PC `0x9FC43954`/`0x9FC43A2C`).
- M2.4: transações seriais do MCH validadas no início e aplicadas na conclusão (`chain(now)`); `TCYCLE`/`TFRM`/`TRDLY`/`TEST77` da RDRAM (layout 64/72 Mbit). `0x1000F520` (`D_ENABLER`) é a chave de uma tabela de 16 gerações do MCH nas três BIOS.
- M2.3: `MchRegisters` (`MCH_RICM`/`MCH_DRD`, bit 31 ocupado pela duração em SCK × 1000 ns, hipótese) sobre `RdramChain` (datasheet Direct RDRAM 64/72 Mbit da Rambus: `SETR`/`CLRR`/`SETF`, `SWR`/`SRD` de `INIT`, repetidor SIO, `SDEVID`); InitDev sintético para dois dispositivos. A BIOS real continua parando no `SW` em `0x1000F410` (`0x9FC4135C`, 637.620/637.598 instruções): `F410`, `0x1000F520`, `F480/F490/F4C0` e os `SOP` `0xE`/`0xB` (reservados no datasheet de 64 Mbit) vêm antes de qualquer `SETR` e aguardam evidência ou decisão do mantenedor.
- M2.2: as três BIOS reais param no `SW` em `0x1000F410` (MCH). Adicionados `GsRegisters` (`SMODE1/2`, `SRFSH`, `SYNCH1/2`, `SYNCV`, escritas de `CSR`; nada é desenhado) com relógio de linha NTSC/PAL que gera o HBLANK dos timers, latches DEV9 `0x1F801470/2`, transmissão do EE SIO (`ps2n-boot` mostra a saída), `LWC1`/`SWC1` com `Status.CU1` e prova AOT de `JR` constante (`LUI`+`ORI`/`ADDIU`), que leva a tradução da BIOS de 12 para 107 instruções estáticas.
- M2.1: `0x1F803204` ("tool board id") lê `0xFF` por hipótese (sem placa de ferramenta; evidência em `docs/M2.1-STATUS.md`). MMI do pipeline 1 + `PADDUW`, relógio do EE (1 ciclo/instrução, hipótese) com `COP0 Count` e timers T0–T3 (`EeRegisters`), latch `0x1000F500`, entrada S da TLB cobrindo 16 KiB e entradas não escritas que não correspondem.
- M2.0: loader de ROM0 (ROMDIR/EXTINFO/ROMVER/SHA-256), barramento EE com TLB
  e scratchpad, COP0 de reset, interpretador + AOT híbrido (`ps2n-aot --bios`,
  `ps2n-boot`) com semântica única em `ee_semantics.hpp`, trace reproduzível e
  ponto de bloqueio explícito.
- M1.3: relocations `R_MIPS_26` e `R_MIPS_HI16/LO16` lidas como evidência
  (nunca aplicadas), jump tables provadas por `SLTIU` + `BEQ $zero`, entradas
  indiretas verificadas em runtime e subconjunto inteiro R5900 de 64 bits.
- Próximo passo: continuação da **M2.8** (`docs/NEXT-TASK.md`) — medição do ID
  do codificador de vídeo e estado do GS depois do reset.
- Fixtures são somente sintéticos. Nenhum jogo comercial, Browser, IOP,
  VU, GS, SPU2 ou DMA é suportado. Nenhum jogo é declarado compatível.

## Fidelidade ao console — regra permanente

O comportamento deve ser o mais fiel possível ao PS2 original, idealmente
idêntico: animação de boot, Browser, System Configuration, Memory Card Browser,
seleção e inicialização de discos, códigos de erro e serviços da BIOS da
revisão correspondente. O launcher desktop gerencia conteúdo, configuração e
execução; **não substitui** menus originais por interfaces falsas. Divergência
de projeto é corrigida em direção ao original, só permanece com justificativa
registrada, e o que não foi comprovado no original fica marcado como hipótese.
Sem sistemas de migração enquanto não houver release: dados locais de teste
são descartáveis. Saves, Memory Cards e configurações reais do usuário são
sempre preservados.

## Isolamento de BIOS e firmware — regra arquitetural permanente

**Uma BIOS, seus componentes correspondentes. Sem mistura de versões ou
regiões.** Cada revisão/região de BIOS (e seus módulos IRX, ROM1/ROM2, fontes,
OSD/Browser e recursos) usa apenas os próprios arquivos. Nomes de módulo,
exports ou tamanhos iguais não comprovam compatibilidade. Registrar versão,
região, procedência e SHA-256 por perfil; recusar dependências ausentes ou
divergentes antes da execução, com mensagem identificando versão e arquivo
necessários. Proibido fallback silencioso, busca por basename em outra versão
ou cache global de componentes originais. Infraestrutura genérica (EE, IOP,
VU, GS, I/O) pode ser compartilhada; firmware específico de uma revisão, não.
O mesmo vale para jogos: perfis por título só para diferenças comprovadas,
indexados por serial/região/versão e hash.

## Licenças — regra permanente

- Código original do projeto é **BSD-3-Clause** (`LICENSE`).
- PCSX2 (GPL-3.0+) e PS2Recomp (GPL-3.0) são referências de comportamento e
  arquitetura: **não copiar, derivar, ligar ou traduzir código deles**. Play!
  e N64Recomp têm licenças permissivas, mas qualquer integração exige revisão
  caso a caso. Antes de integrar componente incompatível, apresentar a decisão
  de licenciamento ao mantenedor e registrá-la em `docs/LICENSE-DECISION.md`.
- Dependências novas exigem versão e hash fixados (actions por SHA de commit).
- Nunca adicionar BIOS, firmware, jogos, ISOs, ELFs/IRX comerciais, assets,
  saves ou C++ traduzido de binários proprietários. Conteúdo do usuário fica
  fora do repositório (ver `.gitignore`); não baixar conteúdo de terceiros nem
  executar scripts vindos de dados de jogos.

## Commits e publicação — regra permanente

- Trabalhar diretamente na `main`, salvo instrução contrária do mantenedor.
  Sem PR, sem ZIP e sem Release até existir produto executável e validado.
- Conventional Commits: `tipo(escopo): descrição objetiva`, com tipo pelo
  conteúdo real (`feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`,
  `ci`, `chore`). Exemplos: `feat(aot): pair R_MIPS_HI16/LO16 relocations`,
  `test(elf): reject orphan HI16 relocation`.
- Não incluir referências a Claude, Codex ou outros assistentes como autores no
  título, corpo ou rodapés: sem “Generated with...”, sem `Co-Authored-By`
  atribuído a assistente. Preservar créditos humanos, licenças e avisos de
  terceiros.
- Nunca reescrever histórico publicado nem fazer force-push sem autorização
  expressa. Push só quando o trabalho estiver pronto e a tarefa o pedir;
  depois do push, acompanhar a CI dos seis alvos e corrigir falhas antes de
  declarar a tarefa concluída.

## Trabalho dos agentes — regras permanentes

- Computer Use e sessões visuais/longas exigem autorização explícita prévia.
- Priorizar testes focados (`ctest -R <nome>`); rodar a suíte completa quando a
  mudança puder afetá-la (hoje ela é curta). Não repetir validações aprovadas
  por uma mudança puramente documental.
- Nenhum processo em segundo plano sem acompanhamento e limite de duração.
- Se uma ferramenta exigir interação não autorizada, interromper e informar o
  bloqueio imediatamente; não esperar horas por aprovação.
- Não ampliar o escopo nem começar outro marco antes de concluir o atual.
- Ao finalizar uma tarefa, informar apenas: commits, funcionalidades
  implementadas, testes/CI e limitações restantes.

## Comandos (raiz do repositório)

Requer CMake ≥ 3.21, Ninja e compilador C++20; sem bibliotecas externas.

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
ctest --test-dir out/build -R native-jump --output-on-failure   # teste focado
```

Windows (igual à CI; Visual Studio 2026):

```powershell
cmake -S . -B out/build -G "Visual Studio 18 2026" -A x64
cmake --build out/build --config Release --parallel
ctest --test-dir out/build -C Release --output-on-failure
```

```sh
./out/build/ps2n-aot input.elf out.cpp [--entry=0xADDR]... [--table=0xADDR:COUNT]...
./out/build/ps2n-aot rom0.bin out.cpp --bios [--expect-sha256=HEX]
./out/build/ps2n-boot rom0.bin [--console=MODEL[:MAINBOARD]] [--nvm=FILE [--expect-nvm-sha256=HEX]] [--trace=events|full] [--trace-out=FILE] [--max-instructions=N] [--stop-at=0xADDR]
```

BIOS, traces e traduções de firmware ficam fora do repositório
(`build-local/`, `*.rom0`, `boot-trace*.txt` estão no `.gitignore`).

- Se o build falhar, **não rode os testes** sobre binários antigos.
- CI: `.github/workflows/build.yml`, matriz de seis alvos, `fail-fast: false`.
  Resultado local não substitui a CI; confirmar os jobs do commit
  correspondente (`gh run list`, `gh run view`) antes de declarar aprovação.
- Resultado de referência (M2.8): 25/25 testes — `core-validation`,
  `native-aot-smoke`, `native-aot-ops`, `native-indirect-{sym,hints,unresolved}`,
  `native-jump-{relocations,explicit,unresolved}`,
  `native-m13-{relocations,stripped,stripped-hint}`, `native-m20-boot`,
  `cli-m20-boot-{interpreter,hybrid}`, `native-m21-boot`, `native-m22-boot`, `native-m23-boot`, `native-m24-boot`, `native-m25-boot`, `native-m25-iop`, `native-m26-nvm`, `native-m26-iop`, `native-m27-iop`, `native-m28-video-gif`. Qualquer falha é regressão.

## Estrutura

- `include/ps2native/` — contratos públicos: `elf`, `memory`, `ee`, `mips`,
  `ee_semantics` (semântica inteira R5900 única para AOT e interpretador),
  `ee_registers` (timers, SIO e registradores do EE em `0x10000000`),
  `gs_registers` (registradores privilegiados do GS e relógio de linha),
  `mch` (interface serial e controle do MCH), `iop` (IOP R3000A do caminho de boot), `nvm` (imagem da NVM do MECHACON),
  `video_encoder` (barramento serial do codificador de vídeo e seus mestres), `gif` (PATH3 do GIF), `console` (perfis de hardware por
  placa/ROMVER, `D_ENABLER`), `rdram` (cadeia Direct RDRAM),
  `device_timing` (`DeviceRefusal`, relógio do EE e comprimentos de linha),
  `control_flow`, `aot`, `aot_runtime`, `interpreter`, `bios`, `boot`,
  `sha256`, `io`, `core`.
- `src/` — biblioteca `ps2n_core`: loader ELF32 (`elf.cpp`), análise de
  relocations sem aplicá-las (`relocations.cpp`), RAM EE, barramento físico,
  ROM0, scratchpad e TLB (`memory.cpp`), estado EE e COP0 de reset
  (`ee.cpp`), decodificador MIPS/R5900 (`mips.cpp`), CFG e descoberta de
  funções (`control_flow.cpp`), emissor C++ (`aot.cpp`), despacho AOT
  (`aot_runtime.cpp`), interpretador com COP0/TLB/ERET/CACHE
  (`interpreter.cpp`), loader de ROM0 (`bios.cpp`), boot híbrido e trace
  (`boot.cpp`), registradores do EE (`ee_registers.cpp`), GS (`gs_registers.cpp`), MCH (`mch.cpp`), cadeia RDRAM (`rdram.cpp`), perfis de console (`console.cpp`), IOP (`iop.cpp`), NVM (`nvm.cpp`), SHA-256 (`sha256.cpp`), I/O centralizado Unicode/long paths
  (`io.cpp`). Todo novo `.cpp` entra em `ps2n_core` no `CMakeLists.txt`.
- `tools/ps2n-aot.cpp` — CLI AOT (ELF ou ROM0 com `--bios`); reusa o `.cpp`
  gerado se idêntico (`write_text_if_changed`), nunca copia a entrada.
- `tools/ps2n-boot.cpp` — boot do EE a partir do vetor de reset com trace;
  `ps2n-boot-hybrid` só é gerado com `-DPS2N_BIOS_AOT_SOURCE=`.
- `tests/` — `make_fixture.cpp` gera os ELFs e a ROM0 sintéticos no build (modo
  por argumento); `m13_program.hpp` é o mini-assembler com rótulos + construtor
  ELF compartilhado entre gerador e teste; `bios_program.hpp` monta a ROM0
  sintética (ROMDIR/EXTINFO/ROMVER + programa de reset) usada por
  `boot_m20.cpp` e, nas variantes `build_firmware(Stage::M21..M25)`, por
  `boot_m21.cpp`..`boot_m25.cpp`; `iop_m25.cpp` testa o IOP e a visão do EE sobre ele; `nvm_m26.cpp` a NVM e os comandos S; `iop_m26.cpp` timers, exceções e dispositivos do IOP; `iop_m27.cpp` VBLANK, ordem EE/IOP, SIF e dispositivos do EE após o reset do GS; `m28.cpp` codificador de vídeo, GIF, `SQ` e perfis SCPH-70004; `boot_checks.hpp` reúne os auxiliares de teste
  (endereços definidos uma vez); cada
  teste E2E compila a TU gerada com `ps2n_core` e valida registradores, RAM,
  contagem de instruções e falhas esperadas.
- `docs/` — arquitetura, status por marco, próxima tarefa, auditoria de
  referências e decisão de licença.
- `out/`, `build/`, `generated/` — ignorados; artefatos locais.

## Regras

1. Não inventar opcodes, semântica, tipos de relocation, flags, endereços,
   hashes ou resultados. Confirmar em especificação MIPS/R5900, no comportamento
   observável do PCSX2 (sem copiar código) ou em teste próprio.
2. Marcar fato, hipótese e não verificado. "Compilou" ≠ "executa" ≠ "inicia" ≠
   "jogável". Compatibilidade de jogo só com teste real de gameplay.
3. Não esconder função ausente retornando sucesso/zero. Sem stubs falsos, hacks
   por jogo ou fallback silencioso: opcode, relocation, delay slot, destino
   indireto, MMIO ou syscall não suportado gera erro explícito.
4. Descoberta de código só por evidência verificável (símbolos, relocations,
   tabelas validadas, fluxo direto). Sem varredura especulativa de `.text` ou
   dados. Hints de CLI (`--entry`, `--table`) são validados e nunca configuração
   por jogo embutida no código.
5. Não reaplicar relocations em executáveis já vinculados (`ET_EXEC`); usá-las
   apenas como evidência. Nenhum byte do ELF de entrada é alterado.
6. Preservar semântica R5900: delay slots, `$zero`, registradores de 128 bits,
   comportamento de 32/64 bits e exceções. Código modificado em RAM é detectado
   e rejeitado até existir fallback explícito.
7. Não remover verificações nem afrouxar testes. Toda funcionalidade nova traz
   fixture sintético e teste CTest, inclusive casos de erro.
8. Para corrigir: reprodução focada → hipótese → mudança pequena → teste
   relevante. Uma única implementação comum; sem duplicação de lógica.
9. Não afirmar desempenho sem medir e descrever a medição.
10. Sem instalação global, alteração do Registro do Windows ou admin sem
    explicar e pedir.
