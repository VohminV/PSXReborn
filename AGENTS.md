# AGENTS.md — PSXReborn Development Instructions

## 1. Project Mission

PSXReborn is an experimental PlayStation 1 to native PC relinking and runtime project targeting Linux and Windows.

The long-term goal is to execute PlayStation 1 games through native MIPS R3000A code translation, a custom compatibility runtime, and a Vulkan-powered graphics backend.

The project also aims to provide modern keyboard, mouse, and gamepad controls, including configurable mouse-driven camera control for supported games.

This is an implementation project, not a documentation-only exercise. The primary objective is to build working software, validate it, identify compatibility problems, and fix their root causes.

Do not claim that a feature works until its implementation and tests provide evidence.

## 2. Local Reference Projects

The following directories contain reference implementations available on the development machine:

- `/home/vita/ps1-recomp-main` — reference for PS1 static recompilation, executable analysis, code generation, and related tooling.
- `/home/vita/pcsx-redux-main` — reference for PlayStation hardware behavior, CPU emulation, memory, GPU, GTE, SPU, BIOS interfaces, debugging, and compatibility.

These projects are references, not the PSXReborn source tree.

### Mandatory reference workflow

Before selecting the architecture or implementing major subsystems:

1. Verify that both reference directories exist and can be read.
2. Inspect their README files, directory structures, build systems, licenses, architecture, and relevant source code.
3. Identify the existing implementations that can answer concrete engineering questions.
4. Trace important behavior into source code instead of relying exclusively on README descriptions.
5. Record findings, source paths, relevant symbols, limitations, and licensing considerations.
6. Compare the available approaches and select the smallest practical implementation path for PSXReborn.

Use `ps1-recomp-main` primarily to investigate recompilation techniques and `pcsx-redux-main` primarily to investigate hardware semantics and reference behavior. These are priorities, not exclusive boundaries.

Do not assume that either project implements every feature needed by PSXReborn. Verify each relevant subsystem independently.

Do not modify, delete, rename, or build over files in the reference projects unless explicitly requested. Keep all PSXReborn implementation work inside the PSXReborn repository.

If a reference directory is missing or incomplete, report the exact limitation and continue with the available sources. Do not repeatedly retry the same failed inspection without new information.

## 3. PS1 Documentation

The `docs/` directory in the PSXReborn repository contains Markdown documentation about the original PlayStation 1.

This documentation is a primary project reference and must be examined before implementing the corresponding subsystem.

### Documentation rules

1. Enumerate all Markdown files under `docs/`, including relevant nested directories.
2. Read the existing PS1 technical documentation before writing a new architecture or hardware implementation.
3. Extract relevant facts about MIPS R3000A, memory mapping, executable formats, GPU commands, VRAM, GTE, DMA, interrupts, timers, SPU, CD-ROM, and controller input where covered.
4. Preserve existing documentation. Do not overwrite or replace it with generated summaries.
5. Cross-check important or ambiguous claims against the reference source code and additional authoritative technical references.
6. When a claim is uncertain, document the uncertainty instead of inventing behavior.
7. Avoid creating duplicate documentation files that merely repeat information already present in `docs/`.

When implementing a feature, identify the applicable documentation file and relevant source references first.

If documentation is insufficient, investigate the actual behavior and update the appropriate documentation with verified findings.

## 4. Engineering Principles

- Implement working code rather than repeatedly producing plans and audits.
- Prefer small, complete, testable implementation increments.
- Identify root causes before applying fixes.
- Never invent APIs, structures, instruction semantics, Vulkan features, or library behavior.
- Inspect the actual headers, source code, and build configuration before using an API.
- Avoid unnecessary rewrites of functioning subsystems.
- Preserve working behavior and add regression tests for fixes.
- Keep guest hardware semantics separate from host-specific implementations.
- Do not introduce unrelated optimizations, dependencies, or broad architectural changes without justification.
- Never silently replace a failing implementation with a stub that reports success.
- Do not hide unsupported behavior behind success return values.
- Record incomplete functionality and known limitations explicitly.

