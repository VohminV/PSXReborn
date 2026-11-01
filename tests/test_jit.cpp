#include <array>
#include <cstdint>
#include <iostream>

#include "psxreborn/cpu.hpp"
#include "psxreborn/memory.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace {

constexpr uint32_t kCode = 0x80010000u;
constexpr uint32_t kData = 0x80011000u;
constexpr uint8_t kT0 = 8;
constexpr uint8_t kT1 = 9;
constexpr uint8_t kT2 = 10;
constexpr uint8_t kT3 = 11;
constexpr uint8_t kT4 = 12;
constexpr uint8_t kT5 = 13;
constexpr uint8_t kT6 = 14;
constexpr uint8_t kT7 = 15;
constexpr uint8_t kV0 = 2;
constexpr uint8_t kV1 = 3;

uint32_t encodeR(uint8_t opcode, uint8_t rs, uint8_t rt, uint8_t rd, uint8_t shamt,
                 uint8_t function) {
    return (static_cast<uint32_t>(opcode) << 26u) | (static_cast<uint32_t>(rs) << 21u) |
           (static_cast<uint32_t>(rt) << 16u) | (static_cast<uint32_t>(rd) << 11u) |
           (static_cast<uint32_t>(shamt) << 6u) | function;
}

uint32_t encodeI(uint8_t opcode, uint8_t rs, uint8_t rt, uint16_t immediate) {
    return (static_cast<uint32_t>(opcode) << 26u) | (static_cast<uint32_t>(rs) << 21u) |
           (static_cast<uint32_t>(rt) << 16u) | immediate;
}

uint32_t encodeJ(uint8_t opcode, uint32_t target) {
    return (static_cast<uint32_t>(opcode) << 26u) | ((target >> 2u) & 0x03FFFFFFu);
}

struct Machine {
    Memory memory;
    Cpu cpu{memory};

    explicit Machine(bool jit) {
        cpu.setExecutionMode(CpuExecutionMode::Hardware);
        cpu.setJitEnabled(jit);
    }

    void writeProgram() {
        const std::array<uint32_t, 30> program = {
            encodeI(0x09, 0, kT0, 5),          // addiu t0, zero, 5
            encodeI(0x09, 0, kT1, 0),          // addiu t1, zero, 0
            encodeR(0x00, kT1, kT0, kT1, 0, 0x21),  // loop: addu t1,t1,t0
            encodeI(0x09, kT0, kT0, 0xFFFF),   // addiu t0,t0,-1
            encodeI(0x05, kT0, 0, 0xFFFD),     // bne t0,zero,loop
            encodeI(0x09, 0, kT2, 7),          // delay: addiu t2,zero,7
            encodeI(0x0F, 0, kV0, 0x1234),     // lui v0,0x1234
            encodeI(0x0D, kV0, kV0, 0x5678),   // ori v0,v0,0x5678
            encodeI(0x0F, 0, kT4, 0x8001),     // lui t4,0x8001
            encodeI(0x0D, kT4, kT4, 0x1000),   // ori t4,t4,0x1000
            encodeI(0x2B, kT4, kV0, 0),        // sw v0,0(t4)
            encodeI(0x23, kT4, kT5, 0),        // lw t5,0(t4)
            encodeR(0x00, kT5, kT5, kT6, 0, 0x21),  // addu t6,t5,t5 (old value)
            encodeR(0x00, kT5, kT5, kT7, 0, 0x21),  // addu t7,t5,t5 (loaded value)
            encodeI(0x28, kT4, kV1, 0xE000),   // sb v1,0xE000(t4) -> 0x8001F000
            encodeI(0x20, kT4, kT7, 0),        // lb t7,0(t4)
            encodeR(0x00, kV0, kT2, kV0, 0, 0x18),  // mult v0,t2
            encodeR(0x00, 0, 0, 0, 0, 0x12),   // mflo v0
            encodeR(0x00, kV0, kT2, kV1, 0, 0x1A),  // div v0,t2
            encodeR(0x00, 0, 0, kV1, 0, 0x10), // mfhi v1
            encodeR(0x00, kV0, kT2, kV0, 0, 0x06),  // srlv v0,v0,t2
            encodeI(0x0A, kV0, kV0, 0x0001),   // slti v0,v0,1
            encodeR(0x00, kV0, kT2, kV0, 4, 0x00),  // sll v0,v0,4
            encodeI(0x2B, kT4, kV0, 4),        // sw v0,4(t4)
            encodeI(0x22, kT4, kV1, 4),        // lwl v1,4(t4)
            encodeI(0x26, kT4, kV1, 7),        // lwr v1,7(t4)
            encodeI(0x2A, kT4, kV1, 8),        // swl v1,8(t4)
            encodeI(0x2E, kT4, kV1, 11),       // swr v1,11(t4)
        };
        for (size_t index = 0; index < program.size(); ++index) {
            memory.write32(kCode + static_cast<uint32_t>(index) * 4u, program[index]);
        }
        memory.write32(kCode + 30u * 4u, encodeJ(0x02, kCode + 30u * 4u));
        memory.write32(kCode + 31u * 4u, 0);
    }

