#include "psxreborn/memcard.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace psxreborn {

namespace {

constexpr uint32_t kHeaderSector = 0;
constexpr uint32_t kDirectoryFirstSector = 1;
constexpr uint32_t kDirectorySectors = 15;
constexpr uint32_t kBrokenListFirstSector = 16;
constexpr uint32_t kBrokenListSectors = 20;

std::vector<uint8_t> readFileBytes(const std::string& path, bool& ok) {
    ok = false;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    if (!file.eof() && file.fail()) {
        return {};
    }
    ok = true;
    return data;
}

bool writeFileBytes(const std::string& path, const uint8_t* data, size_t size, std::string& error) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot create " + path;
        return false;
    }
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.flush();
    if (!file) {
        error = "cannot write " + path;
        return false;
    }
#if !defined(_WIN32)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
    return true;
}

std::string upper(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return value;
}

bool isFreeState(uint32_t state) {
    return state == MemoryCard::kStateFree || state == MemoryCard::kStateDeletedFirst ||
           state == MemoryCard::kStateDeletedMiddle || state == MemoryCard::kStateDeletedLast;
}

bool isFirstState(uint32_t state) {
    return state == MemoryCard::kStateInUseFirst || state == MemoryCard::kStateDeletedFirst;
}

}  // namespace

MemoryCard::MemoryCard() {
    data_.fill(0);
}

MemoryCard MemoryCard::formatted() {
    MemoryCard card;
    card.format();
    card.dirty_ = true;
    return card;
}

uint8_t* MemoryCard::frame(uint8_t* data, uint32_t sector) {
    return data + static_cast<size_t>(sector) * kFrameSize;
}

const uint8_t* MemoryCard::frame(const uint8_t* data, uint32_t sector) {
    return data + static_cast<size_t>(sector) * kFrameSize;
}

uint32_t MemoryCard::readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void MemoryCard::writeLe32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFu);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint8_t MemoryCard::checksum(const uint8_t* frameData) {
    uint8_t value = 0;
    for (uint32_t index = 0; index < kFrameSize - 1; ++index) {
        value ^= frameData[index];
    }
    return value;
}

uint32_t MemoryCard::stateOf(const uint8_t* frameData) {
    return readLe32(frameData);
}

void MemoryCard::setState(uint8_t* frameData, uint32_t state) {
    writeLe32(frameData, state);
}

uint32_t MemoryCard::nextBlockOf(const uint8_t* frameData) {
    return static_cast<uint32_t>(frameData[8]) | (static_cast<uint32_t>(frameData[9]) << 8);
}

void MemoryCard::setNextBlock(uint8_t* frameData, uint32_t block) {
    frameData[8] = static_cast<uint8_t>(block & 0xFFu);
    frameData[9] = static_cast<uint8_t>((block >> 8) & 0xFFu);
}

std::string MemoryCard::nameOf(const uint8_t* frameData) {
    std::string name;
    for (uint32_t index = 0x0A; index <= 0x1E; ++index) {
        const char character = static_cast<char>(frameData[index]);
        if (character == '\0') {
            break;
        }
        name.push_back(character);
    }
    return name;
}

void MemoryCard::setName(uint8_t* frameData, const std::string& name) {
    std::memset(frameData + 0x0A, 0, 0x15);
    const size_t length = std::min<size_t>(name.size(), 20);
    std::memcpy(frameData + 0x0A, name.data(), length);
}

void MemoryCard::format() {
    data_.fill(0);
    uint8_t* header = frame(data_.data(), kHeaderSector);
    header[0] = 'M';
    header[1] = 'C';
    header[0x7F] = checksum(header);

    for (uint32_t block = 1; block < kBlockCount; ++block) {
        uint8_t* directory = frame(data_.data(), block);
        setState(directory, kStateFree);
        setNextBlock(directory, 0xFFFFu);
        directory[0x7F] = checksum(directory);
    }

    for (uint32_t index = 0; index < kBrokenListSectors; ++index) {
        uint8_t* broken = frame(data_.data(), kBrokenListFirstSector + index);
        writeLe32(broken, 0xFFFFFFFFu);
        broken[8] = 0xFFu;
        broken[9] = 0xFFu;
        broken[0x7F] = checksum(broken);
    }

    uint8_t* duplicate = frame(data_.data(), 63);
    std::memcpy(duplicate, header, kFrameSize);
    dirty_ = true;
    readOnly_ = false;
}

