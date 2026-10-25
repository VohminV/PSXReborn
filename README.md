# PSXReborn

**PlayStation 1 → Native PC Relinker & Vulkan Runtime**

PSXReborn is an experimental open-source project focused on bringing original PlayStation 1 games to modern Windows and Linux PCs through native code translation, a custom runtime, and GPU-accelerated rendering with Vulkan.

The long-term goal is to build a reusable toolchain capable of translating PS1 game executables into native PC applications, with configurable keyboard, mouse, and gamepad controls — including mouse-driven camera control where supported.

> **Project status: Early development.** The architecture, compatibility layer, and toolchain are under development. Game compatibility and performance targets have not yet been established.

## Goals

- **Native code translation** — translate MIPS R3000A instructions into native x86-64 code.
- **Vulkan rendering** — implement a graphics backend that reproduces PlayStation GPU behavior using Vulkan.
- **PS1 runtime** — provide guest memory, executable loading, system services, and hardware compatibility.
- **Cross-platform support** — target Windows and Linux.
- **Modern input** — support keyboard, mouse, and gamepads.
- **Mouse camera control** — provide configurable mouse-look for games whose camera behavior can be mapped to mouse input.
- **Game compatibility** — progressively support executable formats, dynamic overlays, CD-ROM access, audio, and other PlayStation subsystems.
- **Testing and verification** — compare execution and rendering behavior against established PS1 emulators.

## Architecture

The project is intended to separate guest hardware behavior from host-specific implementation.

| Component | Responsibility |
| --- | --- |
| Executable Loader | Parse PS-X EXE files, disc images, and dynamically loaded modules |
| MIPS Translator | Decode guest instructions and translate executable code |
| Guest Runtime | Manage guest memory, exceptions, system services, and hardware interfaces |
| PS1 GPU Model | Reproduce GPU commands, VRAM operations, texture handling, and blending semantics |
| Vulkan Renderer | Execute rendering workloads on the host GPU |
| Audio Runtime | Implement PlayStation sound processing and host audio output |
| Input Backend | Handle keyboard, mouse, and gamepad input |
| Camera Profiles | Configure game-specific mouse-to-camera or mouse-to-analog-stick mappings |

The Vulkan renderer is not intended to replace the entire console architecture. CPU execution, memory behavior, synchronization, audio, and game-specific logic require separate implementations.

## Graphics

The planned Vulkan backend will investigate:

- Textured and untextured polygons.
- Sprites and primitive ordering.
- Texture pages and color lookup tables (CLUT).
- VRAM reads, writes, and transfers.
- Transparency and semi-transparency modes.
- Dithering and PlayStation-specific rasterization behavior.
- Synchronization between CPU-visible memory and GPU resources.

Rendering accuracy and performance will be evaluated separately. Vulkan acceleration must preserve the behavior required by the game rather than merely produce visually similar output in simple scenes.

## Native Code Translation

The translation pipeline will investigate:

1. Parsing executable headers and memory layout.
2. Decoding MIPS R3000A instructions.
3. Identifying basic blocks, branches, and control-flow relationships.
4. Translating guest code into native machine code or an intermediate representation.
5. Handling delay slots, exceptions, indirect jumps, and dynamically loaded code.
6. Detecting self-modifying code and invalidating translated blocks when necessary.
7. Verifying execution against reference implementations.

Static recompilation, dynamic binary translation, and hybrid approaches will be evaluated before the execution architecture is finalized.

## Mouse and Camera Controls

PSXReborn aims to provide modern mouse controls without requiring modifications to every game.

Planned features:

- Relative mouse input and cursor capture.
- Configurable horizontal and vertical sensitivity.
- Optional Y-axis inversion and smoothing.
- Keyboard and gamepad remapping.
- Per-game input profiles.
- Mouse-to-analog-stick translation where appropriate.
- Game-specific camera integration when standard input mapping is insufficient.

Camera control is game-dependent. Games without native camera control or suitable analog input may require additional reverse engineering and dedicated compatibility modules.

## Development Roadmap

- [ ] Repository structure and build system.
- [ ] Architecture and compatibility documentation.
- [ ] PS-X EXE parser and executable inspection tools.
- [ ] Guest memory model and MIPS instruction decoder.
- [ ] Instruction-level tests and reference comparisons.
- [ ] Initial native code translation pipeline.
- [ ] Minimal Vulkan rendering prototype.
- [ ] PS1 GPU command processing and VRAM model.
- [ ] Audio and controller runtime.
- [ ] Mouse input and configurable control profiles.
- [ ] First end-to-end game boot.
- [ ] Windows and Linux build verification.
- [ ] Compatibility testing with additional games.

A roadmap item will only be marked complete after implementation and reproducible verification.

## Development Principles

- Correctness before optimization.
- Measurable progress instead of repeated, inconclusive investigations.
- Reproducible builds and automated tests.
- Clear separation between guest hardware semantics and host graphics APIs.
- No invented compatibility claims or unverified performance figures.
- Documented limitations and game-specific requirements.

## Technical References

- [PSX-SPX — PlayStation Specifications](https://psx-spx.consoledev.net/)
- [PCSX-Redux — Open-source PlayStation emulator](https://github.com/grumpycoders/pcsx-redux)
- [DuckStation — PlayStation emulator with Vulkan rendering](https://github.com/stenzek/duckstation)
- [Vulkan Specification](https://docs.vulkan.org/spec/latest/)

These projects serve as technical references for hardware behavior, graphics, debugging, and compatibility. Their code and licenses must be reviewed before any implementation is reused.

## Legal Notice

PSXReborn is an independent development project and is not affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.

The project will not distribute commercial PlayStation game images, copyrighted game assets, or proprietary BIOS files. Users are responsible for obtaining and using game data in accordance with applicable law.

PlayStation and PS1 are trademarks of their respective owners.

## License

The project license has not yet been selected. Licensing terms will be added after the initial architecture and dependency review.
