---
name: ps2-native-investigation
description: Diagnose ps2-native runtime or AOT failures, compatibility blockers, state divergences and measured performance problems using existing CLIs, shared semantics and reproducible evidence. Use for those investigations, not routine documentation or UI edits.
---

# ps2-native investigation

Apply [AGENTS.md](../../AGENTS.md) and Ponytail full. Choose the parts of this
workflow that resolve the current uncertainty; no fixed startup ritual is needed.

## Establish the actual starting point

Read the current request, relevant entries in [STATUS.md](../../STATUS.md) and
[NEXT-TASK.md](../../docs/NEXT-TASK.md), then inspect the affected implementation
and callers. Preserve concurrent edits and staged changes. Verify that referenced
profiles, checkpoints, binaries and logs still exist before using them. When an
artifact is absent, reconstruct a valid scenario through the existing tools or
continue independent work; historical results are not a current baseline.

Identify the input revision/hash, execution path, effective AOT modules, backend,
source root, compiler and build configuration needed to reproduce the symptom.
Keep original boot, Direct ELF and experimental checkpoint scenarios distinct.
The ownership map in AGENTS.md identifies the shared subsystem to change.

## Locate the failing layer

| Evidence | Inspect first |
|---|---|
| Missing AOT target or unexpected fallback | `analysis.cpp`, `control_flow.cpp`, `aot.cpp`, `aot_runtime.cpp`; verified roots, overlays and code identity |
| Wrong register, numeric result or exception | Shared EE/VU semantics and the relevant independent expected-result tests |
| Unknown data, stale bytes or different continuation | `memory*.cpp`, cache/writeback, TLB, DMA and checkpoint invalidation |
| No guest progress or a device refusal | `boot.cpp`, EE/IOP synchronization and the named device; distinguish waiting from a proven deadlock |
| Missing/wrong image or sound | VIF/GIF/GS or SPU2 producers and transport, then host presentation/output |
| Headless works but the launcher differs | Actual executable, module linkage, manifest, settings consumers and session lifecycle |
| Restore rejected after an edit | `cmake/Checkpoint.cmake`, serialized fields and runtime identity; distinguish schema changes from execution helpers |

State the observed failure, the suspected producer and the next observation that
would distinguish plausible causes. Keep facts, hypotheses and unverified behavior
separate. A familiar PC or error message does not prove the same cause as before.

## Obtain a small, decisive probe

Use [DEV-TOOLS.md](../../docs/DEV-TOOLS.md) and current CLI help. Prefer an existing
log/report before rerunning a long prefix. `ps2n-debug log`, `report` and `compare`
cover different evidence; select the one supported by the available artifact.
Use bounded diagnostics around the first relevant failure, including PC, word,
delay slot, operands and unknown masks, device state and guest timestamps as needed.
Read selected log ranges instead of dumping complete traces or generated trees.

Reuse a valid full checkpoint close to the boundary, with its input identities,
scenario and runtime checks. Use `ps2n-dev` profiles for repeatable preparation,
analysis, offline AOT and replay. Its `observed=` input can supply validated
execution roots; verify bytes and image ownership rather than treating every
executable-looking word as code. Replay from reset and checkpoint continuation
are different checks.

If the same hypothesis fails repeatedly, inspect the producer, narrow the probe,
consult an independent result or choose another diagnostic method. Preserve failed
attempts and keep moving within the authorized scope. Request input only for a
missing fact, resource or permission that actually prevents the dependent action.

## Reuse and implement at the owning layer

Consult the relevant candidates in [REFERENCE-AUDIT.md](../../docs/REFERENCE-AUDIT.md)
before implementing a missing subsystem behavior. Pin and inspect the applicable
source/test, its dependencies and assumptions. Reuse a small compatible component
when it saves work; verify that it preserves the shared state and interfaces.
Ports' hard-coded addresses, reset values, ABI assumptions and HLE behavior are
not generic hardware facts.

Check the proposed fix against another explanation and the affected edge cases.
Fix translation defects in the generator/shared semantics; fix hardware behavior
in its shared subsystem; keep revision-specific evidence in profiles/manifests.
Change headers and rebuild dependent translations when needed. Keep per-session
state with its owner. Return-value stubs, skipped DMA/draws and guessed unknown
bits do not establish compatibility.

Discover Ghidra/debugger MCP tools before relying on them. Existing CSV hints,
diagnostics and source inspection remain usable without those integrations.
Compare reference-emulator state at matched observable boundaries with matching
inputs and settings. Treat the result as a differential reference; physical
hardware claims require applicable hardware evidence.

## Validate the result that matters

Build affected targets in the existing environment and check the actual exit
status. Run focused regressions and the relevant production-path integration,
including an independent expected result where available. Compare guest state,
unknown masks, exceptions/events and the affected RGBA/PCM/input/cycle boundaries.
Exercise restoration and code/microcode invalidation when the change touches them.

For performance, establish an uninstrumented Release baseline and a separate
sampling profile with matching symbols. Select one candidate from the current
bottleneck, retain its reference path and measure the same scene/input/settings
before and after. Re-profile structural changes. Apply the acceptance criteria in
AGENTS.md; AOT coverage, GPU selection and successful compilation alone do not
establish faster gameplay.

Use existing bounded runners and distinct output paths. Preserve child exit codes
and timeout outcomes, stop only owned processes, and leave no task run unattended.
Record demonstrated results and remaining limits in the existing subsystem report
and STATUS.md, with the next action in NEXT-TASK.md. Those are the persistent
handoff; another project-state file, wrapper runner or periodic rereading ritual
is unnecessary.

## Reference

Original ps2-native guidance informed by the layer diagnosis, targeted probing
and hypothesis review in [PS2 Recomp Agent Skill, revision a1cafc2](https://github.com/hkmodd/ps2-recomp-Agent-SKILL/tree/a1cafc23ac2d37d16ce979d3d4987a0bb6989dda).
The upstream scripts and reference database are not bundled with this adaptation.
