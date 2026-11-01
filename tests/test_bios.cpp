#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "psxreborn/bios.hpp"
#include "psxreborn/cpu.hpp"
#include "psxreborn/hardware.hpp"
#include "psxreborn/memory.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace {

constexpr uint32_t kReturnAddress = 0x80010000u;
constexpr uint8_t kA0 = 4;
constexpr uint8_t kA1 = 5;
constexpr uint8_t kA2 = 6;
constexpr uint8_t kA3 = 7;
constexpr uint8_t kT1 = 9;
constexpr uint8_t kRa = 31;
constexpr uint8_t kV0 = 2;

void writeCString(Memory& memory, uint32_t address, const std::string& text) {
    for (size_t index = 0; index < text.size(); ++index) {
        memory.write8(address + static_cast<uint32_t>(index), static_cast<uint8_t>(text[index]));
    }
    memory.write8(address + static_cast<uint32_t>(text.size()), 0);
}

void callA0(Cpu& cpu, uint8_t function, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0) {
    cpu.setPc(0xA0u);
    cpu.setReg(kT1, function);
    cpu.setReg(kA0, a0);
    cpu.setReg(kA1, a1);
    cpu.setReg(kA2, a2);
    cpu.setReg(kRa, kReturnAddress);
    const CpuRunResult result = cpu.run(1);
    CHECK_EQ(static_cast<unsigned>(result.reason), static_cast<unsigned>(CpuStopReason::StepLimit));
    CHECK_EQ(result.steps, 1u);
    CHECK_EQ(cpu.pc(), kReturnAddress);
}

void callB0(Cpu& cpu, uint8_t function, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0,
            uint32_t a3 = 0) {
    cpu.setPc(0xB0u);
    cpu.setReg(kT1, function);
    cpu.setReg(kA0, a0);
    cpu.setReg(kA1, a1);
    cpu.setReg(kA2, a2);
    cpu.setReg(kA3, a3);
    cpu.setReg(kRa, kReturnAddress);
    const CpuRunResult result = cpu.run(1);
    CHECK_EQ(static_cast<unsigned>(result.reason), static_cast<unsigned>(CpuStopReason::StepLimit));
    CHECK_EQ(result.steps, 1u);
    CHECK_EQ(cpu.pc(), kReturnAddress);
}

void callC0(Cpu& cpu, uint8_t function, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0,
            uint32_t a3 = 0) {
    cpu.setPc(0xC0u);
    cpu.setReg(kT1, function);
    cpu.setReg(kA0, a0);
    cpu.setReg(kA1, a1);
    cpu.setReg(kA2, a2);
    cpu.setReg(kA3, a3);
    cpu.setReg(kRa, kReturnAddress);
    const CpuRunResult result = cpu.run(1);
    CHECK_EQ(static_cast<unsigned>(result.reason), static_cast<unsigned>(CpuStopReason::StepLimit));
    CHECK_EQ(result.steps, 1u);
    CHECK_EQ(cpu.pc(), kReturnAddress);
}

struct CardFixture {
    Memory memory;
    Cpu cpu{memory};
    System system{memory};
    Bios bios{cpu, memory, system};
    MemoryCard card = MemoryCard::formatted();

    CardFixture() {
        system.setMemoryCard(0, &card);
        system.reset();
        system.bind(cpu);
        bios.reset();
        bios.install();
        cpu.setExecutionMode(CpuExecutionMode::Hardware);
    }
};

}  // namespace

TEST_CASE(bios_a0_strstr) {
    Memory memory;
    Cpu cpu(memory);
    System system(memory);
    Bios bios(cpu, memory, system);
    system.reset();
    system.bind(cpu);
    bios.reset();
    bios.install();
    cpu.setExecutionMode(CpuExecutionMode::Hardware);

    writeCString(memory, 0x80020000u, "abcXYZdef");
    writeCString(memory, 0x80020100u, "XYZ");
    callA0(cpu, 0x24u, 0x80020000u, 0x80020100u);
    CHECK_EQ(cpu.reg(kV0), 0x80020003u);

    writeCString(memory, 0x80020100u, "QQQ");
    callA0(cpu, 0x24u, 0x80020000u, 0x80020100u);
    CHECK_EQ(cpu.reg(kV0), 0u);

    writeCString(memory, 0x80020100u, "");
    callA0(cpu, 0x24u, 0x80020000u, 0x80020100u);
    CHECK_EQ(cpu.reg(kV0), 0x80020000u);
}

