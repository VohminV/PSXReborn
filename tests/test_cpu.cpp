#include "psxreborn/cpu.hpp"

#include "psxreborn/memory.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using psxreborn::Cpu;
using psxreborn::CpuStopReason;
using psxreborn::Memory;

namespace {

constexpr uint32_t kProgramBase = 0x80010000;
constexpr uint32_t kBreak = 0x0000000Du;
constexpr uint32_t kNop = 0x00000000u;

uint32_t runProgram(Memory& memory, Cpu& cpu, std::initializer_list<uint32_t> words, uint64_t maxSteps = 256) {
    test::writeProgram(memory, kProgramBase, words);
    cpu.reset();
    cpu.setPc(kProgramBase);
    const psxreborn::CpuRunResult result = cpu.run(maxSteps);
    CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::Breakpoint));
    return result.pc;
}

}  // namespace

TEST_CASE(cpu_initial_state_from_exe) {
    Memory memory;
    Cpu cpu(memory);
    cpu.resetFromExe(0x80010000, 0x80010400, 0x801FFFF0, 0x10);
    CHECK_EQ(cpu.pc(), 0x80010000u);
    CHECK_EQ(cpu.reg(28), 0x80010400u);
    CHECK_EQ(cpu.reg(29), 0x80200000u);
    CHECK_EQ(cpu.reg(30), 0x80200000u);
    CHECK_EQ(cpu.reg(4), 1u);
    CHECK_EQ(cpu.reg(5), 0u);
    CHECK_EQ(cpu.reg(0), 0u);

    cpu.setReg(0, 0xFFFFFFFFu);
    CHECK_EQ(cpu.reg(0), 0u);

    cpu.resetFromExe(0x80010000, 0, 0, 0);
    CHECK_EQ(cpu.reg(29), 0u);
    CHECK_EQ(cpu.reg(30), 0u);
}

TEST_CASE(cpu_arithmetic_and_logical_operations) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeI(0x0F, 0, 1, 0x1234),
        test::encodeI(0x0D, 1, 1, 0x5678),
        test::encodeI(0x09, 1, 2, 0xF000),
        test::encodeR(1, 2, 3, 0, 0x21),
        test::encodeR(3, 2, 4, 0, 0x23),
        test::encodeR(1, 3, 5, 0, 0x24),
        test::encodeR(1, 3, 6, 0, 0x25),
        test::encodeR(1, 3, 7, 0, 0x26),
        test::encodeR(1, 3, 8, 0, 0x27),
        test::encodeR(2, 1, 9, 0, 0x2A),
        test::encodeR(2, 1, 10, 0, 0x2B),
        kBreak,
    });

    CHECK_EQ(cpu.reg(1), 0x12345678u);
    CHECK_EQ(cpu.reg(2), 0x12344678u);
    CHECK_EQ(cpu.reg(3), 0x24689CF0u);
    CHECK_EQ(cpu.reg(4), 0x12345678u);
    CHECK_EQ(cpu.reg(5), 0x00201470u);
    CHECK_EQ(cpu.reg(6), 0x367CDEF8u);
    CHECK_EQ(cpu.reg(7), 0x365CCA88u);
    CHECK_EQ(cpu.reg(8), 0xC9832107u);
    CHECK_EQ(cpu.reg(9), 1u);
    CHECK_EQ(cpu.reg(10), 1u);
}

TEST_CASE(cpu_shift_operations) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeI(0x09, 0, 1, 0xFFF8),
        test::encodeI(0x09, 0, 2, 2),
        test::encodeR(0, 1, 3, 4, 0x00),
        test::encodeR(0, 1, 4, 4, 0x02),
        test::encodeR(0, 1, 5, 4, 0x03),
        test::encodeR(2, 1, 6, 0, 0x04),
        test::encodeR(2, 1, 7, 0, 0x06),
        test::encodeR(2, 1, 8, 0, 0x07),
        kBreak,
    });

    CHECK_EQ(cpu.reg(1), 0xFFFFFFF8u);
    CHECK_EQ(cpu.reg(3), 0xFFFFFF80u);
    CHECK_EQ(cpu.reg(4), 0x0FFFFFFFu);
    CHECK_EQ(cpu.reg(5), 0xFFFFFFFFu);
    CHECK_EQ(cpu.reg(6), 0xFFFFFFE0u);
    CHECK_EQ(cpu.reg(7), 0x3FFFFFFEu);
    CHECK_EQ(cpu.reg(8), 0xFFFFFFFEu);
}

