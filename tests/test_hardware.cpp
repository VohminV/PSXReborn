#include "psxreborn/cpu.hpp"
#include "psxreborn/hardware.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/mips.hpp"

#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace psxreborn;

namespace {

constexpr uint32_t kProgramBase = 0x80010000u;
constexpr uint32_t kBreak = 0x0000000Du;

}  // namespace

TEST_CASE(sio1_registers_are_not_a_controller_port) {
    Memory memory;
    System system(memory);
    system.reset();
    system.setPadButtons(0xFFFFu & ~0x4000u);

    CHECK(memory.write8(0x1F801050u, 0x01u));
    CHECK(memory.write8(0x1F801050u, 0x42u));
    uint8_t data = 0;
    CHECK(memory.read8(0x1F801050u, data));
    CHECK_EQ(data, 0x42u);

    uint16_t status = 0xFFFFu;
    CHECK(memory.read16(0x1F801054u, status));
    CHECK_EQ(status, 0u);
}

TEST_CASE(interrupts_mask_status_and_acknowledge) {
    Memory memory;
    System system(memory);
    system.reset();

    MemoryFault fault = MemoryFault::None;
    system.interrupts().request(Interrupts::kTimer0);
    CHECK(memory.write32(Interrupts::kMaskAddress, Interrupts::kTimer0));
    CHECK(system.irqPending());

    uint32_t value = 0;
    CHECK(memory.read32(Interrupts::kStatAddress, value));
    CHECK_EQ(value, Interrupts::kTimer0);
    CHECK(memory.read32(Interrupts::kMaskAddress, value));
    CHECK_EQ(value, Interrupts::kTimer0);

    CHECK(memory.write32(Interrupts::kStatAddress, 0));
    CHECK(!system.irqPending());
    CHECK(memory.read32(Interrupts::kStatAddress, value));
    CHECK_EQ(value, 0u);

    CHECK(memory.read32(0x1F801000u, value, &fault));
    CHECK_EQ(value, 0x1F000000u);
    CHECK_EQ(static_cast<unsigned>(fault), static_cast<unsigned>(MemoryFault::None));
    CHECK(memory.read32(0x1F802000u, value, &fault) == false);
    CHECK_EQ(static_cast<unsigned>(fault), static_cast<unsigned>(MemoryFault::Unsupported));
}

TEST_CASE(timers_compare_target_and_interrupt) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(0x1F801070u, 0));
    CHECK(memory.write32(0x1F801074u, Interrupts::kTimer0));
    CHECK(memory.write32(0x1F801108u, 3));
    CHECK(memory.write32(0x1F801104u, 0x18));
    system.tick(2);
    CHECK_EQ(system.timers().count(0), 2u);
    CHECK(!system.irqPending());
    system.tick(1);
    CHECK_EQ(system.timers().count(0), 0u);

    uint32_t value = 0;
    CHECK(memory.read32(0x1F801104u, value));
    CHECK((value & 0x800u) != 0);
    CHECK((value & 0x400u) == 0);
    CHECK(system.irqPending());

    CHECK(memory.write32(0x1F801070u, 0));
    CHECK(memory.write32(0x1F801104u, 0x18));
    CHECK(!system.irqPending());
    CHECK_EQ(system.timers().count(0), 0u);
}

TEST_CASE(timers_prescaler_and_overflow) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(0x1F801070u, 0));
    CHECK(memory.write32(0x1F801074u, Interrupts::kTimer2));
    CHECK(memory.write32(0x1F801124u, 0x220u));
    CHECK(memory.write32(0x1F801120u, 0xFFFFu));
    system.tick(7);
    CHECK_EQ(system.timers().count(2), 0xFFFFu);
    system.tick(1);
    CHECK_EQ(system.timers().count(2), 0u);
    CHECK(system.irqPending());

    uint32_t value = 0;
    CHECK(memory.read32(0x1F801124u, value));
    CHECK((value & 0x1000u) != 0);
}

