# Guide for AI maintainers

This is the shared working policy for every agent contributing to ps2-native. `CLAUDE.md` points here rather than maintaining a second policy. Read the current request, `STATUS.md` and the relevant part of `docs/NEXT-TASK.md`; consult `docs/ARCHITECTURE.md` and subsystem reports where the change touches their contracts. Check the actual revision and implementation when a historical report is stale.

The maintainer selected [x360-native](https://github.com/fabegalo/x360-native) as the execution-model reference: original system UI and games compiled AOT, supported by native HLE system services. Its PowerPC/XEX, ReXGlue/Xenia, XAM, GPU, licensing and SDK layout are not PS2 contracts. Earlier gow-native-pc workflow references do not override this execution policy.

## Ponytail workflow

For coding tasks, use the [Ponytail skill](https://github.com/DietrichGebert/ponytail), `skills/ponytail/SKILL.md`, in `full` mode when available. Otherwise apply the same essentials: trace the affected implementation and callers, reuse existing helpers, and complete the smallest change that solves the requested problem. Keep investigation and output focused. Do not add dependencies or speculative frameworks just to follow a workflow, or claim to have run an unavailable skill.

Implement, run the relevant checks, correct failures, and validate the affected user path. Preserve error handling, accessibility and required correctness checks; saving tokens is not a reason to omit them. Report checks actually performed and remaining limitations, not unmeasured productivity claims. A new blocker changes the next diagnostic step; it is not by itself a reason to end the task with another plan when independent work within scope remains possible.

For runtime/codegen failures, compatibility blockers and performance investigations, use the local [ps2-native investigation skill](skills/ps2-native-investigation/SKILL.md). It adapts the useful layer diagnosis, targeted probing and hypothesis review from [PS2 Recomp Agent Skill](https://github.com/hkmodd/ps2-recomp-Agent-SKILL/tree/a1cafc23ac2d37d16ce979d3d4987a0bb6989dda) to this repository's tools and contracts. Read only the resources needed for the affected path; ordinary documentation or UI edits do not need the investigation workflow.

Use the current request and verified artifacts to select the active frontier. Historical stop points, missing generated files and older reports are context to verify, not reasons to repeat completed work or block an independent task. Keep existing user stop requests effective. After repeated failure of the same hypothesis, change the diagnostic method and continue useful authorized work; do not add a fixed retry count or another approval gate.

## What the system is

ps2-native is a **generic PlayStation 2 static AOT recompiler and native compatibility runtime**, not a port restricted to one game. Guest EE/R5900 and IOP/R3000A code and VU0/VU1 programs that execute must use static AOT translation compiled for the host. Native C++ HLE for BIOS/kernel calls, IOP system services and module imports is authorized, including temporary implementations with an explicit supported contract. HLE implements a service; it does not interpret guest instructions. Instruction interpreters are validation-only and must not be linked into production launchers, game executables or distributed runtime binaries. Missing translations require offline preparation and linkage, never an interpreter fallback.

The generic runtime belongs to this repository. Reuse its existing pipeline, shared state and device interfaces; adopting the x360-native execution model does not require replacing the core or importing another engine. One shared core serves Windows x64/ARM64, Linux x64/ARM64 and macOS Intel/Apple Silicon. Steam Deck follows the Linux path; a successful build is not a Steam Deck gameplay certification. Do not introduce JIT. Native HLE is already authorized and requires no further architectural approval; external code reuse still follows the license policy below.

The **original BIOS/Browser, System Configuration and Memory Card UI remain required targets**, just as the original dashboard remains a target in x360-native. Translate and execute the original UI code and load its matching assets; do not fabricate replacement screens. Native HLE may supply the underlying system services without executing every original kernel/firmware instruction. Identify HLE-assisted execution separately from a hardware-reset firmware run. Support both BIOS UI and games; completing the hardware-reset path is not a prerequisite for independent game progress.

| Area | Existing ownership |
| --- | --- |
| EE analysis and AOT | `src/analysis.cpp`, `src/control_flow.cpp`, `src/relocations.cpp`, `src/aot.cpp`, `src/aot_runtime.cpp` |
| VU0/VU1 AOT and semantics | `src/vu_aot.cpp`, `src/vu_micro.cpp`, `src/vu_float.cpp`, `include/ps2native/vu_*.hpp` |
| Shared execution and devices | `src/boot.cpp`, `src/memory*.cpp`, `src/ee*.cpp`, `src/iop*.cpp`, VIF/GIF and other device sources |
| GS and host GPU backends | `src/gs_renderer.cpp`, `src/gs_vram.cpp`, `src/gs_gpu.cpp`, `src/gs_vulkan.cpp`, `src/gs_directx.cpp`, `shaders/` |
| Preparation and build | `tools/ps2n-dev.cpp`, existing `ps2n-*` CLIs, `CMakeLists.txt`, `cmake/` |
| Desktop integration | `src/frontend/`, `include/ps2native/frontend/`, `launcher/` |
| Tests and evidence | `tests/`, `.github/workflows/`, subsystem reports under `docs/` |
| User content and generated output | ignored local `bios/`, `games/`, `out/`; never part of the source distribution |

Keep game/revision-specific inputs in validated profiles, manifests and local analysis artifacts, not hard-coded branches in the runtime. The launcher imports and prepares content and starts the selected execution path; it does not fabricate the original Browser, System Configuration or Memory Card screens.

## Performance target and acceptance

Target original-speed gameplay with working input and sound on each explicitly validated host tier. Preserve each title's original logic/video timing; do not force a 30 FPS title or PAL mode to 60 updates. A 60 Hz mode must advance at its intended rate, not merely redraw an old image 60 times. This is an acceptance target, not a claim that current builds meet it.

For performance work, use the executable, build configuration, manifest, scene and input path the maintainer actually runs. Verify that the expected EE/IOP/VU0/VU1 translations and effective HLE services are linked and executed, and that the selected GPU handles real primitives. A backend name, AOT percentage, CI pass or synthetic speedup is not evidence of a faster playable session.

Measure before and after on the same machine/settings. Distinguish guest progress, original video fields, game updates when a valid probe exists, and host presentation. Record pacing, stalls and audio starvation as well as throughput. Do not invent a universal guest-frame counter or use a game's address as a generic runtime rule. Keep expensive hashes and per-call timers out of primary timing windows; obtain a separate profile, preferably sampling the Release executable with matching symbols. Re-profile after a structural optimization rather than recycling the previous cost distribution.

Select the next optimization from the current bottleneck, not from the most interesting subsystem. Keep reference/slow paths for differential tests. Native production paths should not carry optional traces, dispatch history or intrusive profiling when diagnostics are disabled. Required validation may be specialized, cached or moved only with correct invalidation and equivalent observable behavior; preserving a contract does not require preserving every internal call or representation. Never disable necessary checks, skip guest effects or remove audio/graphics to manufacture a gain.

Validate visible gameplay and sound before declaring a playable build or hardware tier. Synthetic tests, isolated replay and a commercial window each prove only their own scope. If the user's session did not improve, do not describe an isolated benchmark as delivery of that performance goal. Keep all samples and explain inconclusive or negative results.

## Contracts to preserve

- **Content identity and regional isolation:** validate ROM0/ROM1/ROM2, NVM, modules, executable code and caches against their actual revision/profile and hashes. Identification is not compatibility. Do not mix files by basename or run one revision's translations against another. Hardware implementations remain shared; profiles do not justify needless copies of ISOs or firmware.
- **Original behavior:** retain the original BIOS UI and game behavior, guest semantics, exceptions, delay slots, interrupts, memory/cache/DMA ordering, clocks, graphics, physics, input and sound. HLE must honor the service ABI, guest memory effects, errors, synchronization and asynchronous completion. HLE-assisted BIOS/game sessions, hardware-reset runs and experimental checkpoints remain explicitly identified. A missing behavior produces a useful failure, never a fake successful syscall, invented register value or silently ignored draw. Distinguish established behavior, a selected hypothesis and unverified behavior.
- **AOT and code identity:** prepare dynamically loaded BIOS/kernel code, IOP/IRX modules, overlays and VU programs offline with validated image identity and actual relocation/load addresses. HLE may replace an explicitly supported service; any remaining guest instructions must execute AOT. A missing-AOT refusal is an intermediate diagnostic, not delivery of a runnable game. Preserve evidence-backed control flow and invalidation of changed executable bytes or microcode. Relocations in already-linked images are evidence, not permission to reapply them. Do not confuse compilation coverage with whole-game correctness. Changes to shared or inline semantics must reach every dependent native translation and reference path.
- **One logical state:** CPU/GPU storage, checkpoints and fast paths share the same guest contracts. Preserve unknown-bit masks, coherency and ordering; residency never makes unknown data known. Host caches/handles are not another console state and must not leak across sessions or restores.
- **Launcher-to-runtime wiring:** every exposed setting needs a real consumer. Verify executable ownership, content identity, effective AOT modules, selected backend and fallback reasons. Missing AOT or unsupported GPU must be visible, not silently presented as an accelerated session. Preserve pause, stop, focus-loss button release, controller reconnect and shutdown behavior.
- **User data and paths:** keep source discs/firmware read-only. Preserve saves, memory cards, settings and valid checkpoints. Destructive replacement/restoration requires the appropriate confirmation and recoverable backup. Use common Unicode/long-path I/O; do not replace deep-path support with a hard-coded short build folder, Registry change or administrator requirement.

## How to change things

Inspect `git status`, relevant callers, existing helpers and tests before editing. Preserve concurrent work; do not reset, clean, overwrite or force-push somebody else's changes. Keep each implementation change focused and close the requested user-facing path before starting another milestone.

Reuse the existing preparation, tracing, benchmark and launcher interfaces. Extend them only when the request needs it. Do not build another profiler, launcher, renderer abstraction or runtime just to avoid integrating the current ones. Consult pinned primary references for the affected behavior; do not import another port's game-specific shortcuts, DMA timing or HLE assumptions as PS2 hardware facts.

Before implementing a missing behavior, look for a suitable existing implementation or independent test in the relevant reference projects. Adapt the smallest useful component to the shared state and interfaces, with its origin recorded and the existing reuse requirements preserved. An emulator comparison is evidence about that implementation, not a physical-console measurement; Ghidra and debugger MCP integrations are optional tools to discover, not assumed prerequisites.

Respect outstanding stop requests and permission requirements for Computer Use, installations and external actions. Ordinary scoped checks already authorized by the task need no repeated request for the same permission. Supervise bounded runs and clean up only processes created by the task. If a required permission or local input is unavailable, state the limitation and complete the independent authorized work; do not fabricate a test or leave a run unattended.

## How to test

1. Build the affected targets in the existing compiler environment. Inspect effective Release flags and the build's source root; do not benchmark a stale, Debug or accidentally under-optimized executable. If the build fails, old binaries cannot validate the change.
2. Run focused regressions for the changed contract, then the relevant integration test through the production code path. Preserve independent expected-result tests as well as reference comparisons. Do not weaken an assertion merely to make the new implementation pass.
3. For runtime/codegen changes, compare guest state and relevant RGBA, PCM, input, cycles and events at matched observable boundaries. Test checkpoint continuation and cache invalidation when affected. Equivalent host metadata need not have identical storage. Equality with a reference implementation is not proof of undocumented physical-console behavior.
4. Reproduce the affected commercial scene when authorized local inputs exist. For interactive/performance tasks, verify the launcher-selected executable and report whether a human session, scripted UI, headless run or only a synthetic fixture was exercised. Test hardware-dependent changes on available AMD/NVIDIA/other devices and label untested devices honestly.
5. Check CI for the exact published code revision on the six targets. Separate compilation, CPU execution, software GPU, physical GPU and skipped tests. CI without a disc does not certify gameplay. Do not rerun the complete matrix for an instructions-only edit.

Baseline commands, adapted to the actual build directory and affected tests:

```sh
cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --parallel
ctest --test-dir out/build -N
ctest --test-dir out/build -R "<existing-affected-test-pattern>" --output-on-failure
```

For multi-configuration generators, select `Release` for both build and CTest. Verify current CLI help and CMake options instead of inventing commands or copying the reference port's scripts.

## Build foundation and diagnostics

Generated output and caches must be content-addressed and owned by the correct profile, source checkout, compiler/ABI, configuration and inputs. Preserve stable AOT partitions, unchanged timestamps and reusable objects. Publish generated results transactionally; a failed generation must not destroy the last valid output or unrelated files. Serialize writers to shared build/cache outputs and reserve RAM/threads for the host; do not benchmark while compiling.

Header/ABI, shader, generator or compiler changes invalidate all affected artifacts. Keep required runtime identity checks on checkpoint reuse. Do not impose another project's compiler, SSE baseline, PGO or LTO settings. No mandatory AVX/AVX2 or global fast-math; optional SIMD needs capability checks and a portable path. Evaluate LTO/PGO only with measured build time, memory, disk and gameplay impact; profile identity must prevent stale reuse.

Edit headers when the shared contract needs it and rebuild their actual dependents; do not hide state in globals or duplicate declarations to avoid compilation. Fix persistent AOT defects in the generator or shared semantics and regenerate the affected output. Use the established host toolchain, CMake configuration and affected targets; an ordinary incremental build needs no extra approval. Reuse valid artifacts, and rebuild invalid ones without blanket bans on headers, target selection or regeneration.

Reuse bounded local diagnostics with a distinct output path per experiment so failures and previous samples remain available. Keep exit codes and actionable errors, distinguish timeout from success, identify truncation, and collect only the task's processes by their owned handles/PIDs. Export only an explicit reviewed allowlist with path/private-data redaction; no full environment dumps or automatic upload. Firmware, discs, saves, game-derived traces, microcode captures and generated proprietary code are not public diagnostic attachments. Basic frame accounting and intrusive timing are separate modes.

## Documentation, language and publication

`README.md` and `README.pt-BR.md` are stable public introductions with equivalent content. This file and `CLAUDE.md` contain durable policy, not PCs, per-task counts or daily progress. Update them only when a real policy, architecture or public-use contract changes. Put evidence in the existing subsystem report and `STATUS.md`; put the next action in `docs/NEXT-TASK.md`. Do not churn all documents or copy whole reports into them.

Follow the reference's English convention for new code identifiers, comments and commit messages. New technical guidance may use English; preserve existing Portuguese reports and the English/Portuguese UI and README translations rather than translating unrelated files. User-facing replies stay in the user's language. Check translation placeholders and accessibility when changing the UI.

Preserve BSD-3-Clause and existing attribution. Reference projects do not authorize copying, adapting, translating or linking incompatible GPL implementation code into this repository; review any proposed reuse under `docs/LICENSE-DECISION.md` and obtain a maintainer decision when needed. Do not commit Sony firmware, games, NVM, commercial modules, dumps, captures or translated proprietary code, fetch leaked SDKs, or execute scripts embedded in user content.

Work directly on `main` unless the maintainer says otherwise. Use Conventional Commits without assistant co-author signatures. No PR, ZIP or Release unless requested; no history rewriting or changes to x360-native. Finish with the actual commits, implemented behavior, tests/CI, measured user-path result and remaining limits. A green build or instructions update is not a performance fix.
