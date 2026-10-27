#pragma once

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include "psxreborn/memory.hpp"

namespace test {

constexpr uint32_t encodeR(uint8_t rs, uint8_t rt, uint8_t rd, uint8_t shamt, uint8_t funct) {
    return (static_cast<uint32_t>(rs) << 21) |
           (static_cast<uint32_t>(rt) << 16) |
           (static_cast<uint32_t>(rd) << 11) |
           (static_cast<uint32_t>(shamt) << 6) |
           static_cast<uint32_t>(funct);
}

constexpr uint32_t encodeI(uint8_t opcode, uint8_t rs, uint8_t rt, uint16_t immediate) {
    return (static_cast<uint32_t>(opcode) << 26) |
           (static_cast<uint32_t>(rs) << 21) |
           (static_cast<uint32_t>(rt) << 16) |
           static_cast<uint32_t>(immediate);
}

constexpr uint32_t encodeJ(uint8_t opcode, uint32_t target) {
    return (static_cast<uint32_t>(opcode) << 26) | (target & 0x03FFFFFFu);
}

inline void writeProgram(psxreborn::Memory& memory, uint32_t address, std::initializer_list<uint32_t> words) {
    uint32_t offset = 0;
    for (uint32_t word : words) {
        memory.write32(address + offset, word);
        offset += 4;
    }
}

inline void writeInstruction(psxreborn::Memory& memory, uint32_t address, uint32_t word) {
    memory.write32(address, word);
}

inline uint32_t readWord(psxreborn::Memory& memory, uint32_t address) {
    uint32_t value = 0;
    memory.read32(address, value);
    return value;
}

struct ExeBuildOptions {
    uint32_t entryPoint = 0x80010000;
    uint32_t globalPointer = 0;
    uint32_t loadAddress = 0x80010000;
    uint32_t dataAddress = 0;
    uint32_t dataSize = 0;
    uint32_t bssAddress = 0;
    uint32_t bssSize = 0;
    uint32_t stackBase = 0x801FFFF0;
    uint32_t stackOffset = 0;
    uint32_t declaredLoadSize = std::numeric_limits<uint32_t>::max();
    std::string regionMarker = "Sony Computer Entertainment Inc. for Europe area";
};

inline void writeLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value & 0xFFu);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

inline std::vector<uint8_t> makePsxExe(const std::vector<uint8_t>& payload,
                                       const ExeBuildOptions& options = {}) {
    std::vector<uint8_t> file(0x800 + payload.size(), 0);
    const char magic[8] = {'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
    for (size_t index = 0; index < 8; ++index) {
        file[index] = static_cast<uint8_t>(magic[index]);
    }

    const uint32_t declaredSize = options.declaredLoadSize == std::numeric_limits<uint32_t>::max()
                                      ? static_cast<uint32_t>(payload.size())
                                      : options.declaredLoadSize;

    writeLe32(file, 0x10, options.entryPoint);
    writeLe32(file, 0x14, options.globalPointer);
    writeLe32(file, 0x18, options.loadAddress);
    writeLe32(file, 0x1C, declaredSize);
    writeLe32(file, 0x20, options.dataAddress);
    writeLe32(file, 0x24, options.dataSize);
    writeLe32(file, 0x28, options.bssAddress);
    writeLe32(file, 0x2C, options.bssSize);
    writeLe32(file, 0x30, options.stackBase);
    writeLe32(file, 0x34, options.stackOffset);

    const size_t markerLimit = 0x800 - 0x4C;
    const size_t markerLength = options.regionMarker.size() < markerLimit ? options.regionMarker.size() : markerLimit;
    for (size_t index = 0; index < markerLength; ++index) {
        file[0x4C + index] = static_cast<uint8_t>(options.regionMarker[index]);
    }

    for (size_t index = 0; index < payload.size(); ++index) {
        file[0x800 + index] = payload[index];
    }
    return file;
}

inline std::vector<uint8_t> wordsToPayload(std::initializer_list<uint32_t> words) {
    std::vector<uint8_t> payload;
    payload.reserve(words.size() * 4);
    for (uint32_t word : words) {
        payload.push_back(static_cast<uint8_t>(word & 0xFFu));
        payload.push_back(static_cast<uint8_t>((word >> 8) & 0xFFu));
        payload.push_back(static_cast<uint8_t>((word >> 16) & 0xFFu));
        payload.push_back(static_cast<uint8_t>((word >> 24) & 0xFFu));
    }
    return payload;
}

}  // namespace test
