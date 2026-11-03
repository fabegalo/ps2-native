# Arquitetura técnica e contratos — M0

## Diretriz

Uma base C++20 compartilhada para Windows x64/ARM64, Linux x64/ARM64 (inclui Steam Deck) e macOS Intel/Apple Silicon. Separar as fronteiras de hardware do convidado das interfaces de host; não duplicar arquivos por sistema. A recompilação AOT é o caminho primário **somente quando o código estático estiver comprovadamente coberto**. Sem sucesso falso em syscalls e dispositivos.

## Contratos implementados

| Componente | Entrada | Saída | Garantias / falhas |
|---|---|---|---|
| `read_file(path)` / `write_text_if_changed(path, text)` | caminho nativo Unicode do host | leitura binária / escrita idempotente | I/O centralizado, 128 MiB de limite de entrada; no Windows CLI `wmain` para paths UTF-16 e prefixo Win32 estendido para long paths, sem editar Registro |
| `parse_ee_elf(span<uint8_t>)` | bytes não confiáveis | `ElfImage` com `PT_LOAD`, entrada, símbolos ELF `STT_FUNC` verificados, evidências de relocation (`R_MIPS_32`, `R_MIPS_26`, pares `HI16/LO16`), contagem de tipos não usados, bytes próprios | valida ELF32 LE/MIPS/ET_EXEC, tabelas de símbolos, limites, alinhamento, sobreposições, faixa EE física e `e_entry`; relocations são lidas como evidência e **nunca aplicadas**; pareamento `HI16/LO16` do ABI; erros explícitos para metadados inconsistentes |
| `Memory::load(ElfImage)` | segmentos validados | 32 MiB RAM carregados, zero BSS; acessos de 8/16/32/64 bits | sem alterações ao ELF de entrada; só RAM física e seus espelhos KSEG0/1; desalinhamento e MMIO falham explicitamente |
| `EEState` | estado por execução | 32×`Register128`, LO/HI (pipeline 0), PC, pendência de branch e de transferência indireta | `$zero` imutável; operações de 64 bits preservam a metade alta, `LQ/SQ` movem 128 bits; sem global; sem alegar cobertura total R5900 |
| `analyze_control_flow(ElfImage, entry_roots, tables)` | entrada ELF, funções simbólicas, evidências de relocation, raízes e tabelas opcionais | mapa de instruções, blocos, funções, entradas indiretas verificadas, jump tables provadas e `JR` não resolvidos com motivo | fluxo direto, alvos `JAL` como funções, tabelas provadas por `SLTIU` + `BEQ $zero` em janela sem entradas externas; ponto fixo e reverificação final; nada é aceito por semelhança |
| `generate_aot_cpp(ElfImage, AotOptions)` | CFG e raízes verificadas | TU C++20 com funções por bloco, tabela de despacho e tabela de entradas indiretas; cabeçalho com proveniência | rejeita opcode/delay slot não suportado; **sem** JIT/interpretador implícito |
| `run_recompiled(EEState&, Memory&, stop, budget)` | TU C++ já compilada e estado | número de instruções executadas | delay slots (incluindo anulação de branch-likely), `JR/JALR` só para entradas verificadas, valida instrução não modificada em RAM; rejeita PC dinâmico não coberto, limite esgotado e memória inválida |
| `ps2n-aot` | ELF de entrada e destino do `.cpp`, `--entry`/`--table` opcionais | código traduzido e resumo de evidências | reusa `.cpp` idêntico; nunca copia o ELF/jogo inteiro |

## Organização futura (interfaces propostas, NÃO implementadas)

- **disc/importer:** detectar disco, serial, região, hash e preservar caminho original; biblioteca indexada sem copiar ISOs.
- **EE ISA/dispatch:** cobertura R5900/MMI/FPU/COP0, descoberta de funções, ELF relocável, overlays, saltos indiretos, syscalls e cache de tradução. Separar AOT do fallback interpretado/JIT *declarado*.
- **EE memory/bus:** tradução de endereços, scratchpad, DMA, dispositivos, interrupções, atomicidade, proteção e invalidação de código modificado.
- **IOP:** R3000A, IRX, SIF/RPC, CDVD e sincronização.
- **VU0/VU1 + VIF/GIF/DMA:** macro/micro, tempos e semântica vetorial reais.
- **GS (fiel):** interfaces de comandos VRAM/texturas, provas de imagem e backends modernos; **Native** renderizará através de contratos compartilhados, nunca alterando física/temporização.
- **SPU2, controles e memory cards:** I/O persistente e seguro, áudio e sincronização.
- **BIOS host:** exigir dumps do usuário, sistema original e diferenças regionais; não distribuir firmware.
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
