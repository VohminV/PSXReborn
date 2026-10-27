#include "psxreborn/psx_exe.hpp"

#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

#include "psxreborn/memory.hpp"

namespace psxreborn {

namespace {

constexpr size_t kHeaderSize = 0x800;
constexpr size_t kRegionMarkerOffset = 0x4C;

uint32_t readLe32(std::span<const uint8_t> data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

PsxExeResult makeError(PsxExeError error, std::string message) {
    PsxExeResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

}  // namespace

PsxExeResult parsePsxExe(std::span<const uint8_t> fileData) {
    if (fileData.size() < kHeaderSize) {
        return makeError(PsxExeError::FileTooSmall, "file is smaller than the 2048-byte PS-X EXE header");
    }

    static constexpr char kMagic[8] = {'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
    if (std::memcmp(fileData.data(), kMagic, sizeof(kMagic)) != 0) {
        return makeError(PsxExeError::BadMagic, "missing PS-X EXE magic");
    }

    PsxExeHeader header;
    header.entryPoint = readLe32(fileData, 0x10);
    header.globalPointer = readLe32(fileData, 0x14);
    header.loadAddress = readLe32(fileData, 0x18);
    header.loadSize = readLe32(fileData, 0x1C);
    header.dataAddress = readLe32(fileData, 0x20);
    header.dataSize = readLe32(fileData, 0x24);
    header.bssAddress = readLe32(fileData, 0x28);
    header.bssSize = readLe32(fileData, 0x2C);
    header.stackBase = readLe32(fileData, 0x30);
    header.stackOffset = readLe32(fileData, 0x34);

    const uint64_t loadEnd = static_cast<uint64_t>(header.loadAddress) + header.loadSize;
    if (loadEnd > 0x100000000ull) {
        return makeError(PsxExeError::InvalidLoadRange, "load address and size wrap the 32-bit address space");
    }

    const uint64_t bssEnd = static_cast<uint64_t>(header.bssAddress) + header.bssSize;
    if (header.bssSize != 0 && bssEnd > 0x100000000ull) {
        return makeError(PsxExeError::InvalidBssRange, "BSS address and size wrap the 32-bit address space");
    }

    if (static_cast<uint64_t>(header.loadSize) > static_cast<uint64_t>(fileData.size() - kHeaderSize)) {
        return makeError(PsxExeError::TruncatedPayload, "PS-X EXE payload is shorter than the declared load size");
    }

    size_t markerLength = 0;
    while (kRegionMarkerOffset + markerLength < kHeaderSize &&
           fileData[kRegionMarkerOffset + markerLength] != 0) {
        ++markerLength;
    }
    header.regionMarker.assign(
        reinterpret_cast<const char*>(fileData.data() + kRegionMarkerOffset), markerLength);

    PsxExeImage image;
    image.header = std::move(header);
    const auto payloadBegin = fileData.begin() + static_cast<std::ptrdiff_t>(kHeaderSize);
    const auto payloadEnd = payloadBegin + static_cast<std::ptrdiff_t>(image.header.loadSize);
    image.payload.assign(payloadBegin, payloadEnd);

    PsxExeResult result;
    result.image = std::move(image);
    return result;
}

PsxExeLoadResult loadPsxExe(const PsxExeImage& image, Memory& memory) {
    const PsxExeHeader& header = image.header;

    if (header.loadSize != 0 && !memory.isWritableRange(header.loadAddress, header.loadSize)) {
        return {false, "load range is not writable guest memory"};
    }
    if (header.bssSize != 0 && !memory.isWritableRange(header.bssAddress, header.bssSize)) {
        return {false, "BSS range is not writable guest memory"};
    }

    MemoryFault fault = MemoryFault::None;
    if (!image.payload.empty() &&
        !memory.writeBytes(header.loadAddress, image.payload.data(), image.payload.size(), &fault)) {
        return {false, std::string("failed to write payload: ") + toString(fault)};
    }

    if (header.bssSize != 0 &&
        !memory.fill(header.bssAddress, header.bssSize, 0, &fault)) {
        return {false, std::string("failed to clear BSS: ") + toString(fault)};
    }

    return {true, {}};
}

const char* toString(PsxExeError error) {
    switch (error) {
        case PsxExeError::None:
            return "none";
        case PsxExeError::FileTooSmall:
            return "file too small";
        case PsxExeError::BadMagic:
            return "bad magic";
        case PsxExeError::TruncatedPayload:
            return "truncated payload";
        case PsxExeError::InvalidLoadRange:
            return "invalid load range";
        case PsxExeError::InvalidBssRange:
            return "invalid BSS range";
    }
    return "unknown";
}

}  // namespace psxreborn
