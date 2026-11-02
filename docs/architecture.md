# PSXReborn Architecture

## Current State

PSXReborn parses PS-X EXE files and disc images, boots `SYSTEM.CNF` through BIOS
HLE hooks, executes MIPS R3000A code with an interpreter or JIT, and models
interrupts, timers, DMA, GPU commands, partial SPU behavior, CD-ROM, SIO0
controllers, and memory cards. The SDL2 frontend provides the current display,
audio, and input path. Vulkan support is currently limited to embedded shader
modules and specialization constants. Detailed verified status is maintained in
`docs/IMPLEMENTATION_STAGES.md`.

## Code Layout

| Path | Responsibility |
| --- | --- |
| `include/psxreborn/memory.hpp`, `src/memory.cpp` | Guest address decoding, RAM, scratchpad, BIOS window, faults |
| `include/psxreborn/psx_exe.hpp`, `src/psx_exe.cpp` | PS-X EXE header parsing and load validation |
| `include/psxreborn/mips.hpp`, `src/mips.cpp` | Instruction decoding and branch/jump target calculation |
| `include/psxreborn/cpu.hpp`, `src/cpu.cpp` | Integer interpreter, delay slots, load delay, stop conditions |
| `include/psxreborn/sio0.hpp`, `src/sio0.cpp` | SIO0 controller/memory-card serial ports, status and control registers |
| `include/psxreborn/pad.hpp`, `src/pad.cpp` | Active-low PS1 pad state assembled from keyboard, controller, and stick sources |
| `include/psxreborn/bios.hpp`, `src/bios.cpp` | BIOS HLE calls, including the pad buffer interface |
| `include/psxreborn/vulkan_shader.hpp`, `src/vulkan_shader.cpp` | Embedded SPIR-V lookup, validation, specialization constants, and `VkShaderModule` creation |
| `shaders/`, `tools/embed_spirv.py`, `tools/build_shaders.sh` | GLSL sources, build-time compilation, and C++ embedding |
| `tools/psxreborn.cpp` | `inspect` and `run` command-line interface |
| `tests/` | Unit, integration, and CLI end-to-end tests |

The guest hardware model is kept separate from host tooling. The CLI consumes
the same core library that future translation and runtime components will use.

## Execution Flow

1. `parsePsxExe` validates the 2048-byte header and declared payload range.
2. `loadPsxExe` prevalidates writable ranges, writes the payload, then clears BSS.
3. `Cpu::resetFromExe` sets PC, `gp`, `sp`, `fp`, `a0`, and `a1`.
4. `Cpu::run` fetches, decodes, and executes instructions until a stop condition
   or the step limit is reached.
5. The CLI prints the stop reason, executed step count, PC, nonzero registers,
   `hi`, and `lo`.

## Guest Memory

Current address decoding:

| Region | Addresses | Behavior |
| --- | --- | --- |
| RAM mirror | `0x00000000-0x007FFFFF` | 2 MB RAM mirrored across 8 MB |
| RAM (KSEG0) | `0x80000000-0x807FFFFF` | Same 2 MB RAM mirrored across 8 MB |
| RAM (KSEG1) | `0xA0000000-0xA07FFFFF` | Same 2 MB RAM mirrored across 8 MB |
| Scratchpad | `0x1F800000-0x1F8003FF`, `0x9F800000-0x9F8003FF` | 1 KB, not executable |
| Scratchpad (KSEG1) | `0xBF800000-0xBF8003FF` | Unmapped bus error |
| BIOS window | `0x1FC00000-0x1FC7FFFF`, `0x9FC00000`, `0xBFC00000` | 512 KB zero-filled, read-only, executable |
| I/O | `0x1F801000-0x1F802FFF` and mirrors | Unsupported memory fault |
| Other KSEG2 | `0xC0000000-0xFFFFFFFF` | Unsupported memory fault |

The 8 MB mirror follows the accidental two-megabyte aliases documented in
`docs/memorycontrol.md`. RAM size is not configurable yet. Halfword and word
accesses require alignment; unaligned accesses produce an address error.
Scratchpad execution and KSEG1 scratchpad access are deliberately unsupported.

## SIO0 Controller Emulation

The core implements the digital controller poll sequence on SIO0 at
`0x1F801040-0x1F80104F`. A transaction is started by enabling TX and asserting
the port select line in `SIO0_CR` (bits 0 and 1), then writing the device
address `0x01`. A digital pad is identified by the first `0x42` command byte.

The controller ports are **SIO0** (`0x1F801040`); `0x1F801050-0x1F80105F` is
the unrelated SIO1 UART used by the serial port and must not be used for pad
polling.