TEST_CASE(cpu_branch_delay_slots_always_execute) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeI(0x09, 0, 1, 0),
        test::encodeI(0x04, 1, 0, 2),
        test::encodeI(0x09, 0, 2, 1),
        test::encodeI(0x09, 0, 2, 2),
        test::encodeI(0x05, 1, 0, 2),
        test::encodeI(0x09, 0, 3, 3),
        test::encodeI(0x09, 3, 3, 1),
        test::encodeI(0x04, 0, 0, 1),
        test::encodeI(0x09, 0, 4, 4),
        kBreak,
    });

    CHECK_EQ(cpu.reg(2), 1u);
    CHECK_EQ(cpu.reg(3), 4u);
    CHECK_EQ(cpu.reg(4), 4u);
}

TEST_CASE(cpu_jal_jr_link_and_return) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeJ(0x03, (kProgramBase + 0x10u) >> 2),
        test::encodeI(0x09, 0, 3, 9),
        test::encodeI(0x09, 0, 4, 1),
        kBreak,
        test::encodeI(0x09, 0, 2, 5),
        test::encodeR(31, 0, 0, 0, 0x08),
        test::encodeI(0x09, 2, 2, 1),
    });

    CHECK_EQ(cpu.reg(2), 6u);
    CHECK_EQ(cpu.reg(3), 9u);
    CHECK_EQ(cpu.reg(4), 1u);
    CHECK_EQ(cpu.reg(31), kProgramBase + 8u);
}

TEST_CASE(cpu_jalr_uses_source_before_linking) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeI(0x0F, 0, 1, 0x8001),
        test::encodeI(0x0D, 1, 1, 0x0014),
        test::encodeR(1, 0, 5, 0, 0x09),
        test::encodeI(0x09, 0, 3, 7),
        kBreak,
        test::encodeI(0x09, 0, 2, 1),
        test::encodeR(5, 0, 0, 0, 0x08),
        test::encodeI(0x09, 2, 2, 1),
    });

    CHECK_EQ(cpu.reg(2), 2u);
    CHECK_EQ(cpu.reg(3), 7u);
    CHECK_EQ(cpu.reg(5), kProgramBase + 0x10u);
}

TEST_CASE(cpu_load_delay_and_cancellation) {
    {
        Memory memory;
        Cpu cpu(memory);
        CHECK(memory.write32(0x80020000, 0x12345678u));
        runProgram(memory, cpu, {
            test::encodeI(0x0F, 0, 1, 0x8002),
            test::encodeI(0x23, 1, 2, 0),
            test::encodeR(2, 0, 3, 0, 0x21),
            kBreak,
        });
        CHECK_EQ(cpu.reg(2), 0x12345678u);
        CHECK_EQ(cpu.reg(3), 0u);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        CHECK(memory.write32(0x80020000, 0x12345678u));
        runProgram(memory, cpu, {
            test::encodeI(0x0F, 0, 1, 0x8002),
            test::encodeI(0x23, 1, 2, 0),
            test::encodeI(0x09, 2, 2, 1),
            kBreak,
        });
        CHECK_EQ(cpu.reg(2), 1u);
    }
}