std::optional<MemoryCard> MemoryCard::load(const std::string& path, std::string& error,
                                           MemoryCardValidation* validation) {
    bool ok = false;
    const std::vector<uint8_t> bytes = readFileBytes(path, ok);
    if (!ok) {
        error = "cannot read memory card " + path;
        return std::nullopt;
    }
    return fromBytes(bytes, error, validation);
}

std::optional<MemoryCard> MemoryCard::fromBytes(const std::vector<uint8_t>& bytes, std::string& error,
                                                MemoryCardValidation* validation) {
    MemoryCard card;
    if (!card.loadFromBytes(bytes, error, validation)) {
        return std::nullopt;
    }
    return card;
}

bool MemoryCard::loadFromBytes(const std::vector<uint8_t>& bytes, std::string& error,
                               MemoryCardValidation* validation) {
    size_t offset = 0;
    const bool vgs = bytes.size() >= 4 && std::memcmp(bytes.data(), "VgsM", 4) == 0;
    const bool psv = bytes.size() >= 3 && std::memcmp(bytes.data(), "PSV", 3) == 0;
    const bool gme = bytes.size() >= 11 && std::memcmp(bytes.data(), "123-456-STD", 11) == 0;
    if (bytes.size() == kSize + 64 && vgs) {
        offset = 64;
    } else if (bytes.size() == kSize + 256 && psv) {
        offset = 256;
    } else if (bytes.size() == kSize + 3904 || (gme && bytes.size() == kSize + 3904)) {
        offset = 3904;
    } else if (bytes.size() == kSize) {
        offset = 0;
    } else if (bytes.size() > kSize && bytes.size() < kSize + 4096) {
        error = "unsupported memory card header";
        return false;
    } else if (bytes.size() < kSize) {
        error = "memory card image is smaller than 128 KiB";
        return false;
    } else {
        error = "memory card image is larger than 128 KiB";
        return false;
    }
    if (bytes.size() < offset + kSize) {
        error = "truncated memory card image";
        return false;
    }
    std::memcpy(data_.data(), bytes.data() + offset, kSize);
    const MemoryCardValidation report = validate();
    if (validation != nullptr) {
        *validation = report;
    }
    readOnly_ = report.readOnly();
    dirty_ = false;
    if (!report.sizeValid) {
        error = report.message;
        return false;
    }
    return true;
}

MemoryCardValidation MemoryCard::validate() const {
    MemoryCardValidation report;
    report.sizeValid = true;
    const uint8_t* header = frame(data_.data(), kHeaderSector);
    report.headerValid = header[0] == 'M' && header[1] == 'C' && header[0x7F] == checksum(header);
    if (!report.headerValid) {
        report.message = "invalid memory card header";
    }
    for (uint32_t block = 1; block < kBlockCount; ++block) {
        if (!validateDirectoryFrame(block)) {
            ++report.invalidDirectoryFrames;
        }
    }
    report.directoryValid = report.invalidDirectoryFrames == 0;
    if (!report.directoryValid) {
        report.message = "invalid memory card directory checksum";
    }
    for (uint32_t index = 0; index < kBrokenListSectors; ++index) {
        const uint8_t* broken = frame(data_.data(), kBrokenListFirstSector + index);
        if (broken[0x7F] != checksum(broken)) {
            ++report.invalidBrokenFrames;
        }
    }
    report.brokenSectorListValid = report.invalidBrokenFrames == 0;
    if (!report.brokenSectorListValid && report.message.empty()) {
        report.message = "invalid broken-sector list checksum";
    }
    if (report.readOnly() && report.message.empty()) {
        report.message = "memory card is not a valid 128 KiB image";
    }
    return report;
}

bool MemoryCard::validateDirectoryFrame(uint32_t block) const {
    if (block == 0 || block >= kBlockCount) {
        return false;
    }
    const uint8_t* directory = frame(data_.data(), block);
    return directory[0x7F] == checksum(directory);
}

bool MemoryCard::readSector(uint32_t sector, uint8_t* data, size_t size) const {
    if (sector >= kSectorCount || data == nullptr || size > kFrameSize) {
        return false;
    }
    std::memcpy(data, frame(data_.data(), sector), size);
    return true;
}