When multiple approaches are possible, compare correctness, implementation cost, performance, maintainability, and compatibility before choosing one.

## 5. Required Development Workflow

### Phase A — Inspect

Before modifying code:

1. Identify the actual PSXReborn repository root.
2. Inspect its existing files, source tree, build configuration, tests, and Git status.
3. Read this `AGENTS.md` and relevant documentation under `docs/`.
4. Inspect relevant implementations in the two reference repositories.
5. Identify what is already implemented, what is incomplete, and what is missing.
6. Determine the smallest useful end-to-end feature that can be implemented and tested.

Do not overwrite existing user work. Review uncommitted changes before editing affected files.

### Phase B — Decide

Write a short, concrete implementation plan for the next feature only.

The plan must identify:

- The behavior to implement.
- Existing files and interfaces involved.
- Required new files, if any.
- Relevant reference implementations.
- Tests that will demonstrate correctness.
- Risks and unresolved technical questions.

Do not produce a lengthy roadmap instead of implementing the feature.

### Phase C — Implement

Implement the selected feature in the actual repository.

Prefer a complete vertical slice over numerous disconnected stubs. A vertical slice must connect the relevant input, implementation, runtime behavior, and test.

Do not stop after generating pseudocode, interfaces, empty source files, or architectural diagrams when a functional implementation is feasible.

If an important dependency blocks progress, implement and test the independent parts while documenting the precise blocker.

### Phase D — Verify

After each meaningful change:

1. Build the affected target.
2. Run relevant unit and integration tests.
3. Add regression tests for fixed defects.
4. Run formatting and static analysis when configured.
5. Inspect compiler and test output for errors and warnings.
6. Verify that the change did not break existing functionality.
7. Record the exact commands and actual results.

Never state that a build or test passed unless it was actually executed and passed.

If the full project cannot be built, run the narrowest available verification and explain what remains unverified.

### Phase E — Continue

After verification, proceed to the next useful implementation task without waiting for unnecessary approval when the next step is clear and remains within the established scope.

Do not repeat an investigation unless new evidence, a changed implementation, or a failing regression test justifies it.

## 6. Target Architecture

The final architecture has not been predetermined. Select and refine it using the existing repository, reference implementations, and tests.

The intended responsibilities are:

### Executable loader

- Parse PS-X EXE headers and loadable segments.
- Initialize the guest execution state.
- Identify entry points, memory regions, and executable sections.
- Investigate ISO 9660, SYSTEM.CNF, disc file access, overlays, and dynamically loaded modules.
- Handle formats and loading mechanisms actually encountered in supported games.

Do not assume all PS1 games consist of a single standalone PS-X EXE.

### MIPS translation

- Decode MIPS R3000A instructions.
- Implement or reuse a tested intermediate representation.
- Identify basic blocks, branches, and control-flow relationships.
- Preserve delay-slot behavior and guest-visible arithmetic semantics.
- Handle indirect jumps, exceptions, and guest state changes.
- Investigate self-modifying code and dynamically loaded executable content.
- Generate native x86-64 code or another justified intermediate representation.

Evaluate static recompilation, dynamic binary translation, and hybrid approaches. Do not assume that static recompilation alone can handle every game.

### Guest runtime

- Provide guest memory and correct address mapping.
- Implement the execution environment and required system services.
- Model exceptions, interrupts, timers, DMA, and synchronization as required.
- Provide the interfaces needed by translated game code.
- Maintain correct interactions between guest CPU state and other subsystems.

### PlayStation hardware compatibility

Implement hardware behavior incrementally using the reference emulator and available documentation.

Potential subsystems include:

- CPU and memory.
- GTE.
- GPU and VRAM.
- DMA and interrupts.
- Timers and event handling.
- CD-ROM and disc access.
- BIOS interfaces.
- SPU and audio output.
- Digital and analog controllers.
- Memory cards and save data.

Do not implement every subsystem before producing the first working execution path. Prioritize the dependencies required to boot and run a selected test program.

## 7. Vulkan Graphics Backend

Vulkan is the planned host graphics API. It does not replace the guest CPU, game logic, audio, or complete PS1 hardware model.

