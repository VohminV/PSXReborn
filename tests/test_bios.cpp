#include <cstdint>
#include <string>

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
constexpr uint8_t kT1 = 9;
constexpr uint8_t kRa = 31;
constexpr uint8_t kV0 = 2;

void writeCString(Memory& memory, uint32_t address, const std::string& text) {
    for (size_t index = 0; index < text.size(); ++index) {
        memory.write8(address + static_cast<uint32_t>(index), static_cast<uint8_t>(text[index]));
    }
    memory.write8(address + static_cast<uint32_t>(text.size()), 0);
}

void callA0(Cpu& cpu, uint8_t function, uint32_t a0, uint32_t a1 = 0) {
    cpu.setPc(0xA0u);
    cpu.setReg(kT1, function);
    cpu.setReg(kA0, a0);
    cpu.setReg(kA1, a1);
    cpu.setReg(kRa, kReturnAddress);
    const CpuRunResult result = cpu.run(1);
    CHECK_EQ(static_cast<unsigned>(result.reason), static_cast<unsigned>(CpuStopReason::StepLimit));
    CHECK_EQ(result.steps, 1u);
    CHECK_EQ(cpu.pc(), kReturnAddress);
}

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