TEST_CASE(cpu_services_timer_interrupt) {
    Memory memory;
    System system(memory);
    Cpu cpu(memory);
    test::writeProgram(memory, kProgramBase, {0x1000FFFFu, 0u});
    for (uint32_t offset = 0; offset < 0x100; offset += 4) {
        test::writeInstruction(memory, 0x80000080u + offset, 0);
    }
    test::writeInstruction(memory, 0x80000080u, 0x42000010u);
    test::writeInstruction(memory, 0x80000084u, 0x08004000u);
    test::writeInstruction(memory, 0x80000088u, 0);

    cpu.reset();
    system.reset();
    cpu.setExecutionMode(CpuExecutionMode::Hardware);
    cpu.setPc(kProgramBase);
    cpu.setCop0(12, 0x0401u);
    system.bind(cpu);

    CHECK(memory.write32(0x1F801070u, 0));
    CHECK(memory.write32(0x1F801074u, Interrupts::kTimer0));
    CHECK(memory.write32(0x1F801108u, 1));
    CHECK(memory.write32(0x1F801104u, 0x18u));

    const CpuRunResult result = cpu.run(6);
    CHECK_EQ(static_cast<unsigned>(result.reason), static_cast<unsigned>(CpuStopReason::StepLimit));
    CHECK_EQ((cpu.cause() >> 2) & 0x1Fu, static_cast<uint32_t>(CpuException::Interrupt));
    CHECK(cpu.epc() != 0u);
}

TEST_CASE(cpu_timer_callback_advances_time) {
    Memory memory;
    System system(memory);
    Cpu cpu(memory);
    test::writeProgram(memory, kProgramBase, {0, 0, kBreak});
    cpu.reset();
    system.reset();
    cpu.setPc(kProgramBase);
    system.bind(cpu);
    const CpuRunResult result = cpu.run(2);
    CHECK_EQ(static_cast<unsigned>(result.reason), static_cast<unsigned>(CpuStopReason::StepLimit));
    CHECK_EQ(system.timers().count(0), static_cast<uint16_t>(2u * kCyclesPerInstruction));
}

TEST_CASE(io_subword_access_reads_interrupt_registers) {
    Memory memory;
    System system(memory);
    system.reset();
    CHECK(memory.write32(0x1F801074u, 0x1F801074u));
    uint8_t byte = 0;
    CHECK(memory.read8(0x1F801074u, byte));
    CHECK_EQ(byte, 0x74u);
    CHECK(memory.write8(0x1F801074u, 0x11u));
    uint32_t value = 0;
    CHECK(memory.read32(0x1F801074u, value));
    CHECK_EQ(value, 0x11u);
}

TEST_CASE(gpu_fill_and_cpu_vram_transfer) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp0Address, 0x020000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00030003u));
    CHECK_EQ(system.gpu().vram(0, 0), 0x001Fu);
    CHECK_EQ(system.gpu().vram(15, 0), 0x001Fu);
    CHECK_EQ(system.gpu().vram(0, 2), 0x001Fu);
    CHECK_EQ(system.gpu().vram(15, 2), 0x001Fu);
    CHECK_EQ(system.gpu().vram(0, 3), 0x0000u);
    CHECK_EQ(system.gpu().vram(16, 0), 0x0000u);

    CHECK(memory.write32(Gpu::kGp0Address, 0xA0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00080010u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00020002u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x7C1F03E0u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00007C1Fu));
    CHECK_EQ(system.gpu().vram(16, 8), 0x03E0u);
    CHECK_EQ(system.gpu().vram(17, 8), 0x7C1Fu);
    CHECK_EQ(system.gpu().vram(18, 8), 0x0000u);
    CHECK_EQ(system.gpu().vram(16, 9), 0x7C1Fu);
    CHECK_EQ(system.gpu().vram(17, 9), 0x0000u);

    CHECK(memory.write32(Gpu::kGp0Address, 0xC0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00080010u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00020002u));
    uint32_t value = 0;
    CHECK(memory.read32(Gpu::kGp0Address, value));
    CHECK_EQ(value, 0x7C1F03E0u);
    CHECK(memory.read32(Gpu::kGp0Address, value));
    CHECK_EQ(value, 0x00007C1Fu);
    CHECK(memory.read32(Gpu::kGp0Address, value));
    CHECK_EQ(value, 0u);
}

