# ps2-native — PlayStation 2 native recompilation
**Languages:** English | [Português (Brasil)](README.pt-BR.md)


**Status: M2.0 experimental prototype. NOT a PS2 emulator or playable game launcher. No commercial game, BIOS or Browser is supported; running a real Sony BIOS has not been demonstrated yet.**

### Current progress — M2.0: original PS2 boot infrastructure

- **BIOS loader for your own ROM0 dump:** exactly 4 MiB; ROMDIR, EXTINFO and ROMVER (version, region, console kind, build date) validated field by field; SHA-256 identity with an optional `--expect-sha256`. ROM1/ROM2/EROM/NVM are not loaded yet, so revisions are never mixed. Nothing from Sony is bundled.
- **EE bus and MMU:** KSEG0/KSEG1 decode the physical bus (32 MiB RAM, read-only ROM0 at `0x1FC00000`); kuseg/KSEG2/KSEG3 go only through the guest-written 48-entry TLB (page masks, ASID, global, valid/dirty, the S bit for the 16 KiB scratchpad). Every EE device region (timers, DMAC, INTC, SIO, SIF, MCH, GIF, VIF, IPU, VU, GS, IOP) fails with its name; TLB faults are reported by exception type.
- **EE reset and COP0:** PC `0xBFC00000`, `Status = BEV | ERL`, `PRid 0x2E20`, `Config 0x440`; `MFC0/MTC0` with per-register rules, `TLBWI/TLBR/TLBP`, `ERET`, `EI/DI`, `CACHE` (invalidate/write-back only, since memory is coherent), `PREF`. Cycle timing, cache contents, interrupts and exception vectoring are not modeled and stop with a diagnostic.
- **Hybrid execution:** one shared R5900 semantics header (`ee_semantics.hpp`) used by both AOT blocks and the new interpreter. `ps2n-aot <rom0> <out.cpp> --bios` translates the statically reachable firmware code and lists every interpreter boundary; `ps2n-boot` runs AOT blocks where the PC matches exactly and the interpreter elsewhere, always visible in the trace. A translation of another ROM0 is refused.
- **Reproducible trace:** COP0/TLB/CACHE events, engine switches, optionally every instruction and data access, and the exact block point (PC, word, device, physical address).

A synthetic, self-authored ROM0 boots from the reset vector through TLB init, scratchpad mapping, cache invalidation, a ROM→RAM copy, `ERET` and a RAM payload, then stops at `INTC_STAT` (`0x1000F000`), which is not modeled yet: **582 instructions**, identical results interpreter-only and hybrid (370 AOT + 213 interpreted). **15/15 tests** pass locally on Windows x86-64 (MSVC 19.44, LLVM clang++ 22). [M2.0 evidence, real-firmware workflow and limitations](docs/M2.0-STATUS.md).

```sh
./out/build/ps2n-boot /path/to/your-rom0.bin --trace=full --trace-out=boot-trace.txt
```

### M1.3

- **MIPS relocations as evidence, never applied.** For already-linked ELF32 `ET_EXEC` files that keep `SHT_REL` records, `R_MIPS_26` (validated `J`/`JAL`), and `R_MIPS_HI16`/`R_MIPS_LO16` pairs (ABI pairing, several HI16 per LO16, `LUI` + sign-extended `%lo` carriers) are checked against the linked bytes and symbols. A HI16/LO16 pair that forms a code address (`ADDIU`) proves an address-taken function. Malformed records (orphan HI16, wrong instruction, bad symbol, site outside the code section) fail explicitly; unused types such as `R_MIPS_GPREL16` are counted and reported, not dropped silently.
- **Jump tables proven from the code itself**, with no per-game configuration: a `JR` is accepted only when a straight-line path shows an `SLTIU` bound, a `BEQ …, $zero` guard, a word-scaled index, the table base and the `LW` of the entry, with no outside branch into that window. GCC and ee-gcc shapes are recognized; every table entry must be executable code in the same segment. Unprovable `JR`s are reported with a reason.
- **Verified indirect entries at runtime:** `JR`/`JALR` may only reach function roots, call continuations or proven table targets; anything else fails closed.
- **R5900 integer subset** with 64-bit semantics that preserve the upper GPR half: 32/64-bit shifts, `DADDU/DSUBU/DADDIU`, logic ops, 64-bit `SLT*`, `MOVZ/MOVN`, three-operand `MULT/MULTU`, `DIV/DIVU` (defined division-by-zero results), `MFHI/MFLO/MTHI/MTLO`, byte/half/word/double/quad loads and stores (`LQ/SQ`), `BLEZ/BGTZ/BLTZ/BGEZ`, `BxxZAL`, all branch-likely forms with delay-slot nullification, and `SYNC`. `ORI` and `BEQ/BNE` now use the R5900 64-bit behavior.

