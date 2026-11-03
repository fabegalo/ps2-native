# ps2-native — PlayStation 2 native recompilation
**Languages:** English | [Português (Brasil)](README.pt-BR.md)


**Status: experimental prototype, M2.7 in progress. NOT a PS2 emulator or playable game launcher. No commercial game, BIOS or Browser is supported: real BIOS dumps, run with a console hardware profile, execute 15.8–17.7 million EE instructions (with the IOP running its own boot path alongside) and stop where the IOP needs the console's MECHACON NVM configuration, which is per console: you supply your own console's NVM dump with `--nvm=` (none is bundled). Nothing is rendered.**

### Current progress — M2.7: VBLANK, deterministic EE/IOP ordering and the SIF registers

- **Real BIOS (no NVM supplied):** unchanged — 17,685,811 / 17,685,893 / 15,816,643 EE instructions, stopping at the IOP's `ReadConfig`; interpreter and hybrid traces identical.
- **GS vertical timing:** the field comes from `SYNCV` (the BIOS's NTSC value: 525 half-lines, 480 displayed, 45 blank) on the M2.2 line clock. It is held while `SMODE1.SINT` is set, and unknown (with a reason) for other `CMOD` values, an unwritten `SYNCV`, or after a GS reset. Field edges at the line-clock epoch are a named hypothesis.
- **IOP VBLANK (IRQ 0/11):** raised at the field edges. The IOP never runs ahead of the EE and is synced before every EE read of IOP state and every EE write it observes (GS, SIF), so ordering is deterministic and identical for interpreter and AOT. IRQ 3 (DMA) is a known source that never rises, since no DMA transfer can start.
- **SIF registers:** `MSCOM`/`SMCOM`/`MSFLG`/`SMFLG` are shared between the EE and the IOP, with the set and clear rules from ps2tek. On the IOP, `SIF_BD6` has bits 12-31 known 0 (SIFMAN's residency test; hypothesis). `AND` and `BEQ`/`BNE` work on values whose bits are only partly known.
- **EE:** `GIF_CTRL`/`GIF_MODE`, `DEV9_R_REV` from the profile, and HBLANK timers that keep counting across a GS line-clock change (GS reset, NTSC/PAL).
- **Diagnostic only, not a boot** (local synthetic NVM, never committed):
  - **IOP:** passes the VBLANK block (~436 thousand instructions in M2.6), SIFMAN's residency test and its DMA interrupt setup, and waits in SIFMAN for the EE's `MSFLG` (about 2.2 million instructions).
  - **EE:** reads the IOP's boot data from IOP RAM, resets the GIF and the GS, then stops at the first status read of an undocumented controller the kernel drives at `0x1F801900` (`0x1A000000` on SCPH-70012). No public source documents it, so no answer is invented.
- **No SIF DMA yet:** neither side reaches a SIF transfer, and the public documentation does not settle the FIFO word/quadword framing or depth; channel starts stay refused by name.
- **24/24 tests** pass locally (MSVC and clang). [M2.7 evidence, hypotheses and what is missing](docs/M2.7-STATUS.md).

### M2.6: external MECHACON NVM, EECONF and the IOP kernel start

- **External NVM (`--nvm=FILE`, `--expect-nvm-sha256=HEX`):** the 1024-byte image PS2Ident dumps (`sceCdReadNVM` words 0..511), validated for size, SHA-256, a known MECHACON layout (Dragon, v5+) and the model name, ModelID and ConModelID of the selected profile (PS2Ident database values); an NVM is never accepted for another model. The MECHACON S commands `ReadNVM`, `OpenConfig` (read), `ReadConfig` and `CloseConfig` serve it; anything else is refused by name.
- **Real BIOS (no NVM supplied):** unchanged counts — 17,685,811 / 17,685,893 / 15,816,643 EE instructions; the IOP now stops explicitly at `ReadConfig` with "no NVM image is supplied". Interpreter and hybrid traces are identical (apart from engine-switch events).
- **Diagnostic only, not a boot:** with a locally generated synthetic NVM (never committed, not console data) the IOP runs EECONF to completion and THREADMAN's timer setup, ~433–436 thousand instructions, and stops when interrupts are re-enabled with VBLANK start/end in `I_MASK`: the IOP VBLANK (GS CRTC vertical timing) is the next component.
- **IOP:** tool board id (shared with the EE), `DEV9_R_REV` per profile (measured: PS2Ident's "ssbus revision"), the expansion-bay write, i.LINK id register and PHY bus reset (hypotheses named), values with known and unknown bits (`ANDI`), the six hardware timers raising `I_STAT`, and R3000A exceptions (`SYSCALL`, `BREAK`, interrupts, `RFE`). An interrupt line that depends on a source that is not modeled stops the IOP by name.
- 23/23 tests at the time. [M2.6 evidence, hypotheses and what is missing](docs/M2.6-STATUS.md).

### M2.5: past the MCH/RDRAM block, IOP on the boot path

- **Real BIOS progress:** SCPH-70012, SCPH-77008 and the 2006-09 Japanese dump reach 17,685,811 / 17,685,893 / 15,816,643 EE instructions (was ~637,600 at `0x1000F410`). The BIOS completes its memory setup (including the real `F410` start/poll), prints `Total accessable memory size: 32 MB`, sets up the TLB, starts the kernel in RAM and prints `Initialize Start.` / `Initialize GS ...`. All three stop at `LW 0xBC0003C0`: the IOP's `EECONF` needs `ReadConfig` (MECHACON S command `0x41`), whose bytes live in the per-console NVM. Interpreter and hybrid runs are identical.
- **IOP (R3000A) on the boot path:** same reset vector, PRId from the profile, clock EE/8 (fact) at 1 instruction/cycle (hypothesis), run lazily up to EE time; load delay slots, `IsC`, unknown-value propagation with origin, POST codes, interrupt controller, DMA interrupt/channel registers (transfers refused), CDVD N status and the MECHACON S command interface (`OpenConfig` answered, NVM reads refused).
- **EE:** INTC `I_MASK` (toggle semantics; `I_STAT` reads refused), MMI `PAND`/`PXOR`/`POR`/`PNOR`, and two undocumented blocks the kernel writes (`0x1F801900`, `0x1A000000`) latched with reads refused.
- **Console hardware profiles (`--console=MODEL[:MAINBOARD]`):** `D_ENABLER` (`0x1000F520`) comes from the board, not from a global value. SCPH-77008 is measured (`0x4111`); SCPH-70012 GH-032 and SCPH-90000/90006 use values measured on consoles with the same chip revisions (marked as hypotheses); SCPH-70012 GH-035 stays unknown and refused. A profile is refused with a BIOS of another ROMVER.
- **MCH control registers** `F400`–`F4C0` as latches and the `F410` operation (bit 31 alone, busy for 16 SCK; hypothesis), the SIO pin initialization (`SOP 1110`, hypothesis) and the later Direct RDRAM opcode encoding (`CLRR` = `1011`).
- **RDRAM:** registers programmed before the first `SETR` and kept by it (hypothesis), more register layouts (`DEVID`, `CCA/CCB`, `NAPX`, `PDNXA/B`, `TPARM`, `SKIP`, test registers) and an `SRD` no device answers reading 0 (hypothesis).
- **21/21 tests** at M2.5. [M2.5 evidence, hypotheses and the NVM block](docs/M2.5-STATUS.md).

### M2.4: RDRAM timing registers and completion-time serial transactions

- **Completion-time commits:**
  - MCH serial transactions are validated when `MCH_RICM` is written, so a refusal names that store and changes nothing.
  - Their effect on the RDRAM chain and their `SRD` data become visible only when bit 31 clears.
- **RDRAM `TCYCLE`, `TFRM`, `TRDLY` and `TEST77`** follow the Rambus 64/72-Mbit layouts (reset values undefined). The BIOS writes exactly these addresses before enumerating, which confirms the map. `TPARM` stays refused.
- **Evidence for `0x1000F520` (`D_ENABLER`):**
  - All three BIOSes use it as the key of the same 16-entry memory-controller generation table (`0x1201`…`0x4212`). An unknown key aborts memory init.
  - The value on these console models is not documented, so it stays refused instead of copying an emulator constant.
- **The real BIOS block point does not move:** still 637,620/637,598 instructions, stopping at `SW` to `0x1000F410`. Interpreter and hybrid traces are identical.
- **19/19 tests** pass locally (MSVC, LLVM clang++). [M2.4 evidence and pending hardware inputs](docs/M2.4-STATUS.md).

### M2.3: MCH serial interface and the Direct RDRAM chain

- **`MCH_RICM`/`MCH_DRD`** (`0x1000F430/0x1000F440`) start Direct RDRAM serial control transactions:
  - The field layout is the one the BIOS serial helpers build. Bit 31 stays busy for the transaction length in SCK cycles at the data sheet's 1000 ns minimum period (hypothesis).
  - A `CLRR` sooner than 16 SCK cycles after `SETR` is refused.
- **`RdramChain`** follows the Rambus Direct RDRAM data sheet:
  - `SETR`/`CLRR`/`SETF` and `SWR`/`SRD` of `INIT`;
  - the SIO repeater daisy chain and `SDEVID` selection.
  
  Reserved opcodes, other registers, access before `SETR`, and reads nobody (or several devices) answer are refused without changing state.
- A synthetic ROM0 runs Algorithm InitDev for two devices, identically interpreter-only and hybrid.
- **The real BIOS block point does not move.** The three dumps still retire 637,620/637,598 instructions and stop at `SW` to `0x1000F410`. The BIOS writes this undocumented register before any serial transaction, then uses `0x1000F520`, `F480/F490/F4C0` and serial opcodes `0xE`/`0xB`, which the available data sheet lists as reserved. Each needs evidence or a maintainer decision.
- **18/18 tests** pass locally (MSVC, LLVM clang++). [M2.3 evidence, BIOS access order and pending decision](docs/M2.3-STATUS.md).

### M2.2: GS privileged registers and the BIOS memory setup

The same three user-supplied ROM0 dumps now retire **637,620** instructions (SCPH-70012/77008) and **637,598** (SCPH-90006), identically interpreter-only and hybrid. They:

- program the GS CRTC;
- calibrate the EE clock by counting HBLANK edges with T0 and `Count`;
- pulse DEV9;
- print `# Initialize memory (rev:3.70, ctm:393Mhz, cpuclk:295Mhz detected)` through the EE SIO (`rev:4.00` on SCPH-90006).

They stop at `0x9FC4135C` on `SW` to `0x1000F410`, a memory controller (MCH) register with no public documentation. It stays an explicit refusal until there is evidence.

- **GS privileged registers** (control state only, no rendering):
  - `SMODE1/SMODE2/SRFSH/SYNCH1/SYNCH2/SYNCV` take 64-bit writes; reserved bits are refused and reads are refused (write-only).
  - `CSR` writes: `RESET` and acknowledge bits. `CSR` reads are refused because their value depends on VBLANK and the GS revision.
- **GS line clock drives HBLANK:**
  - `SMODE1.SINT` holds sync; NTSC (9370 BUSCLK) or PAL (9436) lines start when it is released.
  - Retiming the GS while a timer counts HBLANK is refused.
- **DEV9 latches, EE SIO transmit, `LWC1`/`SWC1`:**
  - `LWC1`/`SWC1` move raw words; without `Status.CU1` they stop as coprocessor unusable.
  - The rest of the SIO and FPU is refused.
- **AOT proof of constant `LUI`/`ORI`/`JR` jumps:**
  - A jump is proven only when no entry point lies between the `LUI` and the `JR` and the `LUI` is not in a delay slot.
  - Static BIOS coverage grows from 12 to 107 instructions (2 proven jumps). Code the BIOS copies to RAM stays interpreted.
- **17/17 tests** pass locally (MSVC, LLVM clang++); CI uses synthetic firmware only. [M2.2 evidence, hypotheses and block point](docs/M2.2-STATUS.md).

### M2.1: first steps of a real BIOS

Three user-supplied ROM0 dumps (SCPH-70012 v2.00 USA, SCPH-77008 v2.20 Europe, SCPH-90006 v2.20 Japan; never stored in the repository) boot from the reset vector through COP0/TLB setup, the `0x1000F500` store, the scratchpad stack, `PADDUW` and into the ROM's `RDRAM` module, retiring **70,090 instructions** identically in interpreter-only and hybrid mode. They read the tool board id byte at `0x1F803204`, take the retail path, and stop at the first GS privileged register write (`SMODE1`, `0x12000010`). The GS is not modeled.

- **Tool board id `0x1F803204`:** reads `0xFF`, meaning no tool board. This value is a recorded hypothesis.
  - The firmware only distinguishes tool-board classes (high nibble 1–3 on the EE, `0x60/0x61` on the IOP).
  - `0xFF` (bus pulled high) is supported by PS2Ident data, but the exact retail byte has not been measured.
  - Other sizes, writes and the rest of the IOP register window are refused.

- **MMI pipeline 1 and `PADDUW`** in the shared AOT/interpreter semantics (`MULT1/MULTU1/DIV1/DIVU1/MFHI1/MFLO1/MTHI1/MTLO1` on LO1/HI1).
- **EE clock and COP0 `Count`:** one cycle per retired instruction (a recorded hypothesis); EE timers T0–T3 (`COUNT/MODE/COMP`, BUSCLK, /16, /256 and NTSC HBLANK) derive from the same clock. Gates, timer interrupts, mode flags and `HOLD` are refused with a reason.
- **TLB fixed against the real BIOS:** an S-bit entry maps the whole 16 KiB scratchpad whatever its PageMask; never-written entries do not match.
- **16/16 tests** at the end of M2.1. [M2.1 evidence and hypotheses](docs/M2.1-STATUS.md).

### M2.0: original PS2 boot infrastructure

- **BIOS loader for your own ROM0 dump:** exactly 4 MiB; ROMDIR, EXTINFO and ROMVER (version, region, console kind, build date) validated field by field; SHA-256 identity with an optional `--expect-sha256`. ROM1/ROM2/EROM are not loaded yet, so revisions are never mixed; a MECHACON NVM image is loaded only from your own dump and validated against the console profile. Nothing from Sony is bundled.
- **EE bus and MMU:** KSEG0/KSEG1 decode the physical bus (32 MiB RAM, read-only ROM0 at `0x1FC00000`); kuseg/KSEG2/KSEG3 go only through the guest-written 48-entry TLB (page masks, ASID, global, valid/dirty, the S bit for the 16 KiB scratchpad). Every EE device region (timers, DMAC, INTC, SIO, SIF, MCH, GIF, VIF, IPU, VU, GS, IOP) fails with its name; TLB faults are reported by exception type.
- **EE reset and COP0:** PC `0xBFC00000`, `Status = BEV | ERL`, `PRid 0x2E20`, `Config 0x440`; `MFC0/MTC0` with per-register rules, `TLBWI/TLBR/TLBP`, `ERET`, `EI/DI`, `CACHE` (invalidate/write-back only, since memory is coherent), `PREF`. Cycle timing, cache contents, interrupts and exception vectoring are not modeled and stop with a diagnostic.
- **Hybrid execution:** one shared R5900 semantics header (`ee_semantics.hpp`) used by both AOT blocks and the new interpreter. `ps2n-aot <rom0> <out.cpp> --bios` translates the statically reachable firmware code and lists every interpreter boundary; `ps2n-boot` runs AOT blocks where the PC matches exactly and the interpreter elsewhere, always visible in the trace. A translation of another ROM0 is refused.
- **Reproducible trace:** COP0/TLB/CACHE events, engine switches, optionally every instruction and data access, and the exact block point (PC, word, device, physical address).

A synthetic, self-authored ROM0 boots from the reset vector through TLB init, scratchpad mapping, cache invalidation, a ROM→RAM copy, `ERET` and a RAM payload, then stops at `INTC_STAT` (`0x1000F000`), which is not modeled yet: **582 instructions**, identical results interpreter-only and hybrid (370 AOT + 213 interpreted). **15/15 tests** pass locally on Windows x86-64 (MSVC 19.44, LLVM clang++ 22). [M2.0 evidence, real-firmware workflow and limitations](docs/M2.0-STATUS.md).

```sh
./out/build/ps2n-boot /path/to/your-rom0.bin --console=SCPH-77008 --trace=full --trace-out=boot-trace.txt
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

See [architecture](docs/ARCHITECTURE.md), [M2.7 status](docs/M2.7-STATUS.md), [M2.6 status](docs/M2.6-STATUS.md), [M2.5 status](docs/M2.5-STATUS.md), [M2.4 status](docs/M2.4-STATUS.md), [M2.3 status](docs/M2.3-STATUS.md), [M2.2 status](docs/M2.2-STATUS.md), [M2.1 status](docs/M2.1-STATUS.md), [M2.0 status](docs/M2.0-STATUS.md), [M1.3 status](docs/M1.3-STATUS.md), [M1.2 status](docs/M1.2-STATUS.md), [M1.1 status](docs/M1.1-STATUS.md), [M0 baseline](docs/M0-STATUS.md), [reference audit](docs/REFERENCE-AUDIT.md), and [license decision](docs/LICENSE-DECISION.md). Generated translations of copyrighted guest binaries must not be redistributed without permission. No Sony firmware, game data or third-party GPL implementation is bundled.

Next engineering increment: [M2.7 continued: the kernel's undocumented controller and a real NVM](docs/NEXT-TASK.md). Licensed BSD-3-Clause for independently authored code; no GPL source or Sony firmware is included.