TEST_CASE(gpu_cpu_to_vram_transfer_row_alignment) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp0Address, 0xA0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00080010u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00020003u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x23451234u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x45673456u));
    CHECK_EQ(system.gpu().vram(16, 8), 0x1234u);
    CHECK_EQ(system.gpu().vram(17, 8), 0x2345u);
    CHECK_EQ(system.gpu().vram(18, 8), 0x3456u);
    CHECK_EQ(system.gpu().vram(19, 8), 0x0000u);
    CHECK_EQ(system.gpu().vram(16, 9), 0x4567u);
    CHECK_EQ(system.gpu().vram(17, 9), 0x0000u);
}

TEST_CASE(gpu_rectangle_uses_drawing_area_and_offset) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp0Address, 0xE3000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0xE4000000u | (10u << 10) | 10u));
    CHECK(memory.write32(Gpu::kGp0Address, 0xE5000000u | (2u << 11) | 3u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x600000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00030003u));
    CHECK_EQ(system.gpu().vram(3, 2), 0x001Fu);
    CHECK_EQ(system.gpu().vram(4, 3), 0x001Fu);

    CHECK(memory.write32(Gpu::kGp0Address, 0x600000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00090009u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00030003u));
    CHECK_EQ(system.gpu().vram(12, 11), 0x0000u);
    CHECK_EQ(system.gpu().vram(13, 12), 0x0000u);
    CHECK_EQ(system.gpu().vram(14, 12), 0x0000u);
    CHECK_EQ(system.gpu().vram(20, 20), 0x0000u);
}

TEST_CASE(gpu_flat_triangle_and_status) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp1Address, 0x04000002u));
    uint32_t status = 0;
    CHECK(memory.read32(Gpu::kGp1Address, status));
    CHECK_EQ((status >> 29) & 3u, 2u);
    CHECK((status & (1u << 26)) != 0);

    CHECK(memory.write32(Gpu::kGp0Address, 0x200000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000010u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00100000u));
    CHECK_EQ(system.gpu().vram(0, 0), 0x001Fu);
    CHECK_EQ(system.gpu().vram(5, 1), 0x001Fu);
    CHECK_EQ(system.gpu().vram(15, 15), 0x0000u);
}

TEST_CASE(gpu_textured_polygon_clut_and_texpage) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | 2u));
    CHECK(memory.write32(Gpu::kGp0Address, 0xA0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00040000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00010004u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x1312u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x0u));
    CHECK(memory.write32(Gpu::kGp0Address, 0xA0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00040080u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00010002u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x66666666u));
    CHECK(memory.write32(Gpu::kGp0Address, 0xA0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00050000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00010008u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x22221111u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x44443333u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x77777777u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x77777777u));

    CHECK(memory.write32(Gpu::kGp0Address, 0x250000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x01400401u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000003u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000404u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00030000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000405u));

    CHECK_EQ(system.gpu().vram(1, 5), 0x2222u);
    CHECK_EQ(system.gpu().vram(0, 0), 0x2222u);
    CHECK_EQ(system.gpu().vram(1, 0), 0x4444u);
}

TEST_CASE(gpu_texture_window_masking) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (2u << 7)));
    CHECK(memory.write32(Gpu::kGp0Address, 0xE2000000u | 31u | (1u << 10)));
    CHECK(memory.write32(Gpu::kGp0Address, 0xA0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00010010u));
    for (uint32_t word = 0; word < 8u; ++word) {
        const uint32_t low = (0x0100u + word * 2u + 1u);
        const uint32_t high = (0x0100u + word * 2u + 2u);
        CHECK(memory.write32(Gpu::kGp0Address, (high << 16) | low));
    }

    CHECK(memory.write32(Gpu::kGp0Address, 0x65000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00100010u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00100010u));
    CHECK_EQ(system.gpu().vram(16, 16), 0x0109u);
    CHECK_EQ(system.gpu().vram(23, 16), 0x0110u);
    CHECK_EQ(system.gpu().vram(24, 16), 0x0109u);
    CHECK_EQ(system.gpu().vram(16, 17), 0x0000u);
}

