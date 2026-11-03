# ps2-native — M0 technical foundation

**Status: experimental AOT proof of concept, NOT a PS2 emulator or playable game launcher.**

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

MSVC with a Visual Studio generator is supported as a *CI target* but not locally verified for M0. Linux x86-64 / GCC was the local build/test environment. CI contains six native runner targets for Windows (x86-64/ARM64), Linux (x86-64/ARM64), macOS (Intel/Apple Silicon); passing CI on those targets is not yet established.

`ps2n-aot input.elf output.cpp` produces a translation unit only; a host must link it with `ps2n_core` and initialize memory/state. M0's integration is implemented for the synthetic fixture, not for real commercial games or the PS2 BIOS.

See [architecture](docs/ARCHITECTURE.md), [verified status](docs/M0-STATUS.md), [reference audit](docs/REFERENCE-AUDIT.md), and [license decision](docs/LICENSE-DECISION.md). Generated translations of copyrighted guest binaries must not be redistributed without permission. No Sony firmware, game data or third-party GPL implementation is bundled.
