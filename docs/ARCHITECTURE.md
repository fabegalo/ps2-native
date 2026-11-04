# Arquitetura técnica e contratos — M0

## Diretriz

Uma base C++20 compartilhada para Windows x64/ARM64, Linux x64/ARM64 (inclui Steam Deck) e macOS Intel/Apple Silicon. Separar as fronteiras de hardware do convidado das interfaces de host; não duplicar arquivos por sistema. A recompilação AOT é o caminho primário **somente quando o código estático estiver comprovadamente coberto**. Sem sucesso falso em syscalls e dispositivos.

## Contratos implementados

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
- **EE ISA/dispatch:** cobertura R5900/MMI/FPU/COP0, descoberta de funções, ELF relocável, overlays, saltos indiretos, syscalls e cache de tradução. Separar AOT do fallback interpretado/JIT *declarado*.
- **EE memory/bus:** tradução de endereços, scratchpad, DMA, dispositivos, interrupções, atomicidade, proteção e invalidação de código modificado.
- **IOP:** R3000A, IRX, SIF/RPC, CDVD e sincronização.
- **VU0/VU1 + VIF/GIF/DMA:** macro/micro, tempos e semântica vetorial reais.
- **GS (fiel):** interfaces de comandos VRAM/texturas, provas de imagem e backends modernos; **Native** renderizará através de contratos compartilhados, nunca alterando física/temporização.
- **SPU2, controles e memory cards:** I/O persistente e seguro, áudio e sincronização.
- **BIOS host:** exigir dumps do usuário, sistema original e diferenças regionais; não distribuir firmware. (M2.0 já valida a ROM0 e inicia o EE; a M2.6 carrega a NVM do MECHACON do dump do usuário, validada contra o perfil; ROM1/ROM2/EROM e a maior parte dos dispositivos seguem pendentes.)
- **launcher:** gerenciador gráfico, importação sem duplicar, AOT automático, diagnóstico, salvamento e handoff para boot original.
- **platform:** filesystem Unicode, paths longos, SIMD, memória executável, GPU, áudio e input isolados por backend; API multiplataforma única.

## Posição sobre referências

- **PS2Recomp**: `ps2xRecomp/src/lib/{elf_parser,control_flow_analyzer,r5900_decoder,vu_translator}.cpp` e `ps2xRuntime/`: base experimental madura como referência de questões R5900, mas ainda com tradução incompleta, limites em controle de fluxo e componentes stub. GPL-3.0 => não integrar código neste projeto BSD.
- **PCSX2**: `pcsx2/Interpreter.cpp`, `pcsx2/Memory.cpp`, diretórios R5900, GS, SPU2, IOP: referência principal para especificação observável e testes futuros; não reaproveitar código GPL-3.0+.
- **Play!**: HLE do sistema; não satisfaz sozinho o Browser/boot original. Licença tipo BSD 2 cláusulas, mas conteúdo deve ser revisado caso a caso antes de integrar.
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

O interpretador é o fallback **explícito** previsto desde o M0: cada troca de motor aparece no trace e nada que não seja modelado é ignorado. Detalhes, testes e limitações: [M2.0-STATUS.md](M2.0-STATUS.md).

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