TEST_CASE(gpu_textured_rect_clut4_texel_packing) {
    Memory memory;
    System system(memory);
    system.reset();
    Gpu& gpu = system.gpu();

    for (uint32_t index = 0; index < 16u; ++index) {
        gpu.setVram(16u + index, 6u, static_cast<uint16_t>(0x0100u + index));
    }
    for (uint32_t row = 0; row < 4u; ++row) {
        for (uint32_t column = 0; column < 8u; ++column) {
            uint16_t word = 0;
            for (uint32_t texel = 0; texel < 4u; ++texel) {
                word = static_cast<uint16_t>(
                    word | ((((column * 4u + texel) & 0xFu) & 0xFu) << (texel * 4u)));
            }
            gpu.setVram(column, row, word);
        }
    }

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x65000000u));
    CHECK(memory.write32(Gpu::kGp0Address, (100u << 16) | 100u));
    CHECK(memory.write32(Gpu::kGp0Address, ((6u << 6) | 1u) << 16));
    CHECK(memory.write32(Gpu::kGp0Address, (4u << 16) | 8u));

    for (uint32_t row = 0; row < 4u; ++row) {
        for (uint32_t column = 0; column < 8u; ++column) {
            CHECK_EQ(gpu.vram(100u + column, 100u + row), static_cast<uint16_t>(0x0100u + column));
        }
    }
    CHECK_EQ(gpu.vram(100u, 99u), 0x0000u);
    CHECK_EQ(gpu.vram(108u, 100u), 0x0000u);
}

TEST_CASE(gpu_textured_rect_clut8_byte_packing) {
    Memory memory;
    System system(memory);
    system.reset();
    Gpu& gpu = system.gpu();

    for (uint32_t index = 0; index < 256u; ++index) {
        gpu.setVram(32u + index, 6u, static_cast<uint16_t>(0x0200u + index));
    }
    for (uint32_t row = 0; row < 4u; ++row) {
        for (uint32_t column = 0; column < 8u; ++column) {
            const uint16_t low = static_cast<uint16_t>(column * 2u + row * 8u);
            const uint16_t high = static_cast<uint16_t>(column * 2u + 1u + row * 8u);
            gpu.setVram(column, row, static_cast<uint16_t>(low | (high << 8)));
        }
    }

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (1u << 7)));
    CHECK(memory.write32(Gpu::kGp0Address, 0x65000000u));
    CHECK(memory.write32(Gpu::kGp0Address, (100u << 16) | 100u));
    CHECK(memory.write32(Gpu::kGp0Address, ((6u << 6) | 2u) << 16));
    CHECK(memory.write32(Gpu::kGp0Address, (4u << 16) | 8u));

    for (uint32_t row = 0; row < 4u; ++row) {
        for (uint32_t column = 0; column < 8u; ++column) {
            const uint16_t expected =
                static_cast<uint16_t>(0x0200u + column + row * 8u);
            CHECK_EQ(gpu.vram(100u + column, 100u + row), expected);
        }
    }
    CHECK_EQ(gpu.vram(108u, 100u), 0x0000u);
}

TEST_CASE(gpu_textured_rect_texture_window_with_clut4) {
    Memory memory;
    System system(memory);
    system.reset();
    Gpu& gpu = system.gpu();

    for (uint32_t index = 0; index < 16u; ++index) {
        gpu.setVram(16u + index, 6u, static_cast<uint16_t>(0x0100u + index));
    }
    for (uint32_t row = 0; row < 8u; ++row) {
        for (uint32_t column = 0; column < 8u; ++column) {
            uint16_t word = 0;
            for (uint32_t texel = 0; texel < 4u; ++texel) {
                const uint16_t palette =
                    static_cast<uint16_t>((column * 4u + texel + row * 2u) & 0xFu);
                word = static_cast<uint16_t>(word | (palette << (texel * 4u)));
            }
            gpu.setVram(column, row, word);
        }
    }
    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u));
    CHECK(memory.write32(Gpu::kGp0Address,
                         0xE2000000u | 1u | (31u << 5) | (1u << 10)));
    CHECK(memory.write32(Gpu::kGp0Address, 0x65000000u));
    CHECK(memory.write32(Gpu::kGp0Address, (100u << 16) | 100u));
    CHECK(memory.write32(Gpu::kGp0Address, ((6u << 6) | 1u) << 16));
    CHECK(memory.write32(Gpu::kGp0Address, (12u << 16) | 8u));

    for (uint32_t row = 0; row < 12u; ++row) {
        for (uint32_t column = 0; column < 8u; ++column) {
            const uint16_t sourceRow = row & 7u;
            const uint32_t sourceTexel = (column | 8u) + sourceRow * 2u;
            const uint16_t expected =
                static_cast<uint16_t>(0x0100u + (sourceTexel & 0xFu));
            CHECK_EQ(gpu.vram(100u + column, 100u + row), expected);
        }
    }
    CHECK_EQ(gpu.vram(100u, 112u), 0x0000u);
}