A synthetic M1.3 ELF (with and without sections) is generated, recompiled to C++, compiled natively and executed: **263 guest instructions**, 2 proven jump tables, 61 checked 64-bit ALU/memory values, link registers, branch-likely nullification and fail-closed indirect targets. **12/12 tests** pass locally on Windows x86-64 (MSVC 19.44 and LLVM clang++ 22); see the [GitHub Actions results](https://github.com/fabegalo/ps2-native/actions) for the six targets. [M1.3 evidence and limitations](docs/M1.3-STATUS.md).

### M1.1

The AOT tool discovers validated ELF32 `STT_FUNC` symbols or explicit `--entry=0xADDRESS` hints, builds a conservative control-flow graph, and emits **native C++ basic-block functions with an address-based dispatcher**. A synthetic MIPS ELF executes three indirect `JALR/JR` calls with real delay slots, including the `JALR rs==rd` case. Unknown targets and modified executable instructions fail explicitly; the project has no silent interpreter fallback.

The synthetic M1.1 test executes **28 guest instructions** with expected register/RAM results. **6/6 tests passed** locally under Linux x86-64 with GCC 14.2 and Clang 17 + ASan/UBSan. [Technical evidence and limitations](docs/M1.1-STATUS.md).

For stripped ELF files with verified function addresses:

```sh
./out/build/ps2n-aot input.elf out/build/generated/output.cpp --entry=0x00100050
```

`--entry` marks a known executable root; it does not make arbitrary PS2 games automatically recompilable.

### M0 baseline

An independent project that shares the *vision* of [x360-native](https://github.com/fabegalo/x360-native): preserve the original console experience while using ahead-of-time native compilation where technically feasible. The Xbox project remains untouched.

The M0 prototype validates and loads little-endian MIPS ELF32 `ET_EXEC`/`PT_LOAD` segments, models 32 MiB of EE RAM and register storage, discovers a restricted set of statically reachable instructions, emits C++20, **builds the generated translation into a host executable** and runs a synthetic test program. The E2E test executes 37 guest instructions and checks `$v0 = 37`, `RAM[0x40] = 37`, branch delay slots, call/return and proper termination.

Implemented M0 opcodes: `NOP`, `ADDIU`, `ADDU`, `ORI`, `LUI`, `LW`, `SW`, `BEQ`, `BNE`, `J`, `JAL`, `JR` (limited lower-word semantics). EE 128-bit register *storage* does **not** imply 128-bit/MMI instruction support. Unsupported opcodes, device registers, dynamic code and undiscovered jump targets fail explicitly.


## M1.2 — evidence-backed indirect jump targets

Jump-table discovery is deliberately conservative: the ELF32 loader recognizes **already-resolved R_MIPS_32 pointers** in non-executable allocated data sections, backed by defined executable `STT_SECTION` / `STT_FUNC` symbols. Valid targets become AOT function roots, including targets inside an executable section that have no function symbol. Rejected candidates remain reported, never silently interpreted as code.

For stripped files that lack relocation evidence, a developer can declare a known table explicitly: `ps2n-aot input.elf output.cpp --table=0x00101000:2`. Every word must be readable from file-backed non-executable data and point to executable code; invalid hints fail. Unrecognized indirect addresses still fail at runtime. This is **not** automatic recognition of all jump tables, relocations, dynamic overlays or commercial games. See [M1.2 status](docs/M1.2-STATUS.md).

## Build and test

Requires CMake >= 3.21, Ninja and a C++20 compiler; no external libraries.

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

The CI matrix targets Windows (x86-64/ARM64), Linux (x86-64/ARM64) and macOS (Intel/Apple Silicon). Local results do not replace CI; check the [GitHub Actions results](https://github.com/fabegalo/ps2-native/actions) for remote platform status. Windows jobs use Visual Studio 2026 with CMake.

`ps2n-aot input.elf output.cpp` produces a translation unit only; a host must link it with `ps2n_core` and initialize memory/state. The current integration is implemented for synthetic fixtures, not for real commercial games or the PS2 BIOS.

See [architecture](docs/ARCHITECTURE.md), [M2.0 status](docs/M2.0-STATUS.md), [M1.3 status](docs/M1.3-STATUS.md), [M1.2 status](docs/M1.2-STATUS.md), [M1.1 status](docs/M1.1-STATUS.md), [M0 baseline](docs/M0-STATUS.md), [reference audit](docs/REFERENCE-AUDIT.md), and [license decision](docs/LICENSE-DECISION.md). Generated translations of copyrighted guest binaries must not be redistributed without permission. No Sony firmware, game data or third-party GPL implementation is bundled.

Next engineering increment: [M2.1](docs/NEXT-TASK.md). Licensed BSD-3-Clause for independently authored code; no GPL source or Sony firmware is included.
