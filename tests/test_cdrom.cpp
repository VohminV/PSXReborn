#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "psxreborn/cdrom.hpp"
#include "psxreborn/hardware.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace {

// Builds a raw 2352-byte sector with the standard sync pattern and a marker
// payload at the given offset within the sector.
void fillRaw2352Sector(std::vector<uint8_t>& output, uint32_t index, uint16_t payloadOffset) {
    const size_t offset = index * 2352;
    if (output.size() < offset + 2352) {
        output.resize(offset + 2352, 0);
    }
    output[offset + 0] = 0x00;
    output[offset + 1] = 0xFF;
    output[offset + 2] = 0xFF;
    output[offset + 3] = 0xFF;
    output[offset + 4] = 0xFF;
    output[offset + 5] = 0xFF;
    output[offset + 6] = 0xFF;
    output[offset + 7] = 0xFF;
    output[offset + 8] = 0xFF;
    output[offset + 9] = 0xFF;
    output[offset + 10] = 0xFF;
    output[offset + 11] = 0x00;
    output[offset + payloadOffset] = 1;
    output[offset + payloadOffset + 1] = static_cast<uint8_t>(index);
    output[offset + payloadOffset + 2] = static_cast<uint8_t>(index >> 8);
}

std::filesystem::path makeTestImage(const std::filesystem::path& directory,
                                    const std::vector<uint8_t>& firstBin,
                                    const std::vector<uint8_t>& secondBin,
                                    const std::string& cueText,
                                    bool twoFiles) {
    {
        std::ofstream out(directory / "track1.bin", std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(firstBin.data()),
                  static_cast<std::streamsize>(firstBin.size()));
    }
    if (twoFiles) {
        std::ofstream out(directory / "track2.bin", std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(secondBin.data()),
                  static_cast<std::streamsize>(secondBin.size()));
    }
    std::ofstream out(directory / "test.cue", std::ios::binary | std::ios::trunc);
    out.write(cueText.data(), static_cast<std::streamsize>(cueText.size()));
    return directory / "test.cue";
}

}  // namespace

TEST_CASE(cdimage_cue_multitrack_single_file_timeline) {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "psxreborn_cd_test_d1";
    std::filesystem::create_directories(directory);

    // One bin: 20 data sectors, then 12 audio sectors.
    std::vector<uint8_t> bin;
    for (uint32_t index = 0; index < 20; ++index) {
        fillRaw2352Sector(bin, index, 16);
    }
    for (uint32_t index = 20; index < 32; ++index) {
        fillRaw2352Sector(bin, index, 24);
    }

    const std::string cueText =
        "FILE \"track1.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    PREGAP 00:02:00\n"
        "    INDEX 01 00:00:20\n"
        "REM trailing garbage should be ignored\n"
        "TIMING marker without meaning\n";

    const std::filesystem::path cuePath = makeTestImage(directory, bin, {}, cueText, false);
    std::string error;
    std::optional<CdImage> image = CdImage::open(cuePath.string(), error);
    CHECK(image.has_value());
    if (!image.has_value()) {
        std::fprintf(stderr, "open error: %s\n", error.c_str());
        std::filesystem::remove_all(directory);
        return;
    }
    const CdImage& disc = *image;

    // Timeline: data [0, 20); virtual pregap silence [20, 170); audio [170, 182).
    CHECK_EQ(disc.trackCount(), 2u);
    CHECK_EQ(disc.sectorCount(), 182u);
    CHECK_EQ(disc.tracks()[0].number, 1u);
    CHECK(!disc.tracks()[0].audio);
    CHECK_EQ(disc.tracks()[0].startLba, 0u);
    CHECK_EQ(disc.tracks()[0].endLba, 20u);
    CHECK_EQ(disc.tracks()[1].number, 2u);
    CHECK(disc.tracks()[1].audio);
    CHECK_EQ(disc.tracks()[1].index00Lba, 170u);
    CHECK_EQ(disc.tracks()[1].startLba, 170u);
    CHECK_EQ(disc.tracks()[1].endLba, 182u);

    std::vector<uint8_t> user(512);
    // MODE1/2352 user payload sits 16 bytes into the raw sector (offset 16).
    CHECK(disc.readUserSector(0, user.data(), user.size()));
    CHECK_EQ(user[0], 1u);
    CHECK_EQ(user[1], 0u);
    CHECK_EQ(user[2], 0u);
    CHECK_EQ(user[511], 0u);
    CHECK(disc.readUserSector(19, user.data(), user.size()));
    CHECK_EQ(user[1], 19u);

    std::vector<uint8_t> raw(2352);
    // Audio audio region at LBA 170 maps to file sector 20.
    CHECK(disc.readRawSector(170, raw.data(), raw.size()));
    CHECK_EQ(raw[0], 0x00u);
    CHECK_EQ(raw[1], 0xFFu);
    CHECK_EQ(raw[24], 1u);
    CHECK_EQ(raw[25], 20u);
    CHECK(disc.readRawSector(181, raw.data(), raw.size()));
    CHECK_EQ(raw[24], 1u);
    CHECK_EQ(raw[25], 31u);

    // Audio sectors have no 2048-byte user-data payload.
    CHECK(!disc.readUserSector(170, raw.data(), 2048u));

    // Pregap silence: raw reads deliver zeros.
    CHECK(disc.readRawSector(21, raw.data(), raw.size()));
    CHECK_EQ(raw[0], 0u);
    CHECK_EQ(raw[2351], 0u);

    std::filesystem::remove_all(directory);
}