Keep the following responsibilities distinct:

1. Guest GPU command interpretation and PS1-visible behavior.
2. Guest VRAM state and memory-transfer semantics.
3. Translation of rendering operations into host GPU workloads.
4. Vulkan resource management, command submission, synchronization, and presentation.

Investigate and test:

- Textured and untextured polygons.
- Sprites and primitive ordering.
- Texture pages and CLUT.
- VRAM reads, writes, and transfers.
- Semi-transparency and blending modes.
- Dithering and rasterization differences.
- CPU/GPU synchronization and resource lifetime.
- DMA interactions and GPU-visible memory changes.

Do not assume that forwarding all polygons to a generic Vulkan pipeline reproduces PS1 behavior.

Use CPU fallbacks when required for correctness or when they provide a simpler reference implementation. Optimize only after measuring a working implementation.

Use the existing build configuration and available Vulkan SDK before introducing dependencies or changing toolchains.

## 8. Mouse and Camera Control

Treat mouse input as a separate subsystem from executable loading and graphics.

Implement general input support first:

- Relative mouse movement.
- Cursor capture and release.
- Sensitivity and axis inversion.
- Optional smoothing.
- Keyboard and gamepad mappings.
- Configurable per-game profiles.

Camera behavior is game-specific. Some games use analog-stick input, some use digital controls, and others implement camera movement through custom game logic.

For each target game:

1. Determine how the game reads input.
2. Identify the camera-control path when necessary.
3. Map mouse movement to the appropriate analog or digital input.
4. Test movement limits, dead zones, sensitivity, and camera behavior.
5. Introduce a game-specific integration module only when generic mapping is insufficient.

Do not claim universal mouse-look support without testing it on individual games.

## 9. Testing Strategy

Tests must be deterministic where practical and must not depend exclusively on visually inspecting a running game.

Prioritize:

- Executable header parsing.
- Memory mapping and address aliases.
- Instruction decoding.
- Arithmetic, branch, and delay-slot behavior.
- CPU state and memory comparisons against a reference implementation.
- Guest-to-host translation correctness.
- GPU command interpretation.
- Texture and VRAM operations.
- Vulkan rendering and synchronization.
- Input mapping and mouse sensitivity.
- Save/load behavior where implemented.
- End-to-end execution of a minimal test program.

Use homebrew test programs or legally usable test assets when possible. Do not require proprietary game data for basic unit tests.

For real games, maintain a compatibility matrix recording the game identifier, tested revision, execution stage, graphics, audio, input, known defects, and test environment.

## 10. Build and Repository Safety

- Discover the existing build system before choosing commands.
- Prefer existing project conventions and dependencies.
- Keep generated files and build artifacts out of source directories where practical.
- Never delete user data, source files, test assets, or build directories without explicit authorization.
- Do not perform destructive Git operations.
- Do not commit changes unless explicitly requested.
- Do not modify the two reference repositories.
- Do not introduce unrelated system-wide configuration changes.

Before changing build files, understand the existing target structure and preserve support for the intended host platforms.

## 11. Progress Reporting

At the end of each meaningful work session, provide a concise report containing:

1. What was implemented.
2. Which files were changed.
3. Which reference implementations and documentation informed the change.
4. Exact build and test commands executed.
5. Actual build and test results.
6. Known limitations and remaining defects.
7. The next concrete implementation step.

Distinguish verified behavior from assumptions and planned work.

## 12. First Task

Do not start by writing a large amount of boilerplate or redesigning the entire project.

First inspect the PSXReborn repository, both reference directories, and all relevant Markdown documentation under `docs/`.

Then:

1. Identify the existing repository state and build system.
2. Determine the most practical architecture for native MIPS translation and Vulkan rendering.
3. Create or update concise architecture documentation based on verified findings.
4. Select a minimal end-to-end milestone.
5. Implement the first functional component and its tests.
6. Build and run the tests.
7. Continue with the next concrete task rather than stopping at an audit.

The objective is measurable engineering progress toward a working PS1-to-PC toolchain.