bool MemoryCard::writeSector(uint32_t sector, const uint8_t* data, size_t size) {
    if (readOnly_ || sector >= kSectorCount || data == nullptr || size > kFrameSize) {
        return false;
    }
    uint8_t* destination = frame(data_.data(), sector);
    std::memcpy(destination, data, size);
    if (size == kFrameSize && (sector == 0 || (sector >= 1 && sector < kBlockCount) ||
                               (sector >= kBrokenListFirstSector &&
                                sector < kBrokenListFirstSector + kBrokenListSectors))) {
        destination[0x7F] = checksum(destination);
    }
    dirty_ = true;
    return true;
}

bool MemoryCard::readBytes(uint32_t offset, uint8_t* data, size_t size) const {
    if (data == nullptr || offset > kSize || size > kSize - offset) {
        return false;
    }
    std::memcpy(data, data_.data() + offset, size);
    return true;
}

bool MemoryCard::writeBytes(uint32_t offset, const uint8_t* data, size_t size) {
    if (readOnly_ || data == nullptr || offset > kSize || size > kSize - offset) {
        return false;
    }
    std::memcpy(data_.data() + offset, data, size);
    for (uint32_t sector : {0u, 63u}) {
        if (offset <= static_cast<uint32_t>(sector * kFrameSize) + 0x7E &&
            offset + size > static_cast<uint32_t>(sector * kFrameSize)) {
            uint8_t* destination = frame(data_.data(), sector);
            destination[0x7F] = checksum(destination);
        }
    }
    for (uint32_t block = 1; block < kBlockCount; ++block) {
        const uint32_t sector = block;
        const uint32_t start = sector * kFrameSize;
        if (offset <= start + 0x7E && offset + size > start) {
            uint8_t* destination = frame(data_.data(), sector);
            destination[0x7F] = checksum(destination);
        }
    }
    for (uint32_t index = 0; index < kBrokenListSectors; ++index) {
        const uint32_t sector = kBrokenListFirstSector + index;
        const uint32_t start = sector * kFrameSize;
        if (offset <= start + 0x7E && offset + size > start) {
            uint8_t* destination = frame(data_.data(), sector);
            destination[0x7F] = checksum(destination);
        }
    }
    dirty_ = true;
    return true;
}

bool MemoryCard::saveBytes(const std::string& path, const uint8_t* data, size_t size,
                           std::string& error) {
    const std::filesystem::path target(path);
    std::error_code fsError;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), fsError);
        if (fsError) {
            error = "cannot create memory card directory: " + fsError.message();
            return false;
        }
    }
    return writeFileBytes(path, data, size, error);
}

bool MemoryCard::save(const std::string& path, std::string& error, bool keepBackup) {
    if (readOnly_) {
        error = "memory card is read-only because it failed validation";
        return false;
    }
    std::filesystem::path target(path);
    std::error_code fsError;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), fsError);
        if (fsError) {
            error = "cannot create memory card directory: " + fsError.message();
            return false;
        }
    }
    const std::filesystem::path temporary = target.string() + ".tmp";
    if (!writeFileBytes(temporary.string(), data_.data(), data_.size(), error)) {
        return false;
    }
    if (keepBackup && std::filesystem::exists(target, fsError)) {
        const std::filesystem::path backup = target.string() + ".bak";
        std::filesystem::copy_file(target, backup,
                                   std::filesystem::copy_options::overwrite_existing, fsError);
        if (fsError) {
            error = "cannot create memory card backup: " + fsError.message();
            std::filesystem::remove(temporary, fsError);
            return false;
        }
    }
    std::filesystem::rename(temporary, target, fsError);
    if (fsError) {
        std::filesystem::remove(target, fsError);
        fsError.clear();
        std::filesystem::rename(temporary, target, fsError);
    }
    if (fsError) {
        error = "cannot replace memory card: " + fsError.message();
        return false;
    }
    dirty_ = false;
    return true;
}

bool MemoryCard::saveIfDirty(std::string& error, bool keepBackup) {
    if (!dirty_) {
        return true;
    }
    if (path_.empty()) {
        error = "memory card has no associated file path";
        return false;
    }
    return save(path_, error, keepBackup);
}

bool MemoryCard::loadFrom(const std::string& path, std::string& error) {
    bool ok = false;
    const std::vector<uint8_t> bytes = readFileBytes(path, ok);
    if (!ok) {
        error = "cannot read memory card " + path;
        return false;
    }
    MemoryCardValidation validation;
    if (!loadFromBytes(bytes, error, &validation)) {
        return false;
    }
    path_ = path;
    return true;
}

bool MemoryCard::exportRaw(const std::string& path, std::string& error) const {
    return saveBytes(path, data_.data(), data_.size(), error);
}

