#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "psxreborn/memcard.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace {

std::string temporaryPath(const std::string& name) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "psxreborn-tests" / name;
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    return (directory / "card.mcd").string();
}

std::vector<uint8_t> pattern(size_t size, uint8_t seed) {
    std::vector<uint8_t> data(size);
    for (size_t index = 0; index < size; ++index) {
        data[index] = static_cast<uint8_t>((index * 31u + seed) & 0xFFu);
    }
    return data;
}

}  // namespace

TEST_CASE(memcard_formatted_layout_and_checksums) {
    const MemoryCard card = MemoryCard::formatted();
    const MemoryCardValidation validation = card.validate();
    CHECK(validation.headerValid);
    CHECK(validation.directoryValid);
    CHECK(validation.brokenSectorListValid);
    CHECK(!validation.readOnly());
    CHECK_EQ(card.freeBlockCount(), 15u);
    CHECK_EQ(card.data()[0], static_cast<uint8_t>('M'));
    CHECK_EQ(card.data()[1], static_cast<uint8_t>('C'));
    CHECK_EQ(card.data()[0x7F], static_cast<uint8_t>(0x0E));
    CHECK_EQ(card.data()[128 + 0x7F], static_cast<uint8_t>(0xA0));
}

TEST_CASE(memcard_create_write_read_and_reload) {
    MemoryCard card = MemoryCard::formatted();
    std::string error;
    const int first = card.createFile("BESLES-04064SAVE", 2, error);
    CHECK(first >= 1);
    CHECK(error.empty());
    const std::vector<uint8_t> payload = pattern(9000, 7);
    CHECK(card.writeFile("BESLES-04064SAVE", 0, payload.data(), payload.size()));
    auto info = card.findFile("BESLES-04064SAVE");
    CHECK(info.has_value());
    CHECK_EQ(info->blocks, 2u);
    CHECK_EQ(info->size, 16384u);
    std::vector<uint8_t> readBack(payload.size(), 0);
    CHECK(card.readFile("BESLES-04064SAVE", 0, readBack.data(), readBack.size()));
    CHECK(readBack == payload);

    const std::string path = temporaryPath("reload");
    CHECK(card.save(path, error));
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::exists(path + ".bak") == false);

    MemoryCardValidation validation;
    auto loaded = MemoryCard::load(path, error, &validation);
    CHECK(loaded.has_value());
    CHECK(!validation.readOnly());
    auto loadedInfo = loaded->findFile("BESLES-04064SAVE");
    CHECK(loadedInfo.has_value());
    CHECK_EQ(loadedInfo->size, 16384u);
    std::vector<uint8_t> loadedData(payload.size(), 0);
    CHECK(loaded->readFile("BESLES-04064SAVE", 0, loadedData.data(), loadedData.size()));
    CHECK(loadedData == payload);
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
}

TEST_CASE(memcard_corruption_is_detected_and_read_only) {
    MemoryCard card = MemoryCard::formatted();
    std::string error;
    CHECK(card.createFile("BESLES-04064SAVE", 1, error) >= 1);
    std::vector<uint8_t> payload = pattern(128, 3);
    CHECK(card.writeFile("BESLES-04064SAVE", 0, payload.data(), payload.size()));
    card.data()[128 + 0x7F] ^= 0x01;
    const MemoryCardValidation validation = card.validate();
    CHECK(!validation.directoryValid);
    CHECK(validation.readOnly());
    card.setReadOnly(true);
    CHECK(!card.writeFile("BESLES-04064SAVE", 0, payload.data(), payload.size()));
    card.format();
    CHECK(card.validate().readOnly() == false);
    CHECK(card.findFile("BESLES-04064SAVE") == std::nullopt);
}

TEST_CASE(memcard_backup_restore_and_recovery) {
    const std::string path = temporaryPath("backup");
    std::string error;
    MemoryCard card = MemoryCard::formatted();
    CHECK(card.createFile("BESLES-04064SAVE", 1, error) >= 1);
    const std::vector<uint8_t> first = pattern(128, 1);
    CHECK(card.writeFile("BESLES-04064SAVE", 0, first.data(), first.size()));
    CHECK(card.save(path, error));

    const std::vector<uint8_t> second = pattern(128, 2);
    CHECK(card.writeFile("BESLES-04064SAVE", 0, second.data(), second.size()));
    CHECK(card.save(path, error));
    CHECK(std::filesystem::exists(path + ".bak"));

    std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
    corrupt << "broken";
    corrupt.close();
    auto loaded = MemoryCard::load(path, error);
    CHECK(loaded == std::nullopt);

    MemoryCard recovered = MemoryCard::formatted();
    CHECK(recovered.restoreBackup(path, error));
    auto restored = MemoryCard::load(path, error);
    CHECK(restored.has_value());
    std::vector<uint8_t> data(128, 0);
    CHECK(restored->readFile("BESLES-04064SAVE", 0, data.data(), data.size()));
    CHECK(data == first);
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
}

TEST_CASE(memcard_mcs_import_export_and_wildcards) {
    const std::string path = temporaryPath("mcs");
    const std::string mcs = path + ".mcs";
    std::string error;
    MemoryCard card = MemoryCard::formatted();
    CHECK(card.createFile("BESLES-04064A", 1, error) >= 1);
    CHECK(card.createFile("BESLES-04064B", 1, error) >= 1);
    const std::vector<uint8_t> payload = pattern(300, 9);
    CHECK(card.writeFile("BESLES-04064A", 0, payload.data(), payload.size()));
    CHECK(card.exportMcs(mcs, "BESLES-04064A", error));
    CHECK(std::filesystem::exists(mcs));

    MemoryCard target = MemoryCard::formatted();
    CHECK(target.importMcs(mcs, error));
    const std::vector<uint8_t> payloadB = pattern(100, 4);
    CHECK(card.writeFile("BESLES-04064B", 0, payloadB.data(), payloadB.size()));
    const std::string mcsB = mcs + "b";
    CHECK(card.exportMcs(mcsB, "BESLES-04064B", error));
    CHECK(target.importMcs(mcsB, error));
    std::vector<uint8_t> imported(payload.size(), 0);
    CHECK(target.readFile("BESLES-04064A", 0, imported.data(), imported.size()));
    CHECK(imported == payload);
    CHECK_EQ(target.matchFiles("BESLES-04064*").size(), 2u);
    CHECK_EQ(target.matchFiles("BESLES-04064?").size(), 2u);
    CHECK(target.deleteFile("BESLES-04064A"));
    CHECK_EQ(target.matchFiles("BESLES-0406*").size(), 1u);
    CHECK_EQ(target.matchFiles("BESLES-0406*", true).size(), 2u);
    CHECK(target.undeleteFile("BESLES-04064A"));
    CHECK_EQ(target.matchFiles("BESLES-0406*").size(), 2u);
    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
}