    void start() {
        cpu.setPc(kCode);
        cpu.setReg(kT3, 0xFFFFFFFFu);
    }
};

struct Snapshot {
    std::array<uint32_t, 32> regs{};
    uint32_t hi = 0;
    uint32_t lo = 0;
    uint32_t pc = 0;
    std::array<uint8_t, 32> data{};
};

Snapshot runAndSnapshot(bool jit, uint64_t steps) {
    Machine machine(jit);
    machine.writeProgram();
    machine.start();
    machine.memory.fill(kData, 32, 0);
    const CpuRunResult result = machine.cpu.run(steps);
    Snapshot snapshot;
    for (uint8_t index = 0; index < 32; ++index) {
        snapshot.regs[index] = machine.cpu.reg(index);
    }
    snapshot.hi = machine.cpu.hi();
    snapshot.lo = machine.cpu.lo();
    snapshot.pc = machine.cpu.pc();
    for (uint32_t index = 0; index < snapshot.data.size(); ++index) {
        machine.memory.read8(kData + index, snapshot.data[index]);
    }
    CHECK_EQ(static_cast<unsigned>(result.reason),
             static_cast<unsigned>(CpuStopReason::StepLimit));
    return snapshot;
}

}  // namespace

TEST_CASE(jit_matches_interpreter_for_integer_and_memory_program) {
    const Snapshot reference = runAndSnapshot(false, 400);
    const Snapshot jit = runAndSnapshot(true, 400);
    for (size_t index = 0; index < reference.regs.size(); ++index) {
        if (jit.regs[index] != reference.regs[index]) {
            std::cerr << "reg mismatch index=" << index << " ref=" << std::hex
                      << reference.regs[index] << " jit=" << jit.regs[index] << std::dec << "\n";
        }
        CHECK_EQ(jit.regs[index], reference.regs[index]);
    }
    CHECK_EQ(jit.hi, reference.hi);
    CHECK_EQ(jit.lo, reference.lo);
    CHECK_EQ(jit.pc, reference.pc);
    for (size_t index = 0; index < reference.data.size(); ++index) {
        CHECK_EQ(jit.data[index], reference.data[index]);
    }
    CHECK_EQ(reference.regs[kT1], 15u);
    CHECK_EQ(reference.regs[kT2], 7u);
    CHECK_EQ(reference.regs[kT6], 0u);
    CHECK(reference.regs[kT7] != 0u);
}

TEST_CASE(jit_handles_self_modifying_code_invalidation) {
    Machine machine(true);
    const uint32_t program[] = {
        encodeI(0x09, 0, kV0, 1),
        encodeJ(0x02, kCode + 16u),
        0,
        encodeI(0x09, 0, kV0, 2),
        encodeJ(0x02, kCode + 16u),
        0,
    };
    for (uint32_t index = 0; index < 6; ++index) {
        machine.memory.write32(kCode + index * 4u, program[index]);
    }
    machine.start();
    CpuRunResult result = machine.cpu.run(6);
    CHECK_EQ(static_cast<unsigned>(result.reason),
             static_cast<unsigned>(CpuStopReason::StepLimit));
    CHECK_EQ(machine.cpu.reg(kV0), 1u);
    machine.memory.write32(kCode, encodeI(0x09, 0, kV0, 9));
    machine.cpu.setPc(kCode);
    result = machine.cpu.run(1);
    CHECK_EQ(machine.cpu.reg(kV0), 9u);
}