Verified response sequence for a digital pad:

| Host command | Pad response | Meaning |
| --- | --- | --- |
| `0x01` | `0xFF` | Device address, pad pulls ACK low |
| `0x42` | `0x41` | Digital pad ID low byte |
| `0x00` | `0x5A` | ID high byte / data marker |
| `0x00` | buttons low | Select, Start, D-pad, L1/R1 low byte |
| `0x00` | buttons high | L2/R2, Triangle, Circle, Cross, Square |

Button bits are active-low. The verified PS1 switch mapping is:

| Bit | Button | Bit | Button |
| --- | --- | --- | --- |
| 0 | Select | 8 | L2 |
| 1 | L3 (analog mode only) | 9 | R2 |
| 2 | R3 (analog mode only) | 10 | L1 |
| 3 | Start | 11 | R1 |
| 4 | D-pad Up | 12 | Triangle |
| 5 | D-pad Right | 13 | Circle |
| 6 | D-pad Down | 14 | Cross |
| 7 | D-pad Left | 15 | Square |

`SIO0_SR` exposes TX-ready, RX-not-empty, /ACK level, and the sticky IRQ bit.
`SIO0_CR` bit 1 asserts /CS, bit 13 selects controller port 1 or 2, bit 4
acknowledges the IRQ latch, bit 6 resets the channel, bits 8-9 select the RX
FIFO IRQ threshold (1, 2, 4, or 8 bytes), and bits 11-12 enable RX and /ACK
interrupts. The receive path has an 8-byte FIFO; an overflow replaces the
newest entry. Writing to `SIO0_DR` starts a transfer immediately while TX is
enabled. A byte read from the same register removes one FIFO entry and updates
the RX-not-empty condition; 16-bit and 32-bit reads consume two and four
entries respectively. An RX-threshold interrupt is latched as soon as the FIFO
reaches the configured size. The /ACK interrupt is latched only when bit 12 is
set, after the emulated /ACK release delay (~130 cycles), matching the BIOS
requirement that the pad IRQ must not occur in the same cycle as the transfer.

The host controller input path uses GLFW (the play tool keeps SDL2 for the
window/renderer backend). The mapping layer speaks `ControllerSnapshot` in a
GLFW button id space: buttons 0..14 mirror `GLFW_GAMEPAD_BUTTON_*` one to one
(A->Cross, B->Circle, X->Square, Y->Triangle, LB->L1, RB->R1, Back->Select,
Start->Start, D-pad passed through), axis controls are `kHostAxisBase +
axis` in GLFW axis order (LX, LY, RX, RY, LT, RT; analog triggers drive L2/R2
and the left stick can emulate the D-pad after a deadzone). GLFW pads with a
gamepad mapping use `glfwGetGamepadState`; devices without a mapping fall back
to a raw joystick path that fills the snapshot's raw fields and transposes a
documented Xbox/DInput-style layout onto the semantic GLFW ids. Native
backends that receive an XInput digital button mask can still use
`xinputToPadButtons()` or `PadState::setXInputButtons()`; both apply the same
mapping. Per-device JSON profiles are identified by a device-name based
identity (`glfw:<name>`), because GLFW 3.3 exposes no stable joystick GUID
(`glfwGetJoystickGUID` exists only in GLFW 3.4+); SDL-format mapping databases
can be applied through `PSXREBORN_GAMEPAD_DB`.

### DualShock / analog pad

The guest-visible device is a DualShock-compatible endpoint
(`DualShockDevice` in `ps1_pad_device.hpp`). Command `0x42` returns ID `0x41`
in digital mode (4 data bytes) or `0x73`/`0xF3` in analog/config mode (8 data
bytes with stick bytes `rightX, rightY, leftX, leftY`). Config commands
`0x43`, `0x44`, `0x45`, `0x46`, `0x47`, `0x4C`, and `0x4D` are accepted only
in config mode and latch their data byte on the third transfer tick, matching
PCSX-Redux. DualShock starts in digital mode; games enable analog via the
config sequence. Rumble and multitap are not implemented.

### Low-latency input path

`Sio0` samples host input through the `PadSource` interface when the guest
asserts /CS and writes the pad address byte `0x01`. The snapshot is latched
for the whole transaction so mid-poll host changes cannot tear a response.
`GlfwBackend` (play tool) refreshes GLFW controller state at pad-select time
(throttled to 250 us; GLFW device state updates during `glfwPollEvents`),
independent of host frame cadence, then publishes into `PadState`. The BIOS
pad path calls `System::refreshPadFromSource()` before reading pad state so
HLE pad services also see fresh input.

