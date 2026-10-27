#include "psxreborn/memory.hpp"

#include "test_framework.hpp"
#include "test_helpers.hpp"

using psxreborn::Memory;
using psxreborn::MemoryFault;

TEST_CASE(memory_ram_aliases_and_little_endian) {
    Memory memory;
    CHECK(memory.write32(0x00000010, 0x12345678));
    CHECK_EQ(test::readWord(memory, 0x80000010), 0x12345678u);
    CHECK_EQ(test::readWord(memory, 0xA0000010), 0x12345678u);

    uint16_t half = 0;
    CHECK(memory.read16(0x80000010, half));
    CHECK_EQ(half, 0x5678u);

    uint8_t byte = 0;
    CHECK(memory.read8(0xA0000011, byte));
    CHECK_EQ(byte, 0x56u);
}

TEST_CASE(memory_ram_mirror_covers_first_eight_megabytes) {
    Memory memory;
    CHECK(memory.write32(0x00000020, 0xCAFEBABEu));
    CHECK_EQ(test::readWord(memory, 0x00200020), 0xCAFEBABEu);
    CHECK_EQ(test::readWord(memory, 0x00400020), 0xCAFEBABEu);
    CHECK_EQ(test::readWord(memory, 0x00600020), 0xCAFEBABEu);

    CHECK(memory.write32(0x007FFFFC, 0xDEADBEEFu));
    CHECK_EQ(test::readWord(memory, 0x001FFFFC), 0xDEADBEEFu);
    CHECK_EQ(test::readWord(memory, 0x801FFFFC), 0xDEADBEEFu);
}

TEST_CASE(memory_scratchpad_mapping_and_execution) {
    Memory memory;
    CHECK(memory.write32(Memory::kScratchpadBase, 0x11223344u));
    CHECK_EQ(test::readWord(memory, 0x9F800000), 0x11223344u);
    CHECK(!memory.isExecutable(Memory::kScratchpadBase));
    CHECK(memory.isExecutable(0x80010000));

    MemoryFault fault = MemoryFault::None;
    uint32_t value = 0;
    CHECK(!memory.read32(0xBF800000, value, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::BusError));
}

TEST_CASE(memory_bios_window_and_io_errors) {
    Memory memory;
    uint32_t value = 0xFFFFFFFFu;
    CHECK(memory.read32(Memory::kBiosBase, value));
    CHECK_EQ(value, 0u);
    CHECK(memory.read32(0xBFC00000, value));
    CHECK_EQ(value, 0u);
    CHECK(memory.isExecutable(Memory::kBiosBase));

    MemoryFault fault = MemoryFault::None;
    CHECK(!memory.write32(Memory::kBiosBase, 1, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::BusError));

    CHECK(!memory.read32(0x1F801810, value, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::Unsupported));

    CHECK(!memory.read32(0x00800000, value, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::BusError));

    CHECK(!memory.read32(0xC0000000, value, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::Unsupported));
}

TEST_CASE(memory_alignment_and_region_boundaries) {
    Memory memory;
    uint16_t half = 0;
    uint32_t word = 0;
    MemoryFault fault = MemoryFault::None;

    CHECK(!memory.read16(0x00000011, half, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::Unaligned));
    CHECK(!memory.read32(0x00000012, word, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::Unaligned));
    CHECK(!memory.write16(0x00000011, 1, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::Unaligned));
    CHECK(!memory.write32(0x00000012, 1, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::Unaligned));

    CHECK(memory.read32(0x001FFFFC, word));
    CHECK(memory.read16(0x001FFFFE, half));
    CHECK(memory.read32(0xBFC7FFFC, word));
    CHECK(memory.read16(0xBFC7FFFE, half));
    CHECK(!memory.read32(0x001FFFFE, word, &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::Unaligned));
}

TEST_CASE(memory_bulk_access_and_range_checks) {
    Memory memory;
    const uint8_t data[5] = {1, 2, 3, 4, 5};
    CHECK(memory.writeBytes(0x80001000, data, sizeof(data)));
    uint8_t readBack[5] = {};
    CHECK(memory.readBytes(0x80001000, readBack, sizeof(readBack)));
    for (size_t index = 0; index < sizeof(data); ++index) {
        CHECK_EQ(readBack[index], data[index]);
    }

    CHECK(memory.fill(0x80002000, 16, 0xA5));
    uint32_t filled = 0;
    CHECK(memory.read32(0x80002000, filled));
    CHECK_EQ(filled, 0xA5A5A5A5u);

    CHECK(memory.isWritableRange(0x80003000, 16));
    CHECK(memory.isWritableRange(Memory::kScratchpadBase, 16));
    CHECK(memory.isWritableRange(0, 0));
    CHECK(!memory.isWritableRange(Memory::kBiosBase, 16));
    CHECK(!memory.isWritableRange(0x007FFFFC, 8));
    CHECK(!memory.isWritableRange(0xFFFFFFFCu, 8));

    const uint8_t bad[8] = {};
    MemoryFault fault = MemoryFault::None;
    CHECK(!memory.writeBytes(0x007FFFFC, bad, sizeof(bad), &fault));
    CHECK_EQ(static_cast<int>(fault), static_cast<int>(MemoryFault::BusError));
}

TEST_CASE(memory_reset_clears_state) {
    Memory memory;
    CHECK(memory.write32(0x80000000, 0xFFFFFFFFu));
    CHECK(memory.write32(Memory::kScratchpadBase, 0xFFFFFFFFu));
    memory.reset();
    uint32_t value = 1;
    CHECK(memory.read32(0x80000000, value));
    CHECK_EQ(value, 0u);
    CHECK(memory.read32(Memory::kScratchpadBase, value));
    CHECK_EQ(value, 0u);
}