TEST_CASE(bios_a0_atoi_and_atol) {
    Memory memory;
    Cpu cpu(memory);
    System system(memory);
    Bios bios(cpu, memory, system);
    system.reset();
    system.bind(cpu);
    bios.reset();
    bios.install();
    cpu.setExecutionMode(CpuExecutionMode::Hardware);

    writeCString(memory, 0x80020200u, "  -1234xyz");
    callA0(cpu, 0x11u, 0x80020200u);
    CHECK_EQ(cpu.reg(kV0), static_cast<uint32_t>(-1234));

    writeCString(memory, 0x80020200u, "42");
    callA0(cpu, 0x10u, 0x80020200u);
    CHECK_EQ(cpu.reg(kV0), 42u);
}

TEST_CASE(bios_a0_abs_and_labs) {
    Memory memory;
    Cpu cpu(memory);
    System system(memory);
    Bios bios(cpu, memory, system);
    system.reset();
    system.bind(cpu);
    bios.reset();
    bios.install();
    cpu.setExecutionMode(CpuExecutionMode::Hardware);

    callA0(cpu, 0x0Eu, static_cast<uint32_t>(-7));
    CHECK_EQ(cpu.reg(kV0), 7u);

    callA0(cpu, 0x0Fu, static_cast<uint32_t>(-9));
    CHECK_EQ(cpu.reg(kV0), 9u);
}

TEST_CASE(bios_card_generic_open_read_write_close) {
    CardFixture fixture;
    std::string error;
    CHECK(fixture.card.createFile("BESLES-04064SAVE", 1, error) >= 1);
    const std::vector<uint8_t> initial = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(fixture.card.writeFile("BESLES-04064SAVE", 0, initial.data(), initial.size()));
    writeCString(fixture.memory, 0x80020000u, "bu00:BESLES-04064SAVE");
    callA0(fixture.cpu, 0x00u, 0x80020000u, 3u);
    const uint32_t fd = fixture.cpu.reg(kV0);
    CHECK(fd != 0xFFFFFFFFu);
    callA0(fixture.cpu, 0x02u, fd, 0x80020100u, 8u);
    CHECK_EQ(fixture.cpu.reg(kV0), 8u);
    for (size_t index = 0; index < initial.size(); ++index) {
        uint8_t value = 0;
        CHECK(fixture.memory.read8(0x80020100u + static_cast<uint32_t>(index), value));
        CHECK_EQ(value, initial[index]);
    }
    const std::vector<uint8_t> replacement = {9, 10, 11, 12};
    for (size_t index = 0; index < replacement.size(); ++index) {
        fixture.memory.write8(0x80020200u + static_cast<uint32_t>(index), replacement[index]);
    }
    callA0(fixture.cpu, 0x01u, fd, 0u, 0u);
    callA0(fixture.cpu, 0x03u, fd, 0x80020200u, static_cast<uint32_t>(replacement.size()));
    CHECK_EQ(fixture.cpu.reg(kV0), static_cast<uint32_t>(replacement.size()));
    callA0(fixture.cpu, 0x04u, fd);
    std::vector<uint8_t> stored(8, 0);
    CHECK(fixture.card.readFile("BESLES-04064SAVE", 0, stored.data(), stored.size()));
    for (size_t index = 0; index < replacement.size(); ++index) {
        CHECK_EQ(stored[index], replacement[index]);
    }
}