TEST_CASE(gpu_modulation_texel_color) {
    Memory memory;
    System system(memory);
    system.reset();
    Gpu& gpu = system.gpu();

    gpu.setVram(16u, 6u, 0x7FFFu);

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x64404040u));
    CHECK(memory.write32(Gpu::kGp0Address, (100u << 16) | 100u));
    CHECK(memory.write32(Gpu::kGp0Address, ((6u << 6) | 1u) << 16));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));
    CHECK_EQ(gpu.vram(100u, 100u), 0x3DEFu);

    CHECK(memory.write32(Gpu::kGp0Address, 0x6520A040u));
    CHECK(memory.write32(Gpu::kGp0Address, 110u | (100u << 16)));
    CHECK(memory.write32(Gpu::kGp0Address, ((6u << 6) | 1u) << 16));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));
    CHECK_EQ(gpu.vram(110u, 100u), 0x7FFFu);
}

TEST_CASE(gpu_semi_transparency_blend_modes) {
    Memory memory;
    System system(memory);
    system.reset();
    Gpu& gpu = system.gpu();

    gpu.setVram(100u, 100u, 0x7C1Fu);
    CHECK(memory.write32(Gpu::kGp0Address, 0x620000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 100u | (100u << 16)));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));
    CHECK_EQ(gpu.vram(100u, 100u), 0x3C1Fu);

    gpu.setVram(101u, 100u, 0x7C1Fu);
    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (1u << 5)));
    CHECK(memory.write32(Gpu::kGp0Address, 0x62004000u));
    CHECK(memory.write32(Gpu::kGp0Address, 101u | (100u << 16)));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));
    CHECK_EQ(gpu.vram(101u, 100u), 0x7D1Fu);

    gpu.setVram(102u, 100u, 0x7C1Fu);
    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (2u << 5)));
    CHECK(memory.write32(Gpu::kGp0Address, 0x620000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 102u | (100u << 16)));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));
    CHECK_EQ(gpu.vram(102u, 100u), 0x7C00u);

    gpu.setVram(103u, 100u, 0x01E0u);
    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (3u << 5)));
    CHECK(memory.write32(Gpu::kGp0Address, 0x62FFFFFFu));
    CHECK(memory.write32(Gpu::kGp0Address, 103u | (100u << 16)));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));
    CHECK_EQ(gpu.vram(103u, 100u), 0x1EC7u);

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u));
    gpu.setVram(104u, 100u, 0x7C1Fu);
    CHECK(memory.write32(Gpu::kGp0Address, 0x600000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 104u | (100u << 16)));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));
    CHECK_EQ(gpu.vram(104u, 100u), 0x001Fu);
}

TEST_CASE(gpu_semi_transparency_clut_stp_flag) {
    Memory memory;
    System system(memory);
    system.reset();
    Gpu& gpu = system.gpu();

    gpu.setVram(16u + 0u, 6u, 0x801Fu);
    gpu.setVram(16u + 1u, 6u, 0x001Fu);
    gpu.setVram(16u + 2u, 6u, 0x8000u);
    gpu.setVram(0u, 0u, 0x0001u);
    gpu.setVram(1u, 0u, 0x0002u);

    gpu.setVram(100u, 100u, 0x7C1Fu);
    gpu.setVram(101u, 100u, 0x7C1Fu);

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x67000000u));
    CHECK(memory.write32(Gpu::kGp0Address, (100u << 16) | 100u));
    CHECK(memory.write32(Gpu::kGp0Address, ((6u << 6) | 1u) << 16));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 2u));

    CHECK_EQ(gpu.vram(100u, 100u), 0x001Fu);
    CHECK_EQ(gpu.vram(101u, 100u), 0xBC1Fu);

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (1u << 12)));
    CHECK(memory.write32(Gpu::kGp0Address, 0x65000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 200u | (100u << 16)));
    CHECK(memory.write32(Gpu::kGp0Address, ((6u << 6) | 1u) << 16 | 4u));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 1u));

    CHECK_EQ(gpu.vram(200u, 100u), 0x8000u);
}

