#include "psxreborn/cpu.hpp"

#include "psxreborn/memory.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using psxreborn::Cpu;
using psxreborn::CpuExecutionMode;
using psxreborn::CpuStopReason;
using psxreborn::Memory;

namespace {

constexpr uint32_t kProgramBase = 0x80010000;
constexpr uint32_t kVector = 0x80000080;
constexpr uint32_t kBreak = 0x0000000Du;
constexpr uint32_t kNop = 0x00000000u;

uint32_t encodeCop0(uint8_t rs, uint8_t rt, uint8_t rd) {
    return 0x40000000u | test::encodeR(rs, rt, rd, 0, 0);
}

uint32_t encodeCop2Transfer(uint8_t rs, uint8_t rt, uint8_t rd) {
    return 0x48000000u | test::encodeR(rs, rt, rd, 0, 0);
}

void prepareHardware(Cpu& cpu) {
    cpu.setExecutionMode(CpuExecutionMode::Hardware);
    cpu.setCop0(12, 0);
}

}  // namespace

TEST_CASE(cop0_moves_status_and_rfe_restores_stack) {
    Memory memory;
    Cpu cpu(memory);
    test::writeProgram(memory, kProgramBase, {
        test::encodeI(0x0D, 0, 8, 0x0401),
        encodeCop0(4, 8, 12),
        encodeCop0(0, 9, 12),
        kNop,
        kBreak,
    });
    cpu.reset();
    cpu.setPc(kProgramBase);
    const psxreborn::CpuRunResult result = cpu.run(16);
    CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::Breakpoint));
    CHECK_EQ(cpu.reg(9), 0x0401u);
    CHECK_EQ(cpu.status(), 0x0401u);

    test::writeProgram(memory, kProgramBase, {0x42000010u, kBreak});
    cpu.reset();
    cpu.setPc(kProgramBase);
    cpu.setCop0(12, 0x3Cu);
    const psxreborn::CpuRunResult rfeResult = cpu.run(4);
    CHECK_EQ(static_cast<int>(rfeResult.reason), static_cast<int>(CpuStopReason::Breakpoint));
    CHECK_EQ(cpu.status(), 0x3Fu);
}

TEST_CASE(cop0_syscall_enters_vector_and_rfe_returns) {
    Memory memory;
    Cpu cpu(memory);
    test::writeProgram(memory, kProgramBase, {
        test::encodeR(0, 0, 0, 0, 0x0C),
        test::encodeI(0x09, 0, 2, 1),
        test::encodeI(0x04, 0, 0, 0xFFFF),
        kNop,
    });
    test::writeProgram(memory, kVector, {
        encodeCop0(0, 26, 14),
        kNop,
        test::encodeI(0x09, 26, 26, 4),
        test::encodeR(26, 0, 0, 0, 0x08),
        0x42000010u,
    });
    cpu.reset();
    cpu.setPc(kProgramBase);
    cpu.setCop0(12, 1);
    cpu.setExecutionMode(CpuExecutionMode::Hardware);

    const psxreborn::CpuStepResult first = cpu.step();
    CHECK_EQ(static_cast<int>(first.reason), static_cast<int>(CpuStopReason::None));
    CHECK_EQ(cpu.pc(), kVector);
    CHECK_EQ(cpu.epc(), kProgramBase);
    CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, 8u);
    CHECK_EQ(cpu.status() & 1u, 0u);
    CHECK_EQ(cpu.status() & 4u, 4u);

    const psxreborn::CpuRunResult result = cpu.run(16);
    CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::StepLimit));
    CHECK_EQ(cpu.reg(2), 1u);
    CHECK_EQ(cpu.status() & 1u, 1u);
}

TEST_CASE(cop0_break_overflow_address_error_and_interrupt_causes) {
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {kBreak});
        cpu.reset();
        cpu.setPc(kProgramBase);
        prepareHardware(cpu);
        cpu.step();
        CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, 9u);
        CHECK_EQ(cpu.epc(), kProgramBase);
        CHECK_EQ(cpu.pc(), kVector);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {test::encodeI(0x08, 8, 9, 1)});
        cpu.reset();
        cpu.setPc(kProgramBase);
        cpu.setReg(8, 0x7FFFFFFFu);
        prepareHardware(cpu);
        cpu.step();
        CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, 12u);
        CHECK_EQ(cpu.epc(), kProgramBase);
        CHECK_EQ(cpu.pc(), kVector);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {test::encodeI(0x29, 8, 9, 1)});
        cpu.reset();
        cpu.setPc(kProgramBase);
        cpu.setReg(8, 0x80020000u);
        prepareHardware(cpu);
        cpu.step();
        CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, 5u);
        CHECK_EQ(cpu.badVaddr(), 0x80020001u);
        CHECK_EQ(cpu.epc(), kProgramBase);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {kNop, kNop});
        cpu.reset();
        cpu.setPc(kProgramBase);
        cpu.setCop0(12, 0x0401u);
        cpu.setExecutionMode(CpuExecutionMode::Hardware);
        cpu.setInterruptSource([] { return true; });
        cpu.step();
        CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, 0u);
        CHECK_EQ(cpu.epc(), kProgramBase);
        CHECK_EQ(cpu.pc(), kVector);
        CHECK_EQ(cpu.status() & 4u, 4u);
    }
}

TEST_CASE(cop0_invalid_register_and_coprocessor_2_disable_raise) {
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {encodeCop0(0, 2, 0)});
        cpu.reset();
        cpu.setPc(kProgramBase);
        prepareHardware(cpu);
        cpu.step();
        CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, 10u);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {0x4A000000u | 0x0480012u});
        cpu.reset();
        cpu.setPc(kProgramBase);
        prepareHardware(cpu);
        cpu.step();
        CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, 11u);
        CHECK_EQ((cpu.cause() >> 28) & 3u, 2u);
    }
}

TEST_CASE(cop0_cop2_transfers_and_command_execute) {
    Memory memory;
    Cpu cpu(memory);
    test::writeProgram(memory, kProgramBase, {
        test::encodeI(0x0D, 0, 8, 0x1000),
        encodeCop2Transfer(6, 8, 0),
        test::encodeI(0x0D, 0, 8, 0x1000),
        encodeCop2Transfer(6, 8, 2),
        test::encodeI(0x0D, 0, 8, 0x1000),
        encodeCop2Transfer(6, 8, 4),
        test::encodeI(0x0D, 0, 8, 0x10),
        encodeCop2Transfer(6, 8, 5),
        test::encodeI(0x0D, 0, 8, 1),
        encodeCop2Transfer(4, 8, 0),
        test::encodeI(0x0D, 0, 8, 3),
        encodeCop2Transfer(4, 8, 1),
        0x4A000000u | 0x0480012u,
        encodeCop2Transfer(0, 9, 25),
        kNop,
        kBreak,
    });
    cpu.reset();
    cpu.setPc(kProgramBase);
    cpu.setCop0(12, 0x40000000u);
    const psxreborn::CpuRunResult result = cpu.run(32);
    CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::Breakpoint));
    CHECK_EQ(cpu.reg(9), 0x11u);
    CHECK_EQ(cpu.gte().readData(9), 0x11u);
}
