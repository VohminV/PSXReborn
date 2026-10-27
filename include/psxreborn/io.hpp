#pragma once

#include <cstdint>

#include "psxreborn/memory.hpp"

namespace psxreborn {

class IoDevice {
public:
    virtual ~IoDevice() = default;

    virtual bool read8(uint32_t address, uint8_t& value, MemoryFault* fault) = 0;
    virtual bool read16(uint32_t address, uint16_t& value, MemoryFault* fault) = 0;
    virtual bool read32(uint32_t address, uint32_t& value, MemoryFault* fault) = 0;
    virtual bool write8(uint32_t address, uint8_t value, MemoryFault* fault) = 0;
    virtual bool write16(uint32_t address, uint16_t value, MemoryFault* fault) = 0;
    virtual bool write32(uint32_t address, uint32_t value, MemoryFault* fault) = 0;
    virtual bool irqPending() const = 0;
};

}  // namespace psxreborn