TEST_CASE(cpu_load_store_sizes_and_sign_extension) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeI(0x0F, 0, 1, 0x8002),
        test::encodeI(0x09, 0, 2, 0x1234),
        test::encodeI(0x28, 1, 2, 0),
        test::encodeI(0x29, 1, 2, 2),
        test::encodeI(0x2B, 1, 2, 4),
        test::encodeI(0x24, 1, 3, 0),
        test::encodeI(0x25, 1, 4, 2),
        test::encodeI(0x23, 1, 5, 4),
        test::encodeI(0x20, 1, 6, 0),
        test::encodeI(0x09, 0, 7, 0xFFFF),
        test::encodeI(0x28, 1, 7, 8),
        test::encodeI(0x20, 1, 8, 8),
        test::encodeI(0x24, 1, 9, 8),
        kNop,
        kBreak,
    });

    CHECK_EQ(cpu.reg(3), 0x34u);
    CHECK_EQ(cpu.reg(4), 0x1234u);
    CHECK_EQ(cpu.reg(5), 0x00001234u);
    CHECK_EQ(cpu.reg(6), 0x34u);
    CHECK_EQ(cpu.reg(8), 0xFFFFFFFFu);
    CHECK_EQ(cpu.reg(9), 0xFFu);
    CHECK_EQ(test::readWord(memory, 0x80020008), 0xFFu);
}

TEST_CASE(cpu_lwl_lwr_merge_unaligned_words) {
    Memory memory;
    Cpu cpu(memory);
    CHECK(memory.write32(0x80020000, 0x44332211u));
    runProgram(memory, cpu, {
        test::encodeI(0x0F, 0, 1, 0x8002),
        test::encodeI(0x0F, 0, 2, 0xFFFF),
        test::encodeI(0x0D, 2, 2, 0xEEEE),
        test::encodeI(0x22, 1, 2, 3),
        test::encodeI(0x26, 1, 3, 0),
        test::encodeI(0x0F, 0, 4, 0xFFFF),
        test::encodeI(0x0D, 4, 4, 0xEEEE),
        test::encodeI(0x22, 1, 4, 0),
        test::encodeI(0x26, 1, 5, 3),
        kNop,
        kBreak,
    });

    CHECK_EQ(cpu.reg(2), 0x44332211u);
    CHECK_EQ(cpu.reg(3), 0x44332211u);
    CHECK_EQ(cpu.reg(4), 0x11FFEEEEu);
    CHECK_EQ(cpu.reg(5), 0x44u);
}

TEST_CASE(cpu_lwl_lwr_same_register_pair_without_gap) {
    Memory memory;
    Cpu cpu(memory);
    CHECK(memory.write32(0x80020000, 0x44332211u));
    runProgram(memory, cpu, {
        test::encodeI(0x0F, 0, 1, 0x8002),
        test::encodeI(0x0F, 0, 2, 0xFFFF),
        test::encodeI(0x0D, 2, 2, 0xEEEE),
        test::encodeI(0x22, 1, 2, 0),
        test::encodeI(0x26, 1, 2, 3),
        kNop,
        kBreak,
    });

    CHECK_EQ(cpu.reg(2), 0x11FFEE44u);
}

TEST_CASE(cpu_swl_swr_write_partial_words) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeI(0x0F, 0, 1, 0x8002),
        test::encodeI(0x0F, 0, 6, 0x4433),
        test::encodeI(0x0D, 6, 6, 0x2211),
        test::encodeI(0x2B, 1, 6, 0x10),
        test::encodeI(0x2B, 1, 6, 0x20),
        test::encodeI(0x0F, 0, 5, 0xAABB),
        test::encodeI(0x0D, 5, 5, 0xCCDD),
        test::encodeI(0x09, 1, 7, 0x10),
        test::encodeI(0x09, 1, 8, 0x20),
        test::encodeI(0x2A, 7, 5, 1),
        test::encodeI(0x2E, 8, 5, 2),
        kBreak,
    });

    CHECK_EQ(test::readWord(memory, 0x80020010), 0x4433AABBu);
    CHECK_EQ(test::readWord(memory, 0x80020020), 0xCCDD2211u);
}

