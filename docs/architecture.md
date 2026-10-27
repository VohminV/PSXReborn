# PSXReborn Architecture

## Current Milestone

Milestone 1 provides a complete integer execution slice for self-contained PS-X
EXE programs:

```text
PS-X EXE file
    -> header parsing and validation
    -> guest memory load (payload and BSS)
    -> MIPS R3000A integer instruction interpreter
    -> CLI inspection and bounded execution
```

The implementation is intentionally narrow. It does not yet load a BIOS, boot a
disc, run commercial games, emulate coprocessors, or generate native code.

## Code Layout

| Path | Responsibility |
| --- | --- |
| `include/psxreborn/memory.hpp`, `src/memory.cpp` | Guest address decoding, RAM, scratchpad, BIOS window, faults |
| `include/psxreborn/psx_exe.hpp`, `src/psx_exe.cpp` | PS-X EXE header parsing and load validation |
| `include/psxreborn/mips.hpp`, `src/mips.cpp` | Instruction decoding and branch/jump target calculation |
| `include/psxreborn/cpu.hpp`, `src/cpu.cpp` | Integer interpreter, delay slots, load delay, stop conditions |
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

- No BIOS image loading, BIOS calls, HLE, or kernel services.
- No COP0, COP1, COP2/GTE, cache isolation, or exception vectors.
- No interrupts, timers, DMA, GPU, SPU, CD-ROM, controllers, or memory cards.
- No ISO 9660 or `SYSTEM.CNF` handling, overlays, or dynamic modules.
- No native x86-64 translation yet; execution is interpreted.
- No self-modifying code handling or translated-block invalidation.
- No configurable RAM size; the 8 MB mirror is always enabled.
- I/O and KSEG2 accesses stop execution as unsupported.
- Arithmetic overflow and address errors stop execution instead of raising
  guest exceptions.
- A control-transfer instruction in a delay slot stops as unsupported.

## References

Hardware behavior was derived from the local PS1 documentation and cross-checked
against the reference projects:

- `docs/cdromfileformats.md` for the PS-X EXE header.
- `docs/memorymap.md` and `docs/memorycontrol.md` for address mapping and RAM
  mirroring.
- `docs/cpuspecifications.md` for load delay, branch delay, alignment, and
  unaligned load/store behavior.
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