bool MemoryCard::importRaw(const std::string& path, std::string& error) {
    bool ok = false;
    const std::vector<uint8_t> bytes = readFileBytes(path, ok);
    if (!ok) {
        error = "cannot read " + path;
        return false;
    }
    MemoryCardValidation validation;
    if (!loadFromBytes(bytes, error, &validation)) {
        return false;
    }
    dirty_ = true;
    return true;
}

bool MemoryCard::createBackup(const std::string& path, std::string& error) const {
    if (!saveBytes(path, data_.data(), data_.size(), error)) {
        return false;
    }
    return true;
}

bool MemoryCard::restoreBackup(const std::string& path, std::string& error) {
    const std::string backup = path + ".bak";
    std::error_code fsError;
    if (!std::filesystem::exists(backup, fsError)) {
        error = "no backup found at " + backup;
        return false;
    }
    if (!loadFrom(backup, error)) {
        return false;
    }
    readOnly_ = false;
    if (!save(path, error, false)) {
        return false;
    }
    path_ = path;
    return true;
}

std::vector<MemoryCardFileInfo> MemoryCard::files(bool includeDeleted) const {
    std::vector<MemoryCardFileInfo> result;
    for (uint32_t block = 1; block < kBlockCount; ++block) {
        const uint8_t* directory = frame(data_.data(), block);
        const uint32_t state = stateOf(directory);
        if (!isFirstState(state)) {
            continue;
        }
        if (!includeDeleted && state == kStateDeletedFirst) {
            continue;
        }
        MemoryCardFileInfo info;
        info.firstBlock = block;
        info.size = readLe32(directory + 4);
        info.name = nameOf(directory);
        info.deleted = state == kStateDeletedFirst;
        uint32_t current = block;
        bool valid = true;
        for (uint32_t count = 0; count < kBlockCount; ++count) {
            const uint8_t* currentFrame = frame(data_.data(), current);
            const uint32_t currentState = stateOf(currentFrame);
            if (current != block && currentState != kStateInUseMiddle &&
                currentState != kStateDeletedMiddle && currentState != kStateInUseLast &&
                currentState != kStateDeletedLast) {
                valid = false;
                break;
            }
            ++info.blocks;
            const uint32_t encoded = nextBlockOf(currentFrame);
            if (encoded == 0xFFFFu) {
                break;
            }
            const uint32_t next = encoded + 1u;
            if (next >= kBlockCount || next == current) {
                valid = false;
                break;
            }
            current = next;
        }
        info.valid = valid;
        result.push_back(std::move(info));
    }
    return result;
}

std::optional<MemoryCardFileInfo> MemoryCard::findFile(const std::string& name,
                                                       bool includeDeleted) const {
    const std::string wanted = upper(name);
    for (const MemoryCardFileInfo& info : files(includeDeleted)) {
        if (upper(info.name) == wanted) {
            return info;
        }
    }
    return std::nullopt;
}

std::vector<MemoryCardFileInfo> MemoryCard::matchFiles(const std::string& pattern,
                                                       bool includeDeleted) const {
    std::vector<MemoryCardFileInfo> result;
    const std::string wanted = upper(pattern);
    for (const MemoryCardFileInfo& info : files(includeDeleted)) {
        if (wildcardMatch(wanted, upper(info.name))) {
            result.push_back(info);
        }
    }
    return result;
}

