#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace psxreborn {

class IoDevice;

enum class MemoryFault {
    None,
    Unaligned,
    BusError,
    Unsupported,
};

class Memory {
public:
    static constexpr uint32_t kRamSize = 2u * 1024u * 1024u;
    static constexpr uint32_t kRamMirrorSize = 8u * 1024u * 1024u;
    static constexpr uint32_t kScratchpadSize = 1024u;
    static constexpr uint32_t kBiosSize = 512u * 1024u;

    static constexpr uint32_t kScratchpadBase = 0x1F800000u;
    static constexpr uint32_t kBiosBase = 0x1FC00000u;
    static constexpr uint32_t kIoBase = 0x1F801000u;
    static constexpr uint32_t kIoEnd = 0x1F803000u;

    Memory();

    void reset();
    void setIoDevice(IoDevice* device) { ioDevice_ = device; }

    bool read8(uint32_t address, uint8_t& value, MemoryFault* fault = nullptr) const;
    bool read16(uint32_t address, uint16_t& value, MemoryFault* fault = nullptr) const;
    bool read32(uint32_t address, uint32_t& value, MemoryFault* fault = nullptr) const;
    bool write8(uint32_t address, uint8_t value, MemoryFault* fault = nullptr);
    bool write16(uint32_t address, uint16_t value, MemoryFault* fault = nullptr);
    bool write32(uint32_t address, uint32_t value, MemoryFault* fault = nullptr);

    bool readBytes(uint32_t address, uint8_t* data, size_t size, MemoryFault* fault = nullptr) const;
    bool writeBytes(uint32_t address, const uint8_t* data, size_t size, MemoryFault* fault = nullptr);
    bool fill(uint32_t address, size_t size, uint8_t value, MemoryFault* fault = nullptr);

    bool isWritableRange(uint32_t address, size_t size) const;
    bool isExecutable(uint32_t address) const;

private:
    enum class Region {
        Ram,
        Scratchpad,
        Bios,
        Io,
        Unsupported,
        Unmapped,
    };

    struct Mapping {
        Region region;
        uint32_t offset;
        uint32_t available;
    };

    Mapping map(uint32_t address) const;

    std::vector<uint8_t> ram_;
    std::vector<uint8_t> scratchpad_;
    std::vector<uint8_t> bios_;
    IoDevice* ioDevice_ = nullptr;
};

const char* toString(MemoryFault fault);

}  // namespace psxreborn
