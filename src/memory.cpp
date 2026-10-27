#include "psxreborn/memory.hpp"

#include <algorithm>

#include "psxreborn/io.hpp"

namespace psxreborn {

namespace {

void setFault(MemoryFault* fault, MemoryFault value) {
    if (fault != nullptr) {
        *fault = value;
    }
}

}  // namespace

Memory::Memory()
    : ram_(kRamSize, 0),
      scratchpad_(kScratchpadSize, 0),
      bios_(kBiosSize, 0) {}

void Memory::reset() {
    std::fill(ram_.begin(), ram_.end(), uint8_t{0});
    std::fill(scratchpad_.begin(), scratchpad_.end(), uint8_t{0});
    std::fill(bios_.begin(), bios_.end(), uint8_t{0});
}

Memory::Mapping Memory::map(uint32_t address) const {
    uint32_t physical = 0;
    bool kseg1 = false;

    if (address < 0x80000000u) {
        physical = address;
    } else if (address < 0xA0000000u) {
        physical = address & 0x1FFFFFFFu;
    } else if (address < 0xC0000000u) {
        physical = address & 0x1FFFFFFFu;
        kseg1 = true;
    } else {
        return {Region::Unsupported, 0, 0};
    }

    if (physical < kRamMirrorSize) {
        const uint32_t offset = physical & (kRamSize - 1u);
        return {Region::Ram, offset, kRamSize - offset};
    }

    if (physical >= kScratchpadBase && physical < kScratchpadBase + kScratchpadSize) {
        if (kseg1) {
            return {Region::Unmapped, 0, 0};
        }
        const uint32_t offset = physical - kScratchpadBase;
        return {Region::Scratchpad, offset, kScratchpadSize - offset};
    }

    if (physical >= kBiosBase && physical < kBiosBase + kBiosSize) {
        const uint32_t offset = physical - kBiosBase;
        return {Region::Bios, offset, kBiosSize - offset};
    }

    if (physical >= kIoBase && physical < kIoEnd) {
        return {Region::Io, 0, 0};
    }

    return {Region::Unmapped, 0, 0};
}

bool Memory::read8(uint32_t address, uint8_t& value, MemoryFault* fault) const {
    const Mapping mapping = map(address);
    switch (mapping.region) {
        case Region::Ram:
            value = ram_[mapping.offset];
            setFault(fault, MemoryFault::None);
            return true;
        case Region::Scratchpad:
            value = scratchpad_[mapping.offset];
            setFault(fault, MemoryFault::None);
            return true;
        case Region::Bios:
            value = bios_[mapping.offset];
            setFault(fault, MemoryFault::None);
            return true;
        case Region::Io:
            if (ioDevice_ != nullptr) {
                return ioDevice_->read8(address, value, fault);
            }
            setFault(fault, MemoryFault::Unsupported);
            return false;
        case Region::Unsupported:
            setFault(fault, MemoryFault::Unsupported);
            return false;
        case Region::Unmapped:
        default:
            setFault(fault, MemoryFault::BusError);
            return false;
    }
}

bool Memory::read16(uint32_t address, uint16_t& value, MemoryFault* fault) const {
    if ((address & 1u) != 0) {
        setFault(fault, MemoryFault::Unaligned);
        return false;
    }

    const Mapping mapping = map(address);
    if (mapping.region == Region::Io) {
        if (ioDevice_ != nullptr) {
            return ioDevice_->read16(address, value, fault);
        }
        setFault(fault, MemoryFault::Unsupported);
        return false;
    }
    if (mapping.region == Region::Unsupported) {
        setFault(fault, MemoryFault::Unsupported);
        return false;
    }
    if (mapping.region == Region::Unmapped || mapping.available < 2) {
        setFault(fault, MemoryFault::BusError);
        return false;
    }

    const uint8_t* source = nullptr;
    switch (mapping.region) {
        case Region::Ram:
            source = ram_.data() + mapping.offset;
            break;
        case Region::Scratchpad:
            source = scratchpad_.data() + mapping.offset;
            break;
        case Region::Bios:
            source = bios_.data() + mapping.offset;
            break;
        default:
            setFault(fault, MemoryFault::BusError);
            return false;
    }

    value = static_cast<uint16_t>(source[0]) |
            static_cast<uint16_t>(static_cast<uint16_t>(source[1]) << 8);
    setFault(fault, MemoryFault::None);
    return true;
}

bool Memory::read32(uint32_t address, uint32_t& value, MemoryFault* fault) const {
    if ((address & 3u) != 0) {
        setFault(fault, MemoryFault::Unaligned);
        return false;
    }

    const Mapping mapping = map(address);
    if (mapping.region == Region::Io) {
        if (ioDevice_ != nullptr) {
            return ioDevice_->read32(address, value, fault);
        }
        setFault(fault, MemoryFault::Unsupported);
        return false;
    }
    if (mapping.region == Region::Unsupported) {
        setFault(fault, MemoryFault::Unsupported);
        return false;
    }
    if (mapping.region == Region::Unmapped || mapping.available < 4) {
        setFault(fault, MemoryFault::BusError);
        return false;
    }

    const uint8_t* source = nullptr;
    switch (mapping.region) {
        case Region::Ram:
            source = ram_.data() + mapping.offset;
            break;
        case Region::Scratchpad:
            source = scratchpad_.data() + mapping.offset;
            break;
        case Region::Bios:
            source = bios_.data() + mapping.offset;
            break;
        default:
            setFault(fault, MemoryFault::BusError);
            return false;
    }

    value = static_cast<uint32_t>(source[0]) |
            (static_cast<uint32_t>(source[1]) << 8) |
            (static_cast<uint32_t>(source[2]) << 16) |
            (static_cast<uint32_t>(source[3]) << 24);
    setFault(fault, MemoryFault::None);
    return true;
}

bool Memory::write8(uint32_t address, uint8_t value, MemoryFault* fault) {
    const Mapping mapping = map(address);
    switch (mapping.region) {
        case Region::Ram:
            ram_[mapping.offset] = value;
            setFault(fault, MemoryFault::None);
            return true;
        case Region::Scratchpad:
            scratchpad_[mapping.offset] = value;
            setFault(fault, MemoryFault::None);
            return true;
        case Region::Bios:
            setFault(fault, MemoryFault::BusError);
            return false;
        case Region::Io:
            if (ioDevice_ != nullptr) {
                return ioDevice_->write8(address, value, fault);
            }
            setFault(fault, MemoryFault::Unsupported);
            return false;
        case Region::Unsupported:
            setFault(fault, MemoryFault::Unsupported);
            return false;
        case Region::Unmapped:
        default:
            setFault(fault, MemoryFault::BusError);
            return false;
    }
}

bool Memory::write16(uint32_t address, uint16_t value, MemoryFault* fault) {
    if ((address & 1u) != 0) {
        setFault(fault, MemoryFault::Unaligned);
        return false;
    }

    const Mapping mapping = map(address);
    if (mapping.region == Region::Io) {
        if (ioDevice_ != nullptr) {
            return ioDevice_->write16(address, value, fault);
        }
        setFault(fault, MemoryFault::Unsupported);
        return false;
    }
    if (mapping.region == Region::Ram || mapping.region == Region::Scratchpad) {
        if (mapping.available < 2) {
            setFault(fault, MemoryFault::BusError);
            return false;
        }
        uint8_t* destination = mapping.region == Region::Ram
                                   ? ram_.data() + mapping.offset
                                   : scratchpad_.data() + mapping.offset;
        destination[0] = static_cast<uint8_t>(value & 0xFFu);
        destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        setFault(fault, MemoryFault::None);
        return true;
    }

    if (mapping.region == Region::Unsupported) {
        setFault(fault, MemoryFault::Unsupported);
    } else {
        setFault(fault, MemoryFault::BusError);
    }
    return false;
}

bool Memory::write32(uint32_t address, uint32_t value, MemoryFault* fault) {
    if ((address & 3u) != 0) {
        setFault(fault, MemoryFault::Unaligned);
        return false;
    }

    const Mapping mapping = map(address);
    if (mapping.region == Region::Io) {
        if (ioDevice_ != nullptr) {
            return ioDevice_->write32(address, value, fault);
        }
        setFault(fault, MemoryFault::Unsupported);
        return false;
    }
    if (mapping.region == Region::Ram || mapping.region == Region::Scratchpad) {
        if (mapping.available < 4) {
            setFault(fault, MemoryFault::BusError);
            return false;
        }
        uint8_t* destination = mapping.region == Region::Ram
                                   ? ram_.data() + mapping.offset
                                   : scratchpad_.data() + mapping.offset;
        destination[0] = static_cast<uint8_t>(value & 0xFFu);
        destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        destination[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        destination[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
        setFault(fault, MemoryFault::None);
        return true;
    }

    if (mapping.region == Region::Unsupported) {
        setFault(fault, MemoryFault::Unsupported);
    } else {
        setFault(fault, MemoryFault::BusError);
    }
    return false;
}

bool Memory::readBytes(uint32_t address, uint8_t* data, size_t size, MemoryFault* fault) const {
    for (size_t index = 0; index < size; ++index) {
        if (!read8(address + static_cast<uint32_t>(index), data[index], fault)) {
            return false;
        }
    }
    setFault(fault, MemoryFault::None);
    return true;
}

bool Memory::writeBytes(uint32_t address, const uint8_t* data, size_t size, MemoryFault* fault) {
    if (!isWritableRange(address, size)) {
        setFault(fault, MemoryFault::BusError);
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        if (!write8(address + static_cast<uint32_t>(index), data[index], fault)) {
            return false;
        }
    }
    setFault(fault, MemoryFault::None);
    return true;
}

bool Memory::fill(uint32_t address, size_t size, uint8_t value, MemoryFault* fault) {
    if (!isWritableRange(address, size)) {
        setFault(fault, MemoryFault::BusError);
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        if (!write8(address + static_cast<uint32_t>(index), value, fault)) {
            return false;
        }
    }
    setFault(fault, MemoryFault::None);
    return true;
}

bool Memory::isWritableRange(uint32_t address, size_t size) const {
    if (size == 0) {
        return true;
    }
    const uint64_t end = static_cast<uint64_t>(address) + size;
    if (end > 0x100000000ull) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        const Mapping mapping = map(address + static_cast<uint32_t>(index));
        if (mapping.region != Region::Ram && mapping.region != Region::Scratchpad) {
            return false;
        }
    }
    return true;
}

bool Memory::isExecutable(uint32_t address) const {
    const Mapping mapping = map(address);
    return mapping.region == Region::Ram || mapping.region == Region::Bios;
}

const char* toString(MemoryFault fault) {
    switch (fault) {
        case MemoryFault::None:
            return "none";
        case MemoryFault::Unaligned:
            return "unaligned";
        case MemoryFault::BusError:
            return "bus error";
        case MemoryFault::Unsupported:
            return "unsupported memory region";
    }
    return "unknown";
}

}  // namespace psxreborn