Measured on this machine (Release build, digital `0x42` transaction, no host
controller attached): full transaction average 99 ns, host update to first
guest-visible button byte average 92 ns (p99 105 ns). The previous
frame-quantized path added up to one host frame of delay (16.7 ms at 60 FPS,
6.9 ms at 144 FPS). Instrumentation is available via
`PSXREBORN_TRACE_INPUT=1|2`.

The controller path is a single chain: GLFW gamepad state ->
`ControllerMapping` (per-device JSON profile) -> `PadState` -> `Sio0` pad
transaction; BIOS pad services read the same `PadState`. `HostControl` button
ids for GLFW gamepads must remain identical to `GLFW_GAMEPAD_BUTTON_*`
(face buttons 0..3, bumpers 4..5, back/start/guide 6..8, thumb clicks 9..10,
D-pad 11..14). If they drift, the default D-pad mapping silently becomes
L3/R3 and Home navigation watches the wrong bits.

For end-to-end diagnosis set `PSXREBORN_TRACE_CHAIN=1` (or
`PSXREBORN_TRACE_INPUT=3`). Every line carries the same play-loop frame id and
one of the layers `HOST`, `MAPPING`, `PAD`, `SIO0`, `BIOS`, `MENU`, so a single
physical press can be followed from the raw GLFW snapshot to the active-low
SIO response and BIOS buffer bytes. `PSXREBORN_TRACE_INPUT=1|2` remains the
lower-volume transaction summary. Home does not own a second controller:
opening it only suppresses the shared `GlfwBackend` sample and BIOS pad state,
and closing it re-enables the same mapping.

## Vulkan Shader Modules

The shader sources live in `shaders/`. CMake compiles them with
`glslangValidator --target-env vulkan1.1` into `build/shaders/*.spv`, then runs
`tools/embed_spirv.py` to produce `build/generated/spirv_data.h`. The Python
script validates the SPIR-V magic word and word alignment before emitting
`std::array` constants under `psxreborn::embedded_shaders`. When build-time
shader compilation is disabled, the checked-in fallback
`src/generated/spirv_data.h` is used.

`vulkan_shader.hpp` exposes `ShaderId`, `embeddedShader`, `validateSpirv`,
`PresentSpecializationInfo`, and `createShaderModule`. `present.frag` declares
two specialization constants: `IS_4K` (id 0) and `ENABLE_OVERLAY` (id 1). The
specialization map uses 32-bit `VkBool32` values, matching Vulkan's bool
representation. Tests validate every embedded module, the specialization map
layout, and create real `VkShaderModule` objects on an available Vulkan device;
they skip with an explicit message when no loader or device is present. There
is no swapchain, render pass, or PS1 rendering pipeline yet.

## PS-X EXE Loading

The parser reads the fields documented in `docs/cdromfileformats.md`: entry
point, global pointer, load address and size, data address and size, BSS address
and size, stack base and offset, and the region marker.

Validation currently covers:

- File size of at least `0x800` bytes.
- `PS-X EXE` magic.
- Load and BSS ranges that do not wrap the 32-bit address space.
- Declared payload that is present in the file.
- Load and BSS ranges that map to writable guest memory.

The parser does not require the declared load size to be a multiple of `0x800`.
Trailing file bytes after the declared payload are ignored. BSS is cleared after
the payload is written; overlapping BSS therefore clears the overlapping payload
bytes.

## R3000A Interpreter

Implemented integer instructions:

- Arithmetic: `add`, `addi`, `addiu`, `addu`, `sub`, `subu`.
- Logic: `and`, `andi`, `or`, `ori`, `xor`, `xori`, `nor`.
- Shifts: `sll`, `srl`, `sra`, `sllv`, `srlv`, `srav`.
- Comparisons: `slt`, `sltu`, `slti`, `sltiu`.
- Branches: `beq`, `bne`, `blez`, `bgtz`, `bltz`, `bgez`, `bltzal`, `bgezal`.
- Jumps: `j`, `jal`, `jr`, `jalr`.
- Loads: `lb`, `lbu`, `lh`, `lhu`, `lw`, `lwl`, `lwr`.
- Stores: `sb`, `sh`, `sw`, `swl`, `swr`.
- Multiply/divide: `mult`, `multu`, `div`, `divu`, `mfhi`, `mflo`, `mthi`, `mtlo`.
- Control: `nop`, `syscall`, `break`.

Semantics implemented:

- Branch and jump delay slots execute before transfer.
- `jal` and `jalr` write `pc + 8` to the link register.
- Load results become visible after the following instruction unless that
  instruction writes the same register.
- Back-to-back `lwl`/`lwr` accesses to the same register retain both partial
  results, matching the documented no-gap pairing.
