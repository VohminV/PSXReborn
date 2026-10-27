#include "psxreborn/psx_exe.hpp"

#include <vector>

#include "psxreborn/memory.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using psxreborn::Memory;
using psxreborn::PsxExeError;

TEST_CASE(psx_exe_parses_header_and_payload) {
    const std::vector<uint8_t> payload = test::wordsToPayload({0x03E00008u, 0x2402002Au});
    test::ExeBuildOptions options;
    options.entryPoint = 0x80010000;
    options.globalPointer = 0x80010400;
    options.loadAddress = 0x80010000;
    options.dataAddress = 0x80010800;
    options.dataSize = 0x20;
    options.bssAddress = 0x80011000;
    options.bssSize = 0x40;
    options.stackBase = 0x801FFFF0;
    options.stackOffset = 0x10;

    const std::vector<uint8_t> file = test::makePsxExe(payload, options);
    const psxreborn::PsxExeResult result = psxreborn::parsePsxExe(file);
    CHECK(result);
    CHECK_EQ(static_cast<int>(result.error), static_cast<int>(PsxExeError::None));
    CHECK_EQ(result.image->header.entryPoint, 0x80010000u);
    CHECK_EQ(result.image->header.globalPointer, 0x80010400u);
    CHECK_EQ(result.image->header.loadAddress, 0x80010000u);
    CHECK_EQ(result.image->header.loadSize, 8u);
    CHECK_EQ(result.image->header.dataAddress, 0x80010800u);
    CHECK_EQ(result.image->header.dataSize, 0x20u);
    CHECK_EQ(result.image->header.bssAddress, 0x80011000u);
    CHECK_EQ(result.image->header.bssSize, 0x40u);
    CHECK_EQ(result.image->header.stackBase, 0x801FFFF0u);
    CHECK_EQ(result.image->header.stackOffset, 0x10u);
    CHECK_EQ(result.image->header.regionMarker,
             std::string("Sony Computer Entertainment Inc. for Europe area"));
    CHECK_EQ(result.image->payload.size(), payload.size());
    CHECK_EQ(result.image->payload[0], payload[0]);
}

TEST_CASE(psx_exe_rejects_invalid_headers) {
    const std::vector<uint8_t> payload = test::wordsToPayload({0x03E00008u});
    std::vector<uint8_t> file = test::makePsxExe(payload);

    std::vector<uint8_t> badMagic = file;
    badMagic[0] = 'X';
    psxreborn::PsxExeResult result = psxreborn::parsePsxExe(badMagic);
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.error), static_cast<int>(PsxExeError::BadMagic));

    const std::vector<uint8_t> tooSmall(0x100, 0);
    result = psxreborn::parsePsxExe(tooSmall);
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.error), static_cast<int>(PsxExeError::FileTooSmall));
}

TEST_CASE(psx_exe_rejects_truncated_and_wrapping_ranges) {
    std::vector<uint8_t> file = test::makePsxExe(test::wordsToPayload({0x03E00008u}));
    test::writeLe32(file, 0x1C, 8);
    psxreborn::PsxExeResult result = psxreborn::parsePsxExe(file);
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.error), static_cast<int>(PsxExeError::TruncatedPayload));

    test::ExeBuildOptions options;
    options.loadAddress = 0xFFFFFFF0u;
    options.declaredLoadSize = 0x20;
    file = test::makePsxExe(std::vector<uint8_t>(0x20, 0), options);
    result = psxreborn::parsePsxExe(file);
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.error), static_cast<int>(PsxExeError::InvalidLoadRange));

    options = {};
    options.bssAddress = 0xFFFFFFF0u;
    options.bssSize = 0x20;
    file = test::makePsxExe(test::wordsToPayload({0x03E00008u}), options);
    result = psxreborn::parsePsxExe(file);
    CHECK(!result);
    CHECK_EQ(static_cast<int>(result.error), static_cast<int>(PsxExeError::InvalidBssRange));
}

TEST_CASE(psx_exe_allows_trailing_file_bytes) {
    std::vector<uint8_t> file = test::makePsxExe(test::wordsToPayload({0x03E00008u}));
    file.resize(file.size() + 32, 0xCC);
    const psxreborn::PsxExeResult result = psxreborn::parsePsxExe(file);
    CHECK(result);
    CHECK_EQ(result.image->header.loadSize, 4u);
    CHECK_EQ(result.image->payload.size(), 4u);
}

TEST_CASE(psx_exe_loads_into_guest_memory_and_clears_bss) {
    const std::vector<uint8_t> payload = test::wordsToPayload({0x2402002Au, 0x03E00008u});
    test::ExeBuildOptions options;
    options.loadAddress = 0x80010000;
    options.bssAddress = 0x80011000;
    options.bssSize = 16;
    const std::vector<uint8_t> file = test::makePsxExe(payload, options);
    const psxreborn::PsxExeResult parsed = psxreborn::parsePsxExe(file);
    CHECK(parsed);

    Memory memory;
    CHECK(memory.fill(0x80011000, 16, 0xAA));
    const psxreborn::PsxExeLoadResult loaded = psxreborn::loadPsxExe(*parsed.image, memory);
    CHECK(loaded.success);
    CHECK_EQ(test::readWord(memory, 0x80010000), 0x2402002Au);
    CHECK_EQ(test::readWord(memory, 0xA0010000), 0x2402002Au);
    CHECK_EQ(test::readWord(memory, 0x80011000), 0u);
    CHECK_EQ(test::readWord(memory, 0x8001100C), 0u);
}

TEST_CASE(psx_exe_load_rejects_unwritable_ranges) {
    test::ExeBuildOptions options;
    options.loadAddress = 0x00800000;
    const std::vector<uint8_t> file = test::makePsxExe(test::wordsToPayload({0x03E00008u}), options);
    const psxreborn::PsxExeResult parsed = psxreborn::parsePsxExe(file);
    CHECK(parsed);

    Memory memory;
    const psxreborn::PsxExeLoadResult loaded = psxreborn::loadPsxExe(*parsed.image, memory);
    CHECK(!loaded.success);

    options = {};
    options.bssAddress = Memory::kBiosBase;
    options.bssSize = 16;
    const std::vector<uint8_t> bssFile = test::makePsxExe(test::wordsToPayload({0x03E00008u}), options);
    const psxreborn::PsxExeResult bssParsed = psxreborn::parsePsxExe(bssFile);
    CHECK(bssParsed);
    const psxreborn::PsxExeLoadResult bssLoaded = psxreborn::loadPsxExe(*bssParsed.image, memory);
    CHECK(!bssLoaded.success);
}
