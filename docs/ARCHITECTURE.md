# Arquitetura técnica e contratos — M0

## Diretriz

Uma base C++20 compartilhada para Windows x64/ARM64, Linux x64/ARM64 (inclui Steam Deck) e macOS Intel/Apple Silicon. Separar as fronteiras de hardware do convidado das interfaces de host; não duplicar arquivos por sistema. A recompilação AOT é o caminho primário **somente quando o código estático estiver comprovadamente coberto**. Sem sucesso falso em syscalls e dispositivos.

## Contratos implementados

| Componente | Entrada | Saída | Garantias / falhas |
|---|---|---|---|
| `read_file(path)` / `write_text_if_changed(path, text)` | caminho nativo Unicode do host | leitura binária / escrita idempotente | I/O centralizado, 128 MiB de limite de entrada; no Windows CLI `wmain` para paths UTF-16 e prefixo Win32 estendido para long paths, sem editar Registro |
| `parse_ee_elf(span<uint8_t>)` | bytes não confiáveis | `ElfImage` com `PT_LOAD`, entrada, bytes próprios | valida ELF32 LE/MIPS/ET_EXEC, limites, alinhamento, sobreposições, faixa EE física e `e_entry`; rejeita variantes não suportadas |
| `Memory::load(ElfImage)` | segmentos validados | 32 MiB RAM carregados, zero BSS | sem alterações ao ELF de entrada; só RAM física e seus espelhos KSEG0/1; MMIO deve falhar |
| `EEState` | estado por execução | 32×`Register128` + PC + pendência de branch | `$zero` imutável para writes do M0; sem global; sem alegar cobertura total R5900 |
| `generate_aot_cpp(ElfImage)` | instruções executáveis alcançáveis | TU C++20 textual | CFG limitado; rejeita opcode/delay-slot não suportado e execução fora de PT_LOAD; **sem** JIT/interpretador implícito |
| `run_recompiled(EEState&, Memory&, stop, budget)` | TU C++ já compilada e estado | número de instruções executadas | delay slots e saltos estáticos; rejeita PC dinâmico não coberto, limite esgotado, memória inválida |
| `ps2n-aot` | ELF de entrada e destino do `.cpp` | código traduzido | reusa `.cpp` idêntico; nunca copia o ELF/jogo inteiro |

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
