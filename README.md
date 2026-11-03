# ps2-native — PlayStation 2 native recompilation
**Languages:** English | [Português (Brasil)](README.pt-BR.md)


**Status: M1.1 experimental AOT proof of concept. NOT a PS2 emulator or playable game launcher.**

### Current progress — M1.1

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

## Build and test

Requires CMake >= 3.21, Ninja and a C++20 compiler; no external libraries.

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

The CI matrix targets Windows (x86-64/ARM64), Linux (x86-64/ARM64) and macOS (Intel/Apple Silicon). Local validation is Linux only; check the [GitHub Actions results](https://github.com/fabegalo/ps2-native/actions) for remote platform status. Windows jobs use Visual Studio 2026 with CMake.

`ps2n-aot input.elf output.cpp` produces a translation unit only; a host must link it with `ps2n_core` and initialize memory/state. The current integration is implemented for synthetic fixtures, not for real commercial games or the PS2 BIOS.

See [architecture](docs/ARCHITECTURE.md), [M1.1 status](docs/M1.1-STATUS.md), [M0 baseline](docs/M0-STATUS.md), [reference audit](docs/REFERENCE-AUDIT.md), and [license decision](docs/LICENSE-DECISION.md). Generated translations of copyrighted guest binaries must not be redistributed without permission. No Sony firmware, game data or third-party GPL implementation is bundled.

Next engineering increment: [M1.2](docs/NEXT-TASK.md). Licensed BSD-3-Clause for independently authored code; no GPL source or Sony firmware is included.