TEST_CASE(cdimage_cue_multifile_toc_and_locp) {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "psxreborn_cd_test_d2";
    std::filesystem::create_directories(directory);

    std::vector<uint8_t> dataBin;
    std::vector<uint8_t> audioBin;
    for (uint32_t index = 0; index < 20; ++index) {
        fillRaw2352Sector(dataBin, index, 16);
    }
    for (uint32_t index = 0; index < 20; ++index) {
        fillRaw2352Sector(audioBin, index, 24);
    }
    const std::string cueText =
        "FILE \"track1.bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n"
        "FILE \"track2.bin\" BINARY\n"
        "  TRACK 02 AUDIO\n"
        "    PREGAP 00:02:00\n"
        "    INDEX 01 00:00:00\n";

    const std::filesystem::path cuePath = makeTestImage(directory, dataBin, audioBin, cueText, true);
    std::string error;
    std::optional<CdImage> image = CdImage::open(cuePath.string(), error);
    CHECK(image.has_value());
    if (!image.has_value()) {
        std::filesystem::remove_all(directory);
        return;
    }
    const CdImage& disc = *image;

    // Timeline: data [0, 20); silence [20, 170); audio [170, 190).
    CHECK_EQ(disc.trackCount(), 2u);
    CHECK_EQ(disc.sectorCount(), 190u);
    CHECK_EQ(disc.tracks()[1].audio, true);
    CHECK_EQ(disc.tracks()[1].startLba, 170u);
    CHECK_EQ(disc.tracks()[1].endLba, 190u);

    Interrupts interrupts;
    Cdrom cdrom(interrupts);
    cdrom.attachDisc(&*image);
    cdrom.reset();

    bool ok = false;
    auto writeReg = [&](uint32_t port, uint8_t value) {
        cdrom.write8(0x1F801800u + port, value, ok);
        CHECK(ok);
    };
    auto readReg = [&](uint32_t port, uint8_t& value) {
        cdrom.read8(0x1F801800u + port, value, ok);
        CHECK(ok);
    };

    // GetTN: first = 1, last = 2 (BCD).
    writeReg(1, 0x13);
    CHECK_EQ(cdrom.interruptFlag(), 0x03u);
    uint8_t first = 0;
    uint8_t last = 0;
    uint8_t status = 0;
    readReg(1, status);
    readReg(1, first);
    readReg(1, last);
    CHECK_EQ(first, 0x01u);
    CHECK_EQ(last, 0x02u);
    cdrom.acknowledgeInterrupt(0x03);

    // GetTD(1): starts at LBA 0 -> abs 150 frames = 00:02:00 (rounded down: 00:02).
    writeReg(2, 0x01);
    writeReg(1, 0x14);
    uint8_t minute = 0;
    uint8_t second = 0;
    readReg(1, status);
    readReg(1, minute);
    readReg(1, second);
    CHECK_EQ(minute, 0x00u);
    CHECK_EQ(second, 0x02u);
    cdrom.acknowledgeInterrupt(0x03);

    // GetTD(2): starts at LBA 170 -> abs 320 frames = 00:04:20 (rounded: 00:04).
    writeReg(2, 0x02);
    writeReg(1, 0x14);
    readReg(1, status);
    readReg(1, minute);
    readReg(1, second);
    CHECK_EQ(minute, 0x00u);
    CHECK_EQ(second, 0x04u);
    cdrom.acknowledgeInterrupt(0x03);

    // GetTD(9): invalid track -> error response.
    writeReg(2, 0x09);
    writeReg(1, 0x14);
    readReg(1, status);
    readReg(1, minute);
    CHECK_EQ(cdrom.interruptFlag(), 0x05u);
    CHECK_EQ(minute, 0x10u);
    cdrom.acknowledgeInterrupt(0x05);

    // SeekP to the middle of the virtual pregap silence: MSF 00:03:34 ->
    // LBA (0*60 + 3) * 75 + 34 - 150 = 109.
    writeReg(2, 0x00);
    writeReg(2, 0x03);
    writeReg(2, 0x34);
    writeReg(1, 0x02);  // SetLoc
    cdrom.acknowledgeInterrupt(0x03);
    writeReg(1, 0x16);  // SeekP
    cdrom.acknowledgeInterrupt(0x03);
    writeReg(1, 0x11);  // GetLocP
    uint8_t locp[8] = {};
    for (uint8_t& byte : locp) {
        readReg(1, byte);
    }
    CHECK_EQ(cdrom.interruptFlag(), 0x03u);
    CHECK_EQ(locp[0], 0x02u);
    CHECK_EQ(locp[1], 0x00u);

    // SeekP into the audio content of track 2: MSF 00:04:20 -> LBA 170.
    writeReg(2, 0x00);
    writeReg(2, 0x04);
    writeReg(2, 0x20);
    writeReg(1, 0x02);  // SetLoc
    cdrom.acknowledgeInterrupt(0x03);
    writeReg(1, 0x16);  // SeekP
    cdrom.acknowledgeInterrupt(0x03);
    writeReg(1, 0x11);  // GetLocP
    for (uint8_t& byte : locp) {
        readReg(1, byte);
    }
    CHECK_EQ(locp[0], 0x02u);
    CHECK_EQ(locp[1], 0x01u);

    std::filesystem::remove_all(directory);
}

