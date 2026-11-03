<div align="center">

# ps2-native

**An open research project for running original PlayStation 2 software through native ahead-of-time recompilation.**

[![CI](https://img.shields.io/github/actions/workflow/status/fabegalo/ps2-native/build.yml?branch=main&label=CI)](https://github.com/fabegalo/ps2-native/actions/workflows/build.yml)
[![Experimental](https://img.shields.io/badge/status-experimental-orange)](STATUS.md)
[![BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue)](LICENSE)

[English](README.md) · [Português (Brasil)](README.pt-BR.md) · [Current status](STATUS.md) · [Documentation](docs/README.md) · [Contributing](CONTRIBUTING.md)

</div>

> **Experimental research software, not a playable PS2 launcher.** The project can analyze and recompile a verified subset of PS2 code and execute part of original firmware using a hybrid AOT/interpreter runtime. It does **not** complete the original Browser boot or run commercial games. Passing CI does not prove gameplay or platform-level runtime compatibility.

## What is ps2-native?

The goal is to execute the **original PlayStation 2 system and games** on modern computers, with static ahead-of-time (AOT) translation of Emotion Engine (R5900/MIPS) instructions into native C++ as the primary path. The compatibility runtime must still reproduce the console's CPU behavior, memory, firmware services, IOP, graphics, audio, devices and timing.

Firmware code and dynamically loaded or copied code that cannot safely be translated statically may use an **explicit, traceable interpreter path**. Compiling code is not the same as booting a console or making a game playable.

The **original PlayStation 2 Browser** and system configuration screens are targets in their own right. A future desktop launcher will manage users' local content; it will not replace Sony's menus with imitations.

## Current status

| Subsystem | Demonstrated | Still missing |
|---|---|---|
| **Native AOT** | Validated ELF32/MIPS analysis, conservative control-flow discovery, generated C++ and synthetic end-to-end execution | Broad commercial-game translation and dynamic-code coverage |
| **Emotion Engine** | Partial R5900 semantics, memory/TLB, hardware boot and hybrid AOT/interpreter | Full CPU behavior, precise timing and exceptions |
| **IOP and firmware** | Partial R3000A boot, hardware registers, external NVM support, SIF flags and VBLANK | Complete BIOS/Browser boot, full interrupts, SIF DMA and peripheral services |
| **Graphics** | GS/GIF control registers, timing and a limited GIF PATH3 packet path | Framebuffer rendering, full GS/VU/VIF and game graphics |
| **Games and user experience** | Developer tools and synthetic fixtures | Playable games, launcher, installers and public releases |

**The maintained, evidence-based record is [STATUS.md](STATUS.md).** It distinguishes real firmware runs, diagnostic assumptions, CI results and missing functionality. Milestone-specific findings remain in [technical reports](docs/README.md); they are not copied into this README after every change.

## How it works

1. Load and validate user-supplied firmware or a legally obtained MIPS executable. Keep each BIOS revision, region and its matching resources isolated.
2. Analyze provably reachable code and emit C++20 AOT blocks for instructions with supported semantics.
3. Execute native blocks where available. The firmware boot runner uses the interpreter explicitly for code outside proven AOT coverage, with traceable engine switches and named block points.
4. Improve hardware fidelity using technical evidence, reproducible synthetic tests and documented real-firmware observations. Unsupported operations must not silently succeed.

**Performance-first, correctness-preserving:** optimize the recompilation, CPU, memory and graphics paths without sacrificing console behavior. Any future enhanced rendering mode must preserve game logic, physics and synchronization.

## Build and test

Requirements: **CMake 3.21+**, a **C++20 compiler** and **Ninja** (or a supported native build generator). No mandatory external SDK or AVX instruction-set requirement.

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build --output-on-failure
```

On Windows, the Visual Studio CMake generator is also supported; pass `--config Release` when building and testing a multi-configuration build.

GitHub Actions builds and tests **Windows x86-64/ARM64, Linux x86-64/ARM64 (including the Steam Deck architecture) and macOS Intel/Apple Silicon**. Builds on all six targets are not evidence that real PS2 games work on those hosts.

### Developer tools

```sh
# Generate native C++ from a supported, legally obtained MIPS ELF.
./out/build/ps2n-aot input.elf out/build/generated/program.cpp

# Inspect a user-supplied ROM0 boot. Choose the matching console profile.
./out/build/ps2n-boot bios/your-rom0.bin --console=MODEL:MAINBOARD --trace=events

# If available, provide the NVM dumped from the matching console.
./out/build/ps2n-boot bios/your-rom0.bin --console=MODEL:MAINBOARD --nvm=bios/your-console.nvm --trace=events
```

`ps2n-aot` produces C++ that must be linked into a compatible host; it does not produce a standalone, playable game. `ps2n-boot` is a diagnostic runner and may stop on unsupported hardware. The current boot runner does not represent a successful BIOS startup merely because its process exits.

The user's original ROM0, ROM1/ROM2, NVM, games and generated proprietary translations are **not provided**. Keep them local and do not commit or redistribute them. A BIOS/NVM match must be verified, not inferred from filenames.

## Roadmap

The long-term direction is **generic AOT → accurate EE/IOP, firmware and DMA → faithful GS/VU/SPU2 → original Browser → games → a simple cross-platform launcher and carefully measured optimizations**. This is a technical direction, not a release schedule or a compatibility promise. Follow [the next engineering task](docs/NEXT-TASK.md) for active work.

## Contributing and documentation

Contributions should include focused tests and evidence, keep a shared C++20 core across platforms and avoid per-game hacks, silent fallbacks and unnecessary file copies. See [CONTRIBUTING.md](CONTRIBUTING.md), [architecture](docs/ARCHITECTURE.md), [technical documentation](docs/README.md) and [project status](STATUS.md).

## License

Original ps2-native code is licensed under **[BSD-3-Clause](LICENSE)**. PCSX2 and PS2Recomp are behavioral references, **not copied or linked GPL implementations**. Firmware, games, device-specific NVM and copyrighted translated code are not part of this repository. See [licensing decisions](docs/LICENSE-DECISION.md).