TEST_CASE(gpu_textured_rect_x_flip) {
    Memory memory;
    System system(memory);
    system.reset();
    Gpu& gpu = system.gpu();

    for (uint32_t index = 0; index < 16u; ++index) {
        gpu.setVram(16u + index, 6u, static_cast<uint16_t>(0x0100u + index));
    }
    gpu.setVram(0u, 0u, 0x3210u);
    gpu.setVram(1u, 0u, 0x7654u);
    gpu.setVram(2u, 0u, 0xBA98u);
    gpu.setVram(3u, 0u, 0xFEDCu);

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (1u << 12)));
    CHECK(memory.write32(Gpu::kGp0Address, 0x65000000u));
    CHECK(memory.write32(Gpu::kGp0Address, (100u << 16) | 100u));
    CHECK(memory.write32(Gpu::kGp0Address, 7u | ((6u << 6) | 1u) << 16));
    CHECK(memory.write32(Gpu::kGp0Address, (1u << 16) | 8u));

    CHECK_EQ(gpu.vram(100u, 100u), 0x0107u);
    CHECK_EQ(gpu.vram(101u, 100u), 0x0106u);
    CHECK_EQ(gpu.vram(106u, 100u), 0x0101u);
    CHECK_EQ(gpu.vram(107u, 100u), 0x0100u);
}

TEST_CASE(gpu_gouraud_shaded_triangle) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp0Address, 0x300000FFu));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000008u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00080000u));

    CHECK_EQ(system.gpu().vram(0u, 0u), 0x001Fu);
    CHECK_EQ(system.gpu().vram(4u, 0u), 0x000Fu);
    CHECK_EQ(system.gpu().vram(8u, 0u), 0x0000u);
    CHECK_EQ(system.gpu().vram(0u, 8u), 0x0000u);
    CHECK_EQ(system.gpu().vram(4u, 1u), 0x000Bu);
}

TEST_CASE(gpu_gp1_info_and_display_control) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp0Address, 0xE1000000u | (1u << 7) | 3u));
    CHECK(memory.write32(Gpu::kGp1Address, 0x10000000u));
    uint32_t value = 0;
    CHECK(memory.read32(Gpu::kGp0Address, value));
    CHECK_EQ(value, 0x83u);

    CHECK(memory.write32(Gpu::kGp1Address, 0x03000001u));
    CHECK(memory.read32(Gpu::kGp1Address, value));
    CHECK((value & (1u << 23)) != 0);
}

TEST_CASE(gpu_display_area_registers) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp1Address, 0x05000000u | (100u << 10) | 40u));
    CHECK_EQ(system.gpu().displayStartX(), 40u);
    CHECK_EQ(system.gpu().displayStartY(), 100u);

    CHECK(memory.write32(Gpu::kGp1Address, 0x08000000u | 0x09u));
    CHECK_EQ(system.gpu().displayWidth(), 320u);
    CHECK_EQ(system.gpu().displayHeight(), 240u);
    CHECK_EQ(system.gpu().displayMode(), 0x09u);

    CHECK(memory.write32(Gpu::kGp1Address, 0x08000000u | 0x0Au));
    CHECK_EQ(system.gpu().displayWidth(), 512u);
    CHECK_EQ(system.gpu().displayHeight(), 240u);

    CHECK(memory.write32(Gpu::kGp1Address, 0x08000000u | 0x40u));
    CHECK_EQ(system.gpu().displayWidth(), 368u);

    CHECK(memory.write32(Gpu::kGp1Address, 0x08000000u | 0x05u));
    CHECK_EQ(system.gpu().displayWidth(), 320u);
    CHECK_EQ(system.gpu().displayHeight(), 480u);

    CHECK(memory.write32(Gpu::kGp1Address, 0x03000001u));
    CHECK(system.gpu().displayDisabled());
    CHECK(memory.write32(Gpu::kGp1Address, 0x03000000u));
    CHECK(!system.gpu().displayDisabled());
}