bool MemoryCard::wildcardMatch(const std::string& pattern, const std::string& text) {
    size_t p = 0;
    size_t t = 0;
    size_t star = std::string::npos;
    size_t mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

std::optional<MemoryCard::Chain> MemoryCard::chainFor(const std::string& name,
                                                      bool includeDeleted) const {
    const std::string wanted = upper(name);
    for (uint32_t block = 1; block < kBlockCount; ++block) {
        const uint8_t* directory = frame(data_.data(), block);
        const uint32_t state = stateOf(directory);
        if (!isFirstState(state)) {
            continue;
        }
        if (!includeDeleted && state == kStateDeletedFirst) {
            continue;
        }
        if (upper(nameOf(directory)) != wanted) {
            continue;
        }
        Chain chain;
        chain.name = nameOf(directory);
        chain.size = readLe32(directory + 4);
        chain.deleted = state == kStateDeletedFirst;
        uint32_t current = block;
        for (uint32_t count = 0; count < kBlockCount; ++count) {
            chain.blocks.push_back(current);
            const uint32_t encoded = nextBlockOf(frame(data_.data(), current));
            if (encoded == 0xFFFFu) {
                break;
            }
            const uint32_t next = encoded + 1u;
            if (next >= kBlockCount) {
                return std::nullopt;
            }
            current = next;
        }
        return chain;
    }
    return std::nullopt;
}

uint32_t MemoryCard::freeBlockCount() const {
    uint32_t count = 0;
    for (uint32_t block = 1; block < kBlockCount; ++block) {
        if (isFreeState(stateOf(frame(data_.data(), block)))) {
            ++count;
        }
    }
    return count;
}

int MemoryCard::createFile(const std::string& name, uint32_t blocks, std::string& error) {
    if (readOnly_) {
        error = "memory card is read-only";
        return -1;
    }
    const std::string safeName = sanitizeName(name);
    if (safeName.empty()) {
        error = "invalid memory card file name";
        return -1;
    }
    if (blocks == 0) {
        blocks = 1;
    }
    if (blocks > kBlockCount - 1) {
        error = "file needs too many blocks";
        return -1;
    }
    if (findFile(safeName, true)) {
        error = "file already exists";
        return -1;
    }
    std::vector<uint32_t> freeBlocks;
    for (uint32_t block = 1; block < kBlockCount && freeBlocks.size() < blocks; ++block) {
        if (isFreeState(stateOf(frame(data_.data(), block)))) {
            freeBlocks.push_back(block);
        }
    }
    if (freeBlocks.size() < blocks) {
        error = "not enough free memory card blocks";
        return -1;
    }
    for (uint32_t index = 0; index < blocks; ++index) {
        const uint32_t block = freeBlocks[index];
        uint8_t* directory = frame(data_.data(), block);
        std::memset(directory, 0, kFrameSize);
        const uint32_t state = blocks == 1 ? kStateInUseFirst
                              : index == 0 ? kStateInUseFirst
                              : index + 1 == blocks ? kStateInUseLast
                                                    : kStateInUseMiddle;
        setState(directory, state);
        setNextBlock(directory, index + 1 == blocks ? 0xFFFFu : freeBlocks[index + 1] - 1u);
        if (index == 0) {
            writeLe32(directory + 4, blocks * kBlockSize);
            setName(directory, safeName);
        }
        directory[0x7F] = checksum(directory);
        std::memset(frame(data_.data(), block * kSectorsPerBlock), 0, kBlockSize);
    }
    dirty_ = true;
    return static_cast<int>(freeBlocks.front());
}

bool MemoryCard::deleteFile(const std::string& name) {
    const auto chain = chainFor(name, false);
    if (readOnly_ || !chain) {
        return false;
    }
    for (uint32_t block : chain->blocks) {
        uint8_t* directory = frame(data_.data(), block);
        uint32_t state = stateOf(directory);
        if (state >= kStateInUseFirst && state <= kStateInUseLast) {
            state += 0x50u;
            setState(directory, state);
            directory[0x7F] = checksum(directory);
        }
    }
    dirty_ = true;
    return true;
}

bool MemoryCard::undeleteFile(const std::string& name) {
    const auto chain = chainFor(name, true);
    if (readOnly_ || !chain) {
        return false;
    }
    for (uint32_t block : chain->blocks) {
        uint8_t* directory = frame(data_.data(), block);
        uint32_t state = stateOf(directory);
        if (state >= kStateDeletedFirst && state <= kStateDeletedLast) {
            state -= 0x50u;
            setState(directory, state);
            directory[0x7F] = checksum(directory);
        }
    }
    dirty_ = true;
    return true;
}

bool MemoryCard::renameFile(const std::string& oldName, const std::string& newName) {
    const auto chain = chainFor(oldName, false);
    const std::string safeName = sanitizeName(newName);
    if (readOnly_ || !chain || safeName.empty() || findFile(safeName, true)) {
        return false;
    }
    uint8_t* directory = frame(data_.data(), chain->blocks.front());
    setName(directory, safeName);
    directory[0x7F] = checksum(directory);
    dirty_ = true;
    return true;
}

bool MemoryCard::updateFileSize(const std::string& name, uint32_t size) {
    const auto chain = chainFor(name, false);
    if (readOnly_ || !chain || size > chain->blocks.size() * kBlockSize) {
        return false;
    }
    uint8_t* directory = frame(data_.data(), chain->blocks.front());
    writeLe32(directory + 4, size);
    directory[0x7F] = checksum(directory);
    dirty_ = true;
    return true;
}

bool MemoryCard::writeFile(const std::string& name, uint32_t offset, const uint8_t* data,
                           size_t size) {
    const auto chain = chainFor(name, false);
    if (readOnly_ || data == nullptr || !chain || offset > chain->blocks.size() * kBlockSize ||
        size > chain->blocks.size() * kBlockSize - offset) {
        return false;
    }
    size_t remaining = size;
    size_t source = 0;
    size_t position = offset;
    while (remaining != 0) {
        const size_t blockIndex = position / kBlockSize;
        const size_t blockOffset = position % kBlockSize;
        const size_t count = std::min(remaining, static_cast<size_t>(kBlockSize) - blockOffset);
        uint8_t* destination = data_.data() + static_cast<size_t>(chain->blocks[blockIndex]) * kBlockSize +
                               blockOffset;
        std::memcpy(destination, data + source, count);
        position += count;
        source += count;
        remaining -= count;
    }
    uint32_t fileSize = static_cast<uint32_t>(chain->size);
    if (offset + size > fileSize) {
        fileSize = static_cast<uint32_t>(offset + size);
        updateFileSize(name, fileSize);
    }
    dirty_ = true;
    return true;
}

bool MemoryCard::readFile(const std::string& name, uint32_t offset, uint8_t* data,
                          size_t size) const {
    const auto chain = chainFor(name, false);
    if (data == nullptr || !chain || offset > chain->size) {
        return false;
    }
    const size_t count = std::min<size_t>(size, chain->size - offset);
    size_t remaining = count;
    size_t destination = 0;
    size_t position = offset;
    while (remaining != 0) {
        const size_t blockIndex = position / kBlockSize;
        const size_t blockOffset = position % kBlockSize;
        const size_t chunk = std::min(remaining, static_cast<size_t>(kBlockSize) - blockOffset);
        const uint8_t* source = data_.data() + static_cast<size_t>(chain->blocks[blockIndex]) * kBlockSize +
                                blockOffset;
        std::memcpy(data + destination, source, chunk);
        position += chunk;
        destination += chunk;
        remaining -= chunk;
    }
    return true;
}

bool MemoryCard::importMcs(const std::string& path, std::string& error) {
    bool ok = false;
    const std::vector<uint8_t> bytes = readFileBytes(path, ok);
    if (!ok || bytes.size() < kFrameSize) {
        error = "cannot read MCS file " + path;
        return false;
    }
    const uint8_t* directory = bytes.data();
    if (!isFirstState(stateOf(directory))) {
        error = "MCS file does not start with a valid directory frame";
        return false;
    }
    const std::string name = nameOf(directory);
    uint32_t blocks = (readLe32(directory + 4) + kBlockSize - 1) / kBlockSize;
    if (blocks == 0) {
        blocks = 1;
    }
    if (bytes.size() < kFrameSize + static_cast<size_t>(blocks) * kBlockSize) {
        error = "MCS file is truncated";
        return false;
    }
    const int first = createFile(name, blocks, error);
    if (first < 0) {
        return false;
    }
    for (uint32_t block = 0; block < blocks; ++block) {
        if (!writeFile(name, block * kBlockSize, bytes.data() + kFrameSize +
                                                   static_cast<size_t>(block) * kBlockSize,
                       kBlockSize)) {
            error = "cannot import MCS data";
            return false;
        }
    }
    return true;
}

bool MemoryCard::exportMcs(const std::string& path, const std::string& name,
                           std::string& error) const {
    const auto chain = chainFor(name, false);
    if (!chain) {
        error = "file not found on memory card";
        return false;
    }
    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), frame(data_.data(), chain->blocks.front()),
                 frame(data_.data(), chain->blocks.front()) + kFrameSize);
    for (uint32_t block : chain->blocks) {
        const uint8_t* source = data_.data() + static_cast<size_t>(block) * kBlockSize;
        bytes.insert(bytes.end(), source, source + kBlockSize);
    }
    return saveBytes(path, bytes.data(), bytes.size(), error);
}

std::string MemoryCard::sanitizeName(const std::string& name) {
    std::string result;
    for (char character : name) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (code >= 0x20 && code <= 0x7E) {
            result.push_back(character);
        } else if (character == '\0') {
            break;
        }
    }
    if (result.size() > 20) {
        result.resize(20);
    }
    return result;
}

}  // namespace psxreborn