TEST_CASE(bios_card_fcb_api_and_directory_listing) {
    CardFixture fixture;
    std::string error;
    CHECK(fixture.card.createFile("BESLES-04064A", 1, error) >= 1);
    CHECK(fixture.card.createFile("BESLES-04064B", 1, error) >= 1);
    writeCString(fixture.memory, 0x80020000u, "bu00:BESLES-04064C");
    callA0(fixture.cpu, 0x65u, 0x80021000u, 0x80020000u, 0x00020203u);
    CHECK_EQ(fixture.cpu.reg(kV0), 1u);
    const std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC};
    for (size_t index = 0; index < data.size(); ++index) {
        fixture.memory.write8(0x80020200u + static_cast<uint32_t>(index), data[index]);
    }
    callA0(fixture.cpu, 0x67u, 0x80021000u, 0x80020200u,
           static_cast<uint32_t>(data.size()));
    CHECK_EQ(fixture.cpu.reg(kV0), static_cast<uint32_t>(data.size()));
    callA0(fixture.cpu, 0x68u, 0x80021000u);
    CHECK_EQ(fixture.cpu.reg(kV0), 0u);
    std::vector<uint8_t> stored(data.size(), 0);
    CHECK(fixture.card.readFile("BESLES-04064C", 0, stored.data(), stored.size()));
    CHECK(stored == data);

    writeCString(fixture.memory, 0x80020300u, "bu00:BESLES-04064*");
    callB0(fixture.cpu, 0x42u, 0x80020300u, 0x80021400u);
    CHECK(fixture.cpu.reg(kV0) == 0x80021400u);
    uint8_t firstByte = 0;
    uint32_t attribute = 0;
    uint32_t size = 0;
    CHECK(fixture.memory.read8(0x80021400u, firstByte));
    CHECK_EQ(firstByte, static_cast<uint8_t>('B'));
    CHECK(fixture.memory.read32(0x80021414u, attribute));
    CHECK_EQ(attribute, 0x50u);
    CHECK(fixture.memory.read32(0x80021418u, size));
    CHECK(size >= 8192u);
    callB0(fixture.cpu, 0x43u, 0x80021440u);
    CHECK(fixture.cpu.reg(kV0) == 0x80021440u);
    callB0(fixture.cpu, 0x43u, 0x80021480u);
    CHECK(fixture.cpu.reg(kV0) == 0x80021480u);
    callB0(fixture.cpu, 0x43u, 0x800214C0u);
    CHECK_EQ(fixture.cpu.reg(kV0), 0u);
}

TEST_CASE(bios_card_raw_sectors_and_delete) {
    CardFixture fixture;
    std::string error;
    CHECK(fixture.card.createFile("BESLES-04064SAVE", 1, error) >= 1);
    for (uint32_t index = 0; index < 128; ++index) {
        fixture.memory.write8(0x80020000u + index, static_cast<uint8_t>(index ^ 0x5A));
    }
    callB0(fixture.cpu, 0x4Eu, 0u, 64u, 0x80020000u);
    CHECK_EQ(fixture.cpu.reg(kV0), 1u);
    std::array<uint8_t, 128> sector{};
    CHECK(fixture.card.readSector(64, sector.data(), sector.size()));
    for (uint32_t index = 0; index < 128; ++index) {
        CHECK_EQ(sector[index], static_cast<uint8_t>(index ^ 0x5A));
    }
    for (uint32_t index = 0; index < 128; ++index) {
        fixture.memory.write8(0x80020000u + index, 0);
    }
    callB0(fixture.cpu, 0x4Fu, 0u, 64u, 0x80020000u);
    CHECK_EQ(fixture.cpu.reg(kV0), 1u);
    uint8_t readBack = 0;
    CHECK(fixture.memory.read8(0x80020000u, readBack));
    CHECK_EQ(readBack, static_cast<uint8_t>(0 ^ 0x5A));

    writeCString(fixture.memory, 0x80020300u, "bu00:BESLES-04064SAVE");
    callB0(fixture.cpu, 0x45u, 0x80020300u);
    CHECK_EQ(fixture.cpu.reg(kV0), 1u);
    CHECK(fixture.card.findFile("BESLES-04064SAVE") == std::nullopt);
    callB0(fixture.cpu, 0x46u, 0x80020300u);
    CHECK_EQ(fixture.cpu.reg(kV0), 1u);
    CHECK(fixture.card.findFile("BESLES-04064SAVE").has_value());
}

TEST_CASE(bios_change_clear_flags_return_previous_value) {
    Memory memory;
    Cpu cpu(memory);
    System system(memory);
    Bios bios(cpu, memory, system);
    system.reset();
    system.bind(cpu);
    bios.reset();
    bios.install();
    cpu.setExecutionMode(CpuExecutionMode::Hardware);

    callB0(cpu, 0x5Bu, 1u);
    CHECK_EQ(cpu.reg(kV0), 0u);
    callB0(cpu, 0x5Bu, 0u);
    CHECK_EQ(cpu.reg(kV0), 1u);
    callB0(cpu, 0x5Bu, 0u);
    CHECK_EQ(cpu.reg(kV0), 0u);

    callC0(cpu, 0x0Au, 3u, 1u);
    CHECK_EQ(cpu.reg(kV0), 0u);
    callC0(cpu, 0x0Au, 3u, 0u);
    CHECK_EQ(cpu.reg(kV0), 1u);
    callC0(cpu, 0x0Au, 4u, 1u);
    CHECK_EQ(cpu.reg(kV0), 0u);
}