TEST_CASE(dma_otc_builds_reverse_chain) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Dma::kDpcrAddress, 0x0F000000u));
    CHECK(memory.write32(Dma::kBaseAddress + 6u * 0x10u, 0x80001000u));
    CHECK(memory.write32(Dma::kBaseAddress + 6u * 0x10u + 4u, 4));
    CHECK(memory.write32(Dma::kBaseAddress + 6u * 0x10u + 8u, 0x11000002u));
    CHECK(!system.dma().busy(6));

    uint32_t value = 0;
    CHECK(memory.read32(0x80001000u, value));
    CHECK_EQ(value, 0x00000FFCu);
    CHECK(memory.read32(0x80000FFCu, value));
    CHECK_EQ(value, 0x00000FF8u);
    CHECK(memory.read32(0x80000FF8u, value));
    CHECK_EQ(value, 0x00000FF4u);
    CHECK(memory.read32(0x80000FF4u, value));
    CHECK_EQ(value, 0x00FFFFFFu);
}

TEST_CASE(dma_gpu_linked_list_renders) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Gpu::kGp1Address, 0x04000002u));
    CHECK(memory.write32(Dma::kDpcrAddress, 0x00000800u));
    CHECK(memory.write32(0x80002000u, 0x03FFFFFFu));
    CHECK(memory.write32(0x80002004u, 0x020000FFu));
    CHECK(memory.write32(0x80002008u, 0x00000000u));
    CHECK(memory.write32(0x8000200Cu, 0x00020002u));
    CHECK(memory.write32(Dma::kBaseAddress + 2u * 0x10u, 0x80002000u));
    CHECK(memory.write32(Dma::kBaseAddress + 2u * 0x10u + 4u, 0));
    CHECK(memory.write32(Dma::kBaseAddress + 2u * 0x10u + 8u, 0x01000401u));

    CHECK(!system.dma().busy(2));
    CHECK_EQ(system.gpu().vram(0, 0), 0x001Fu);
    CHECK_EQ(system.gpu().vram(15, 1), 0x001Fu);
    CHECK_EQ(system.gpu().vram(0, 2), 0x0000u);
}

TEST_CASE(dma_completion_requests_irq3) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Dma::kDpcrAddress, 0x0F000000u));
    CHECK(memory.write32(Dma::kDicrAddress, (1u << 23) | (1u << 22) | (1u << 6)));
    system.interrupts().clear(Interrupts::kDma);
    CHECK(memory.write32(Dma::kBaseAddress + 6u * 0x10u, 0x80001000u));
    CHECK(memory.write32(Dma::kBaseAddress + 6u * 0x10u + 4u, 1));
    CHECK(memory.write32(Dma::kBaseAddress + 6u * 0x10u + 8u, 0x11000002u));

    uint32_t value = 0;
    CHECK(memory.read32(Dma::kDicrAddress, value));
    CHECK((value & (1u << 31)) != 0);
    CHECK((value & (1u << 30)) != 0);
    CHECK(memory.read32(Interrupts::kStatAddress, value));
    CHECK((value & Interrupts::kDma) != 0);

    CHECK(memory.write32(Dma::kDicrAddress, 1u << 30));
    CHECK(memory.read32(Dma::kDicrAddress, value));
    CHECK((value & (1u << 30)) == 0);
    CHECK((value & (1u << 31)) == 0);
}

TEST_CASE(dma_unsupported_channel_reports_error) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Dma::kDpcrAddress, 0x0000000Fu));
    CHECK(memory.write32(Dma::kBaseAddress + 4u, 1));
    CHECK(memory.write32(Dma::kBaseAddress + 8u, 0x01000201u));
    CHECK(!system.dma().busy(0));

    uint32_t value = 0;
    CHECK(memory.read32(Dma::kDicrAddress, value));
    CHECK((value & (1u << 15)) != 0);
    CHECK((value & (1u << 31)) != 0);
    CHECK(memory.read32(Interrupts::kStatAddress, value));
    CHECK((value & Interrupts::kDma) != 0);
}

