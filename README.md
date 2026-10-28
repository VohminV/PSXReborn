# PSXReborn

**PlayStation 1 → Native PC Relinker & Vulkan Runtime**

PSXReborn is an experimental open-source project focused on bringing original PlayStation 1 games to modern Windows and Linux PCs through native code translation, a custom runtime, and GPU-accelerated rendering with Vulkan.

The long-term goal is to build a reusable toolchain capable of translating PS1 game executables into native PC applications, with configurable keyboard, mouse, and gamepad controls — including mouse-driven camera control where supported.

> **Project status: Early development.** The architecture, compatibility layer, and toolchain are under development. Game compatibility and performance targets have not yet been established.

The current implementation includes a PS-X EXE loader, guest memory model, an
interpreted MIPS R3000A CPU with COP0 and GTE, BIOS HLE hooks, CD-ROM image
access with `SYSTEM.CNF` boot parsing, interrupts, timers, DMA, partial SPU and
SIO pad emulation, and a software GPU rasterizer with a 1024x512 VRAM. An SDL2
frontend boots a disc image or PS-X EXE and presents the emulated display.

Native code translation and the Vulkan renderer are **not implemented yet**.
Rendering currently runs in the CPU rasterizer, and SDL2 copies VRAM into a
texture for presentation. See [docs/architecture.md](docs/architecture.md) for
the original milestone design.

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
| Executable Loader | Parse PS-X EXE files, CUE/ISO disc images, and `SYSTEM.CNF` boot paths |
| MIPS Translator | Decode guest instructions; currently interpreted, native translation planned |
| Guest Runtime | Guest memory, exceptions, interrupts, timers, DMA, BIOS HLE, and hardware interfaces |
| PS1 GPU Model | Software command interpreter, 1024x512 VRAM, texture sampling, and display registers |
| Vulkan Renderer | Planned host GPU backend; not integrated. SDL2 presents the software framebuffer today |
| Audio Runtime | Partial SPU register, ADPCM, and DMA model; no host audio output yet |
| Input Backend | SDL2 keyboard/gamepad mapped to the SIO pad; mouse support planned |
| Camera Profiles | Planned game-specific mouse-to-camera mappings |

The Vulkan renderer is not intended to replace the entire console architecture. CPU execution, memory behavior, synchronization, audio, and game-specific logic require separate implementations.

## Build and Test

Requirements: CMake 3.20 or newer, a C++20 compiler, and Ninja. SDL2 is optional;
when found, CMake also builds the `psxreborn_play` frontend.

```sh
cmake -S . -B build -G Ninja -DPSXREBORN_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Inspect, execute, or boot:

```sh
./build/psxreborn inspect path/to/program.exe
./build/psxreborn run path/to/program.exe --max-steps 1000000
./build/psxreborn boot path/to/disc.cue --max-steps 33868800
./build/psxreborn_play path/to/disc.cue
```

## Graphics

### Current implementation

The GPU is a software rasterizer writing into a 1024x512 16-bit VRAM. It handles
fill, polygon, rectangle, and VRAM copy commands; CPU-to-VRAM and VRAM-to-CPU
transfers; 4-bit, 8-bit, and 16-bit texture sampling; drawing area/offset and
mask bits; and GP1 display registers.

The SDL2 frontend converts VRAM to ARGB8888 and presents the current display area
through `SDL_Renderer`. This is not a Vulkan path. Semi-transparency, dithering,
and PlayStation display timing are not implemented.

### Planned Vulkan backend

The Vulkan backend will investigate:

- Textured and untextured polygons.
- Sprites and primitive ordering.
- Texture pages and color lookup tables (CLUT).
- VRAM reads, writes, and transfers.
- Transparency and semi-transparency modes.
- Dithering and PlayStation-specific rasterization behavior.
- Synchronization between CPU-visible memory and GPU resources.

Rendering accuracy and performance will be evaluated separately. Vulkan acceleration must preserve the behavior required by the game rather than merely produce visually similar output in simple scenes.

## Performance

The execution engine is currently an interpreter. In a Release build on the
development machine it executed about 24 million instructions per second. A 60
FPS PS1 frame budget is 564,480 CPU instructions, so interpreter throughput is
the main frame-rate limit. The play loop also caps presentation at roughly
60 FPS.

Vulkan will reduce rendering and presentation overhead, but it will not remove
the CPU-bound limit on its own; native translation is required for large gains.

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

- [x] Repository structure and build system.
- [x] Architecture and compatibility documentation.
- [x] PS-X EXE parser and executable inspection tools.
- [x] Guest memory model and MIPS instruction decoder.
- [x] Instruction-level tests and reference comparisons.
- [x] MIPS R3000A interpreter with COP0, GTE, interrupts, timers, and DMA.
- [x] BIOS HLE hooks, CD-ROM image access, and `SYSTEM.CNF` boot parsing.
- [x] Software GPU command interpreter and VRAM model.
- [x] SDL2 play frontend with keyboard and gamepad input.
- [ ] Initial native code translation pipeline.
- [ ] Minimal Vulkan rendering prototype.
- [ ] Complete PS1 GPU rendering semantics (semi-transparency, dithering, display timing).
- [ ] Host audio output and full SPU behavior.
- [ ] Mouse input and configurable control profiles.
- [ ] First end-to-end playable game boot.
- [x] Linux build and automated test suite.
- [ ] Windows build verification.
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