- `lwl`/`lwr`/`swl`/`swr` align their memory access and merge only the relevant
  bytes.
- Signed overflow in `add`/`addi`/`sub` stops execution.
- An instruction fetch from an invalid region or a misaligned PC stops execution.
- `syscall` and `break` stop execution rather than entering exception handlers.

The pending-load model has one full-load slot plus partial-load chaining. It does
not yet reproduce every possible two-load in-flight sequence of the R3000A.

## CLI

```sh
psxreborn inspect <file.exe>
psxreborn run <file.exe> [--max-steps <count>]
```

`inspect` parses the header and verifies that the image can be loaded into guest
memory. `run` loads the image, executes from the header entry point, and prints
the resulting state.

Exit codes:

| Code | Meaning |
| --- | --- |
| `0` | `break`, `syscall`, or successful inspection |
| `1` | File, header, or load error |
| `2` | Invalid CLI usage or CPU execution fault |
| `3` | Step limit reached |

## Build and Test

```sh
cmake -S . -B build -G Ninja -DPSXREBORN_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests use a small custom harness because no test framework dependency has
been adopted. They cover memory mapping, PS-X EXE parsing and loading, decoding,
delay slots, arithmetic, loads and stores, CPU stop conditions, and CLI execution
of generated PS-X EXE test images.

## Known Limitations

- BIOS behavior is provided by HLE hooks rather than a real BIOS image or a full
  kernel reimplementation.
- COP1 (FPU) is not implemented, cache isolation is not modeled, and exception
  behavior is still incomplete for uncommon paths.
- SIO0 implements the digital and DualShock pad poll paths plus memory-card
  commands; rumble, multitap, and mouse are unsupported.
- Host controller input requires GLFW (play tool links `GlfwBackend`); the
  core itself has no platform input backend compiled in by default. Window,
  renderer, and Vulkan surface stay on SDL2.
- Vulkan support in the core is limited to shader module creation; the play
  tool owns the Vulkan presenter instance, swapchain, and render pass.
- Dynamic module and self-modifying-code handling exists but has not been
  validated across all supported games.
- No configurable RAM size; the 8 MB mirror is always enabled.
- Unimplemented I/O and KSEG2 accesses stop execution as unsupported.
- Arithmetic overflow and address errors can still terminate execution instead
  of following every guest exception path.
- A control-transfer instruction in a delay slot stops as unsupported.

## References

Hardware behavior was derived from the local PS1 documentation and cross-checked
against the reference projects:

- `docs/cdromfileformats.md` for the PS-X EXE header.
- `docs/memorymap.md` and `docs/memorycontrol.md` for address mapping and RAM
  mirroring.
- `docs/cpuspecifications.md` for load delay, branch delay, alignment, and
  unaligned load/store behavior.
- `docs/serialinterfacessio.md` and `docs/controllersandmemorycards.md` for
  SIO0 register behavior and the digital-pad protocol.
- `https://psx-spx.consoledev.net/controllersandmemorycards/` and
  `https://www.problemkaputt.de/psxspx-serial-port-sio.htm` for the verified
  poll sequence, controller IDs, switch bits, and SIO0 status/control bits.
- `/home/vita/pcsx-redux-main/src/core/sio.cc` and `src/core/pad.cc` for the
  reference SIO0/pad state machines and ACK/IRQ behavior.
- Microsoft XInput `XINPUT_GAMEPAD` documentation for the host button masks.
- `/home/vita/pcsx-redux-main/src/supportpsx/binloader.cc` for executable loading.
- `/home/vita/pcsx-redux-main/src/core/psxinterpreter.cc` and
  `/home/vita/pcsx-redux-main/src/core/r3000a.h` for interpreter behavior.
- `/home/vita/ps1-recomp-main/ps1Recomp/src/mips_decoder.cpp` and
  `/home/vita/ps1-recomp-main/ps1Runtime/include/runtime/memory.h` for decoder
  and memory-model comparisons.

`ps1-recomp` is GPLv3 and `pcsx-redux` is GPLv2. No reference source code has
been copied into PSXReborn; the implementations here were written from the
documented behavior and observed reference structure.

## Next Steps

1. Add exception and COP0 handling so `syscall`, `break`, overflow, and address
   errors can follow guest exception vectors.
2. Load and execute a legally usable BIOS or a homebrew bootstrap path.
3. Add a basic GPU command interpreter and VRAM model with CPU-visible behavior.
4. Add a first translation pipeline from decoded basic blocks to an intermediate
   representation, then to x86-64, using the interpreter as the reference.
5. Add compatibility fixtures and a per-game test matrix as broader execution
   paths become available.