TEST_CASE(spu_voice_registers_key_on_and_off) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(Spu::kBaseAddress, 0x12345678u));
    CHECK(memory.write32(Spu::kBaseAddress + 4u, 0x00001000u));
    uint32_t value = 0;
    CHECK(memory.read32(Spu::kBaseAddress, value));
    CHECK_EQ(value, 0x12345678u);
    CHECK(memory.read32(Spu::kBaseAddress + 4u, value));
    CHECK_EQ(value, 0x00001000u);

    CHECK(memory.write32(Spu::kBaseAddress + 0x188u, 1u));
    CHECK(system.spu().voiceKeyedOn(0));
    CHECK_EQ(system.spu().currentEnvelope(0), 0u);
    CHECK(memory.write32(Spu::kBaseAddress + 0x18Cu, 1u));
    CHECK(!system.spu().voiceKeyedOn(0));
}

TEST_CASE(spu_manual_transfer_and_adpcm_decode) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write16(Spu::kBaseAddress + 0x1AAu, 0x10u));
    CHECK(memory.write16(Spu::kBaseAddress + 0x1A6u, 0));
    CHECK(memory.write32(Spu::kBaseAddress + 0x1A8u, 0x1234u));
    CHECK_EQ(system.spu().ram(0), 0x34u);
    CHECK_EQ(system.spu().ram(1), 0x12u);

    uint8_t block[16] = {};
    block[0] = 0x0C;
    for (unsigned index = 2; index < 16; ++index) {
        block[index] = 0x11;
    }
    int16_t history1 = 0;
    int16_t history2 = 0;
    const auto samples = Spu::decodeBlock(block, history1, history2);
    for (int16_t sample : samples) {
        CHECK_EQ(sample, 1);
    }
    CHECK_EQ(history1, 1);
    CHECK_EQ(history2, 1);
}

TEST_CASE(spu_dma_write_and_read) {
    Memory memory;
    System system(memory);
    system.reset();

    CHECK(memory.write32(0x80003000u, 0x11223344u));
    CHECK(memory.write32(0x80003004u, 0x55667788u));
    CHECK(memory.write16(Spu::kBaseAddress + 0x1AAu, 0x20u));
    CHECK(memory.write16(Spu::kBaseAddress + 0x1A6u, 0x10u));
    CHECK(memory.write32(Dma::kDpcrAddress, 0x00080000u));
    CHECK(memory.write32(Dma::kBaseAddress + 4u * 0x10u, 0x80003000u));
    CHECK(memory.write32(Dma::kBaseAddress + 4u * 0x10u + 4u, 0x00010002u));
    CHECK(memory.write32(Dma::kBaseAddress + 4u * 0x10u + 8u, 0x01000201u));
    CHECK(!system.dma().busy(4));
    CHECK_EQ(system.spu().ram(0x80u), 0x44u);
    CHECK_EQ(system.spu().ram(0x81u), 0x33u);
    CHECK_EQ(system.spu().ram(0x84u), 0x88u);
    CHECK_EQ(system.spu().ram(0x87u), 0x55u);

    CHECK(memory.write16(Spu::kBaseAddress + 0x1AAu, 0x30u));
    CHECK(memory.write16(Spu::kBaseAddress + 0x1A6u, 0x10u));
    CHECK(memory.write32(Dma::kBaseAddress + 4u * 0x10u, 0x80003010u));
    CHECK(memory.write32(Dma::kBaseAddress + 4u * 0x10u + 4u, 0x00010002u));
    CHECK(memory.write32(Dma::kBaseAddress + 4u * 0x10u + 8u, 0x01000200u));
    uint32_t value = 0;
    CHECK(memory.read32(0x80003010u, value));
    CHECK_EQ(value, 0x11223344u);
    CHECK(memory.read32(0x80003014u, value));
    CHECK_EQ(value, 0x55667788u);
}
