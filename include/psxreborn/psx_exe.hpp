#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace psxreborn {

class Memory;

struct PsxExeHeader {
    uint32_t entryPoint = 0;
    uint32_t globalPointer = 0;
    uint32_t loadAddress = 0;
    uint32_t loadSize = 0;
    uint32_t dataAddress = 0;
    uint32_t dataSize = 0;
    uint32_t bssAddress = 0;
    uint32_t bssSize = 0;
    uint32_t stackBase = 0;
    uint32_t stackOffset = 0;
    std::string regionMarker;
};

struct PsxExeImage {
    PsxExeHeader header;
    std::vector<uint8_t> payload;
};

enum class PsxExeError {
    None,
    FileTooSmall,
    BadMagic,
    TruncatedPayload,
    InvalidLoadRange,
    InvalidBssRange,
};

struct PsxExeResult {
    std::optional<PsxExeImage> image;
    PsxExeError error = PsxExeError::None;
    std::string message;

    explicit operator bool() const { return image.has_value(); }
};

struct PsxExeLoadResult {
    bool success = false;
    std::string message;
};

PsxExeResult parsePsxExe(std::span<const uint8_t> fileData);
PsxExeLoadResult loadPsxExe(const PsxExeImage& image, Memory& memory);

const char* toString(PsxExeError error);

}  // namespace psxreborn