TEST_CASE(cdimage_iso_boot_executable_and_toc_single_track) {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "psxreborn_cd_test_d3";
    std::filesystem::create_directories(directory);

    // Minimal ISO9660 image (2048 bytes per sector):
    //   sector 16: PVD with CD001 and root directory record (extent 18);
    //   sector 18: root directory with one SYSTEM.CNF;1 entry (extent 19);
    //   sector 19: SYSTEM.CNF content.
    const std::string configText =
        "BOOT = cdrom:SLES_040.64;1\n"
        "STACK = 801fff00,800\n";
    std::vector<uint8_t> iso(32 * 2048, 0);
    iso[16 * 2048 + 0] = 1;  // PVD volume descriptor type.
    iso[16 * 2048 + 1] = 'C';
    iso[16 * 2048 + 2] = 'D';
    iso[16 * 2048 + 3] = '0';
    iso[16 * 2048 + 4] = '0';
    iso[16 * 2048 + 5] = '1';
    auto writeDirRecord = [&](uint8_t* record, uint32_t extent, uint32_t size,
                              const std::string& name) {
        // ISO directory record: length, extent (both endians), size (both
        // endians), recording date (7 bytes), file flags, then the name.
        std::memset(record, 0, 64);
        const uint32_t recordLength = 33 + name.size();
        record[0] = static_cast<uint8_t>(recordLength);
        record[2] = static_cast<uint8_t>(extent & 0xFFu);
        record[3] = static_cast<uint8_t>((extent >> 8) & 0xFFu);
        record[4] = static_cast<uint8_t>((extent >> 16) & 0xFFu);
        record[5] = static_cast<uint8_t>((extent >> 24) & 0xFFu);
        record[6] = 0;
        record[7] = 0;
        record[8] = 0;
        record[9] = 0;
        record[10] = static_cast<uint8_t>(size & 0xFFu);
        record[11] = static_cast<uint8_t>((size >> 8) & 0xFFu);
        record[12] = static_cast<uint8_t>((size >> 16) & 0xFFu);
        record[13] = static_cast<uint8_t>((size >> 24) & 0xFFu);
        record[14] = 0;
        record[15] = 0;
        record[16] = 0;
        record[17] = 0;
        record[18] = 0;
        record[19] = 0;
        record[20] = 0;
        record[25] = 0;  // file flags: plain data file
        record[32] = static_cast<uint8_t>(name.size());
        std::memcpy(record + 33, name.data(), name.size());
        return static_cast<uint8_t>(recordLength);
    };
    // Root directory record points at extent 18 with one full sector size.
    writeDirRecord(iso.data() + 16 * 2048 + 156, 18, 2048, std::string("\x00", 1));
    // Root directory content: "." then ".." then SYSTEM.CNF;1.
    {
        uint8_t* dir = iso.data() + 18 * 2048;
        constexpr uint32_t recordSize = 34;  // 33 header bytes + one name byte
        writeDirRecord(dir, 18, 2048, std::string("\x00", 1));
        writeDirRecord(dir + recordSize, 18, 2048, std::string("\x01", 1));
        writeDirRecord(dir + recordSize * 2, 19, static_cast<uint32_t>(configText.size()),
                       "SYSTEM.CNF;1");
    }
    std::memcpy(iso.data() + 19 * 2048, configText.data(), configText.size());

    const std::filesystem::path isoPath = directory / "game.iso";
    {
        std::ofstream out(isoPath, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(iso.data()),
                  static_cast<std::streamsize>(iso.size()));
    }
    std::string error;
    std::optional<CdImage> image = CdImage::open(isoPath.string(), error);
    CHECK(image.has_value());
    if (!image.has_value()) {
        std::fprintf(stderr, "open error: %s\n", error.c_str());
        std::filesystem::remove_all(directory);
        return;
    }
    const CdImage& disc = *image;
    CHECK_EQ(disc.trackCount(), 1u);
    CHECK_EQ(disc.sectorCount(), 32u);
    CHECK_EQ(disc.tracks()[0].number, 1u);
    CHECK(!disc.tracks()[0].audio);

    std::string stackLine;
    std::optional<std::string> boot = disc.bootExecutable(stackLine);
    CHECK(boot.has_value());
    if (boot.has_value()) {
        CHECK_EQ(*boot, "SLES_040.64;1");
    }

    std::filesystem::remove_all(directory);
}
