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
    CHECK_EQ(system.timers().count(0), 2u);
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
    CHECK_EQ(system.gpu().vram(3, 3), 0x001Fu);
    CHECK_EQ(system.gpu().vram(4, 4), 0x0000u);

    CHECK(memory.write32(Gpu::kGp0Address, 0xA0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00080010u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00010001u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x7C1F03E0u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00007C1Fu));
    CHECK_EQ(system.gpu().vram(16, 8), 0x03E0u);
    CHECK_EQ(system.gpu().vram(17, 8), 0x7C1Fu);
    CHECK_EQ(system.gpu().vram(16, 9), 0x7C1Fu);
    CHECK_EQ(system.gpu().vram(17, 9), 0x0000u);

    CHECK(memory.write32(Gpu::kGp0Address, 0xC0000000u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00080010u));
    CHECK(memory.write32(Gpu::kGp0Address, 0x00010001u));
    uint32_t value = 0;
    CHECK(memory.read32(Gpu::kGp0Address, value));
    CHECK_EQ(value, 0x7C1F03E0u);
    CHECK(memory.read32(Gpu::kGp0Address, value));
    CHECK_EQ(value, 0x00007C1Fu);
    CHECK(memory.read32(Gpu::kGp0Address, value));
    CHECK_EQ(value, 0u);
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
    CHECK(memory.write32(0x8000200Cu, 0x00010001u));
    CHECK(memory.write32(Dma::kBaseAddress + 2u * 0x10u, 0x80002000u));
    CHECK(memory.write32(Dma::kBaseAddress + 2u * 0x10u + 4u, 0));
    CHECK(memory.write32(Dma::kBaseAddress + 2u * 0x10u + 8u, 0x01000401u));

    CHECK(!system.dma().busy(2));
    CHECK_EQ(system.gpu().vram(0, 0), 0x001Fu);
    CHECK_EQ(system.gpu().vram(1, 1), 0x001Fu);
    CHECK_EQ(system.gpu().vram(2, 2), 0x0000u);
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