TEST_CASE(cpu_multiply_divide_and_hi_lo) {
    Memory memory;
    Cpu cpu(memory);
    runProgram(memory, cpu, {
        test::encodeI(0x09, 0, 1, 0xFFFD),
        test::encodeI(0x09, 0, 2, 7),
        test::encodeR(1, 2, 0, 0, 0x18),
        test::encodeR(0, 0, 3, 0, 0x10),
        test::encodeR(0, 0, 4, 0, 0x12),
        test::encodeI(0x09, 0, 5, 20),
        test::encodeI(0x09, 0, 6, 6),
        test::encodeR(5, 6, 0, 0, 0x1A),
        test::encodeR(0, 0, 7, 0, 0x12),
        test::encodeR(0, 0, 8, 0, 0x10),
        test::encodeR(5, 0, 0, 0, 0x1A),
        test::encodeR(0, 0, 9, 0, 0x12),
        test::encodeR(0, 0, 10, 0, 0x10),
        kBreak,
    });

    CHECK_EQ(cpu.reg(3), 0xFFFFFFFFu);
    CHECK_EQ(cpu.reg(4), 0xFFFFFFEBu);
    CHECK_EQ(cpu.reg(7), 3u);
    CHECK_EQ(cpu.reg(8), 2u);
    CHECK_EQ(cpu.reg(9), 0xFFFFFFFFu);
    CHECK_EQ(cpu.reg(10), 20u);
}

TEST_CASE(cpu_stops_on_overflow_and_unsupported_instructions) {
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {
            test::encodeI(0x0F, 0, 1, 0x7FFF),
            test::encodeI(0x0D, 1, 1, 0xFFFF),
            test::encodeI(0x08, 1, 2, 1),
            kBreak,
        });
        cpu.reset();
        cpu.setPc(kProgramBase);
        const psxreborn::CpuRunResult result = cpu.run(16);
        CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::ArithmeticOverflow));
        CHECK_EQ(result.pc, kProgramBase + 8u);
        CHECK_EQ(cpu.reg(2), 0u);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {test::encodeI(0x10, 0, 2, 0)});
        cpu.reset();
        cpu.setPc(kProgramBase);
        const psxreborn::CpuRunResult result = cpu.run(16);
        CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::UnsupportedInstruction));
        CHECK_EQ(result.instruction, test::encodeI(0x10, 0, 2, 0));
    }
}

TEST_CASE(cpu_stops_on_step_limit_break_and_syscall) {
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {
            test::encodeI(0x04, 0, 0, 0xFFFF),
            kNop,
        });
        cpu.reset();
        cpu.setPc(kProgramBase);
        const psxreborn::CpuRunResult result = cpu.run(10);
        CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::StepLimit));
        CHECK_EQ(result.steps, 10u);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        runProgram(memory, cpu, {kBreak});
        CHECK_EQ(static_cast<int>(cpu.haltReason()), static_cast<int>(CpuStopReason::Breakpoint));
    }
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {test::encodeR(0, 0, 0, 0, 0x0C)});
        cpu.reset();
        cpu.setPc(kProgramBase);
        const psxreborn::CpuRunResult result = cpu.run(16);
        CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::Syscall));
    }
}

TEST_CASE(cpu_reports_alignment_bus_and_fetch_errors) {
    {
        Memory memory;
        Cpu cpu(memory);
        test::writeProgram(memory, kProgramBase, {
            test::encodeI(0x0F, 0, 1, 0x8002),
            test::encodeI(0x29, 1, 2, 1),
            kBreak,
        });
        cpu.reset();
        cpu.setPc(kProgramBase);
        const psxreborn::CpuRunResult result = cpu.run(16);
        CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::AddressError));
        CHECK_EQ(result.address, 0x80020001u);
    }
    {
        Memory memory;
        Cpu cpu(memory);
        cpu.setPc(0x1F800000);
        const psxreborn::CpuStepResult result = cpu.step();
        CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::BusError));
    }
    {
        Memory memory;
        Cpu cpu(memory);
        cpu.setPc(0x80010002);
        const psxreborn::CpuStepResult result = cpu.step();
        CHECK_EQ(static_cast<int>(result.reason), static_cast<int>(CpuStopReason::AddressError));
    }
}
