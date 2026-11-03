# M0 — Relatório de evidência (03/11/2026)

## Ambiente local verificado

- Linux x86-64, GNU C++ 14.2 (`Release`), Clang 17.0 (`Debug` com UBSan), CMake 3.31.6, Ninja 1.12.1; ambos builds e suites `CTest` executados localmente.
- Fluxo real: gerador de fixture (ELF32 sintético produzido pelo projeto) → `ps2n-aot` → fonte C++ gerada → compilador host → executável → assertions.
- Resultado local: **3 testes CTest aprovados em cada um dos dois compiladores (6 execuções), 0 falhas**. Smoke test: **37 instruções aposentadas**, `$v0 == 37`, `RAM[0x40] == 37`, retorno `JR` através de delay slot. Sem medir FPS ou desempenho de jogo.
- Segundo teste AOT: **9 instruções executadas**, `LUI`, `ORI`, `J` e seu delay slot, `$zero` imutável e `$v0 == 0x12345679`.
- Teste de núcleo: cabeçalho ELF, `PT_LOAD`, BSS, KSEG0/1, RAM little-endian, registrador zero, extensão de sinal, entrada fora de segmento, opcode não suportado, memória/alinhamento inválidos.
- Estado da CI Windows/Linux/macOS: **workflow criado para 6 targets, mas nenhum job remoto executado/validado nesta entrega**.

## Realmente implementado

- ELF32 v1, little-endian, `ET_EXEC`/`EM_MIPS`; tabela de `PT_LOAD` e bounds check, até 128 headers; imagens de até 128 MiB; segmentos carregados somente em 32 MiB de RAM EE.
- Endereços físicos `0..0x01ffffff` e espelhos RAM KSEG0 `0x80000000..0x81ffffff`, KSEG1 `0xa0000000..0xa1ffffff`.
- Registradores GPR128 armazenados, `$zero`, `PC`, `LO/HI` como slots sem operações; lower-word apenas.
- Opcode AOT: `NOP`, `ADDIU`, `ADDU`, `ORI`, `LUI`, `LW`, `SW`, `BEQ`, `BNE`, `J`, `JAL`, `JR`. Os dois cenários provam esse conjunto e os delay slots, dentro do modelo de palavra limitado de M0.
- Fluxo direto descoberto a partir de `e_entry`; `JAL` segue alvo e continuação, `JR` sem destino estático; destino dinâmico só executa se já recompilado.
- Fonte gerada compila para binário host nativo, sem interpretador no caminho testado. I/O centralizado em `std::filesystem::path`, CLI com `wmain` no Windows e reaproveitamento de `.cpp` quando o conteúdo gerado não muda. Paths Unicode no Windows **não foram testados em host Windows**.

## Simulações/stubs

**Nenhum syscall ou acesso ao hardware retorna sucesso falso.** Há abstrações de RAM e estado do EE incompletas; não constituem simulação funcional do restante do PS2. Um endereço de retorno artificial (`0xfffffff0`) é usado SOMENTE pelo teste sintético como condição de término do programa de teste, não como firmware, syscall ou comportamento do console.

## Não implementado / limites importantes

- ISA R5900 completa, GPR de 128 bits com semântica MMI, HI/LO operacional, FPU, COP0/2, exceptions, SYSCALL e privilégios.
- Semelhança MIPS32 parcial NÃO garante correção de detalhes R5900; precisão de upper-bits e modos aritméticos precisa de testes de referência.
- Relocações, ELF32 big-endian/ET_DYN, modelos complexos de linker, controle de fluxo indireto genérico, jump tables, code-in-data, funções automaticamente recuperadas para todos os jogos, overlays e código automodificável.
- Jumps/branches dentro de delay slot rejeitados; branch-likely e detalhes de exceções no delay slot ausentes.
- Memory map de console (scratchpad 0x70000000, MMIO, ROM/BIOS, MMU, EEscratch, CDVD, IOP) ausente; acesso falha explicitamente.
- IOP, R3000A, VU0/VU1 micro, VIF, GIF, DMA, GS, SPU2, inputs, memory cards, BIOS, boot, Browser, launcher, renderer GS/Native ausentes.
- Backends D3D12/Vulkan/Metal ainda não existem; six-platform workflows ainda não foram verificados.
- Caminhos longos do Windows em ambiente real, hardlink safety, cache AOT por hash, catálogos de jogos e deduplicação completa ainda requerem trabalho e teste. I/O com paths UTF-16 e prefixo estendido Win32 implementado na camada centralizada, mas **ainda não validado num host Windows**.

## M1 — incremento técnico curto

> Implementar um despachante de funções AOT com descoberta recursiva e saltos indiretos rastreados, suporte determinístico a ELF/relocações permitidas e fallback interpretado **explicitamente sinalizado** apenas para código não coberto. Ampliar testes de MMI, branch-likely, `SYSCALL` falhando corretamente e MMI básicos contra casos de referência. Preservar BSD-3-Clause sem incorporar código GPL. Validar nos seis runners de CI e documentar divergências, sem declarar jogos ou BIOS funcionais.
