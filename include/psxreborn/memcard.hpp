#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace psxreborn {

struct MemoryCardFileInfo {
    uint32_t firstBlock = 0;
    uint32_t blocks = 0;
    uint32_t size = 0;
    std::string name;
    bool deleted = false;
    bool valid = true;
};

struct MemoryCardValidation {
    bool headerValid = false;
    bool directoryValid = true;
    bool brokenSectorListValid = true;
    bool sizeValid = true;
    unsigned invalidDirectoryFrames = 0;
    unsigned invalidBrokenFrames = 0;
    std::string message;

    bool readOnly() const { return !sizeValid || !headerValid || !directoryValid || !brokenSectorListValid; }
};

class MemoryCard {
public:
    static constexpr uint32_t kBlockSize = 8192;
    static constexpr uint32_t kFrameSize = 128;
    static constexpr uint32_t kBlockCount = 16;
    static constexpr uint32_t kSize = kBlockCount * kBlockSize;
    static constexpr uint32_t kDirectoryBlock = 0;
    static constexpr uint32_t kFirstDataBlock = 1;
    static constexpr uint32_t kSectorCount = kSize / kFrameSize;
    static constexpr uint32_t kSectorsPerBlock = kBlockSize / kFrameSize;

    static constexpr uint32_t kStateInUseFirst = 0x51;
    static constexpr uint32_t kStateInUseMiddle = 0x52;
    static constexpr uint32_t kStateInUseLast = 0x53;
    static constexpr uint32_t kStateFree = 0xA0;
    static constexpr uint32_t kStateDeletedFirst = 0xA1;
    static constexpr uint32_t kStateDeletedMiddle = 0xA2;
    static constexpr uint32_t kStateDeletedLast = 0xA3;

    MemoryCard();

    static MemoryCard formatted();
    static std::optional<MemoryCard> load(const std::string& path, std::string& error,
                                          MemoryCardValidation* validation = nullptr);
    static std::optional<MemoryCard> fromBytes(const std::vector<uint8_t>& bytes, std::string& error,
                                               MemoryCardValidation* validation = nullptr);

    bool save(const std::string& path, std::string& error, bool keepBackup = true);
    bool saveIfDirty(std::string& error, bool keepBackup = true);
    void setPath(const std::string& path) { path_ = path; }
    const std::string& path() const { return path_; }
    bool loadFrom(const std::string& path, std::string& error);
    bool exportRaw(const std::string& path, std::string& error) const;
    bool importRaw(const std::string& path, std::string& error);
    bool importMcs(const std::string& path, std::string& error);
    bool exportMcs(const std::string& path, const std::string& name, std::string& error) const;
    bool createBackup(const std::string& path, std::string& error) const;
    bool restoreBackup(const std::string& path, std::string& error);

    void format();
    MemoryCardValidation validate() const;

    bool readSector(uint32_t sector, uint8_t* data, size_t size) const;
    bool writeSector(uint32_t sector, const uint8_t* data, size_t size);
    bool readBytes(uint32_t offset, uint8_t* data, size_t size) const;
    bool writeBytes(uint32_t offset, const uint8_t* data, size_t size);

    std::vector<MemoryCardFileInfo> files(bool includeDeleted = false) const;
    std::optional<MemoryCardFileInfo> findFile(const std::string& name,
                                               bool includeDeleted = false) const;
    std::vector<MemoryCardFileInfo> matchFiles(const std::string& pattern,
                                               bool includeDeleted = false) const;

    int createFile(const std::string& name, uint32_t blocks, std::string& error);
    bool deleteFile(const std::string& name);
    bool undeleteFile(const std::string& name);
    bool eraseFile(const std::string& name) { return deleteFile(name); }
    bool renameFile(const std::string& oldName, const std::string& newName);
    bool writeFile(const std::string& name, uint32_t offset, const uint8_t* data, size_t size);
    bool readFile(const std::string& name, uint32_t offset, uint8_t* data, size_t size) const;
    bool updateFileSize(const std::string& name, uint32_t size);

    uint32_t freeBlockCount() const;
    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }
    bool isReadOnly() const { return readOnly_; }
    void setReadOnly(bool value) { readOnly_ = value; }
    const uint8_t* data() const { return data_.data(); }
    uint8_t* data() { return data_.data(); }
    const std::array<uint8_t, kSize>& bytes() const { return data_; }

    static uint8_t checksum(const uint8_t* frame);
    static std::string sanitizeName(const std::string& name);

private:
    struct Chain {
        std::vector<uint32_t> blocks;
        uint32_t size = 0;
        std::string name;
        bool deleted = false;
    };

    bool loadFromBytes(const std::vector<uint8_t>& bytes, std::string& error,
                       MemoryCardValidation* validation);
    static bool saveBytes(const std::string& path, const uint8_t* data, size_t size, std::string& error);
    std::optional<Chain> chainFor(const std::string& name, bool includeDeleted) const;
    bool validateDirectoryFrame(uint32_t block) const;
    static bool wildcardMatch(const std::string& pattern, const std::string& text);
    static uint8_t* frame(uint8_t* data, uint32_t sector);
    static const uint8_t* frame(const uint8_t* data, uint32_t sector);
    static uint32_t readLe32(const uint8_t* data);
    static void writeLe32(uint8_t* data, uint32_t value);
    static uint32_t stateOf(const uint8_t* frameData);
    static void setState(uint8_t* frameData, uint32_t state);
    static uint32_t nextBlockOf(const uint8_t* frameData);
    static void setNextBlock(uint8_t* frameData, uint32_t block);
    static std::string nameOf(const uint8_t* frameData);
    static void setName(uint8_t* frameData, const std::string& name);
    void updateDirectoryChecksum(uint32_t sector);
    void updateAllChecksums();

    std::array<uint8_t, kSize> data_{};
    std::string path_;
    bool dirty_ = false;
    bool readOnly_ = false;
};

}  // namespace psxreborn
