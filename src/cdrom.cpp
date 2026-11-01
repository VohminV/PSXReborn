#include "psxreborn/cdrom.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "psxreborn/hardware.hpp"

namespace psxreborn {

namespace {

bool cdTrace() {
    static const bool enabled = std::getenv("PSXREBORN_CD_LOG") != nullptr;
    return enabled;
}

std::string lower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string trim(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

std::string resolveCaseInsensitive(const std::string& path) {
    std::error_code existsError;
    if (std::filesystem::exists(path, existsError)) {
        return path;
    }
    const std::filesystem::path requested(path);
    const std::filesystem::path directory = requested.parent_path();
    const std::string filename = requested.filename().string();
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error)) {
        return path;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError)) {
            continue;
        }
        if (lower(entry.path().filename().string()) == lower(filename)) {
            return entry.path().string();
        }
    }
    return path;
}

// Splits a CUE line into upper-case keyword + remainder, honouring ASCII only.
bool cueKeyword(const std::string& line, std::string& keyword, std::string& rest) {
    std::string normalized;
    size_t begin = 0;
    while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) {
        ++begin;
    }
    size_t end = begin;
    while (end < line.size() && std::isgraph(static_cast<unsigned char>(line[end])) != 0) {
        ++end;
    }
    keyword = lower(line.substr(begin, end - begin));
    rest = end < line.size() ? line.substr(end) : "";
    rest = trim(rest);
    return !keyword.empty();
}

// Extracts the first token (either quoted string or bare word) from a line.
std::string cueToken(const std::string& line, size_t& position) {
    while (position < line.size() && (line[position] == ' ' || line[position] == '\t')) {
        ++position;
    }
    std::string token;
    if (position < line.size() && line[position] == '"') {
        ++position;
        while (position < line.size() && line[position] != '"') {
            token.push_back(line[position++]);
        }
        if (position < line.size() && line[position] == '"') {
            ++position;
        }
    } else {
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t') == false) {
            token.push_back(line[position++]);
        }
    }
    return token;
}

bool parseMsfFrames(const std::string& value, uint32_t& frames, std::string& error) {
    const int minute = std::atoi(value.substr(0, 2).c_str());
    const int second = std::atoi(value.substr(3, 2).c_str());
    const int frame = std::atoi(value.substr(6, 2).c_str());
    if (value.size() != 8 || value[2] != ':' || value[5] != ':' || frame >= 75 ||
        minute > 99 || second > 59) {
        error = "cue: invalid mm:ss:ff value '" + value + "'";
        return false;
    }
    frames = static_cast<uint32_t>(minute * 60 * 75) + static_cast<uint32_t>(second * 75) +
             static_cast<uint32_t>(frame);
    return true;
}

// Sector layout for a CUE TRACK type, matching the raw byte stream inside the
// referenced file.
bool trackLayout(const std::string& type, uint32_t& sectorSize, uint32_t& dataOffset,
                 uint32_t& dataSize, bool& audio, std::string& error) {
    audio = false;
    if (type == "AUDIO") {
        sectorSize = 2352;
        dataOffset = 0;
        dataSize = 2352;
        audio = true;
        return true;
    }
    if (type == "MODE1/2048") {
        sectorSize = 2048;
        dataOffset = 0;
        dataSize = 2048;
        return true;
    }
    if (type == "MODE1/2352") {
        sectorSize = 2352;
        dataOffset = 16;
        dataSize = 2048;
        return true;
    }
    if (type == "MODE2/2352") {
        sectorSize = 2352;
        dataOffset = 24;
        dataSize = 2048;
        return true;
    }
    if (type == "MODE2/2336") {
        sectorSize = 2336;
        dataOffset = 8;
        dataSize = 2048;
        return true;
    }
    error = "cue: unsupported TRACK type '" + type + "'";
    return false;
}

}  // namespace

std::optional<CdImage> CdImage::open(const std::string& path, std::string& error) {
    CdImage image;
    const std::string extension = lower(std::filesystem::path(path).extension().string());
    if (extension == ".cue") {
        std::string cueDirectory;
        const size_t slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            cueDirectory = path.substr(0, slash + 1);
        }
        if (!image.initializeCue(path, cueDirectory, image.files_, error)) {
            return std::nullopt;
        }
        return image;
    }

    const std::string imagePath = resolveCaseInsensitive(path);
    image.files_.emplace_back();
    MediaFile& file = image.files_.back();
    file.path = imagePath;
    file.stream = std::make_unique<std::ifstream>(imagePath, std::ios::binary);
    if (!file.stream || !file.stream->is_open()) {
        error = "cannot open disc image: " + imagePath;
        return std::nullopt;
    }
    file.stream->seekg(0, std::ios::end);
    if (!file.stream) {
        error = "cannot determine disc image size";
        return std::nullopt;
    }
    file.size = static_cast<uint64_t>(file.stream->tellg());
    file.stream->clear();
    file.stream->seekg(0, std::ios::beg);
    if (!image.buildFallbackTimeline(error)) {
        return std::nullopt;
    }
    return image;
}

std::optional<CdImage> CdImage::fromBytes(std::vector<uint8_t> bytes, std::string& error) {
    CdImage image;
    if (bytes.empty()) {
        error = "disc image buffer is empty";
        return std::nullopt;
    }
    image.files_.emplace_back();
    MediaFile& file = image.files_.back();
    file.inlineBuffer = std::move(bytes);
    file.size = file.inlineBuffer.size();
    if (!image.buildFallbackTimeline(error)) {
        return std::nullopt;
    }
    return image;
}

bool CdImage::readAt(const MediaFile& file, uint64_t offset, uint8_t* destination,
                     size_t size) const {
    if (size == 0) {
        return true;
    }
    if (!file.inlineBuffer.empty()) {
        if (offset + size > file.inlineBuffer.size()) {
            return false;
        }
        std::memcpy(destination, file.inlineBuffer.data() + offset, size);
        return true;
    }
    if (!file.stream) {
        return false;
    }
    file.stream->clear();
    file.stream->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.stream) {
        return false;
    }
    file.stream->read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
    return static_cast<size_t>(file.stream->gcount()) == size;
}

bool CdImage::detectSingleFileLayout(const MediaFile& file, uint32_t& sectorSize,
                                     uint32_t& dataOffset, uint32_t& dataSize) {
    const uint64_t totalSize = file.size;
    struct Layout {
        uint32_t sectorSize;
        uint32_t dataOffset;
        uint32_t dataSize;
    };
    const Layout layouts[] = {
        {2352, 24, 2048},
        {2352, 16, 2048},
        {2048, 0, 2048},
    };

    for (const Layout& layout : layouts) {
        if (totalSize < static_cast<uint64_t>(layout.sectorSize) * 17u) {
            continue;
        }
        std::array<uint8_t, 2048> sector{};
        if (!readAt(file, static_cast<uint64_t>(16u) * layout.sectorSize + layout.dataOffset,
                    sector.data(), sector.size())) {
            continue;
        }
        if (sector[1] != 'C' || sector[2] != 'D' || sector[3] != '0' || sector[4] != '0' ||
            sector[5] != '1') {
            continue;
        }
        if (totalSize % layout.sectorSize != 0) {
            continue;
        }
        sectorSize = layout.sectorSize;
        dataOffset = layout.dataOffset;
        dataSize = layout.dataSize;
        return true;
    }
    return false;
}

// Single plain image (raw bin/iso file with no cue sheet): auto-probe the
// sector layout and build a one-track timeline covering the entire file.
bool CdImage::buildFallbackTimeline(std::string& error) {
    MediaFile& file = files_.back();
    uint32_t sectorSize = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
    if (!detectSingleFileLayout(file, sectorSize, dataOffset, dataSize)) {
        error = "disc image is not a recognizable ISO9660 image";
        return false;
    }
    file.sectorSize = sectorSize;
    file.dataOffset = dataOffset;
    file.dataSize = dataSize;
    const uint32_t sectors = static_cast<uint32_t>(file.size / sectorSize);
    regions_.push_back({CdRegion::Type::Data, false, 0, sectors, 0, 0, 1});
    CdTrack track;
    track.number = 1;
    track.audio = false;
    track.index00Lba = 0;
    track.startLba = 0;
    track.endLba = sectors;
    tracks_.push_back(track);
    sectorCount_ = sectors;
    return true;
}

bool CdImage::initializeCue(const std::string& cuePath, const std::string& cueDirectory,
                            std::vector<MediaFile>& files, std::string& error) {
    std::ifstream cue(cuePath, std::ios::binary);
    if (!cue) {
        error = "cannot open cue file: " + cuePath;
        return false;
    }

    // Phase 1: parse the cue sheet into a flat track list. Positions in
    // INDEX/PREGAP commands are offsets within the referenced file, measured
    // in frames (sectors); PREGAP sectors are virtual silence that only exist
    // on the disc timeline, while INDEX 00 sectors are stored in the file.
    struct TrackRecord {
        uint32_t number = 0;
        uint32_t fileIndex = 0;
        bool audio = false;
        bool hasIndex00 = false;
        uint32_t index00Frames = 0;
        uint32_t index01Frames = 0;
        uint32_t pregapFrames = 0;
    };

    std::vector<TrackRecord> records;
    TrackRecord current{};
    bool currentValid = false;
    int32_t currentFileIndex = -1;
    MediaFile* currentFile = nullptr;
    uint32_t fileLayoutSize = 0;

    auto openCueFile = [&](const std::string& reference) -> bool {
        std::string imagePath = reference;
        const size_t slash = imagePath.find_last_of("/\\");
        if (slash == std::string::npos) {
            imagePath = cueDirectory + imagePath;
        }
        imagePath = resolveCaseInsensitive(imagePath);
        MediaFile file;
        file.path = imagePath;
        file.stream = std::make_unique<std::ifstream>(imagePath, std::ios::binary);
        if (!file.stream || !file.stream->is_open()) {
            error = "cue: cannot open file: " + imagePath;
            return false;
        }
        file.stream->seekg(0, std::ios::end);
        if (!file.stream) {
            error = "cue: cannot determine file size: " + imagePath;
            return false;
        }
        file.size = static_cast<uint64_t>(file.stream->tellg());
        file.stream->clear();
        file.stream->seekg(0, std::ios::beg);
        files.push_back(std::move(file));
        currentFileIndex = static_cast<int32_t>(files.size() - 1);
        currentFile = &files.back();
        fileLayoutSize = 0;
        return true;
    };

    uint32_t sectorSize = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
    bool audio = false;

    std::string line;
    while (std::getline(cue, line)) {
        std::string keyword;
        std::string rest;
        if (!cueKeyword(line, keyword, rest)) {
            continue;
        }
        if (keyword == "file") {
            size_t position = 0;
            const std::string reference = cueToken(rest, position);
            if (reference.empty()) {
                error = "cue: FILE without file name";
                return false;
            }
            if (currentValid) {
                records.push_back(current);
                currentValid = false;
            }
            if (!openCueFile(reference)) {
                return false;
            }
            continue;
        }
        if (keyword == "track") {
            if (currentValid) {
                records.push_back(current);
                currentValid = false;
            }
            size_t position = 0;
            const std::string numberText = cueToken(rest, position);
            if (numberText.empty()) {
                error = "cue: TRACK without track number";
                return false;
            }
            const uint32_t number = static_cast<uint32_t>(std::atoi(numberText.c_str()));
            if (number == 0 || number > 99) {
                error = "cue: invalid track number '" + numberText + "'";
                return false;
            }
            const std::string trackType = cueToken(rest, position);
            if (trackType.empty()) {
                error = "cue: TRACK without track type";
                return false;
            }
            if (!trackLayout(trackType, sectorSize, dataOffset, dataSize, audio, error)) {
                return false;
            }
            if (currentFile == nullptr) {
                error = "cue: TRACK without preceding FILE";
                return false;
            }
            if (fileLayoutSize == 0) {
                currentFile->sectorSize = sectorSize;
                currentFile->dataOffset = dataOffset;
                currentFile->dataSize = dataSize;
                fileLayoutSize = sectorSize;
            } else if (fileLayoutSize != sectorSize) {
                error = "cue: TRACK types with differing sector sizes share one FILE";
                return false;
            }
            current = TrackRecord{};
            current.number = number;
            current.fileIndex = static_cast<uint32_t>(currentFileIndex);
            current.audio = audio;
            currentValid = true;
            continue;
        }
        if (keyword == "pregap" || keyword == "postgap") {
            uint32_t frames = 0;
            if (!parseMsfFrames(rest, frames, error)) {
                return false;
            }
            if (!currentValid) {
                error = std::string("cue: ") + (keyword == "pregap" ? "PREGAP" : "POSTGAP") +
                        " outside TRACK";
                return false;
            }
            // POSTGAP and PREGAP both occupy the same virtual gap slot in the
            // track record: real cue sheets use at most one of them per track.
            current.pregapFrames += frames;
            continue;
        }
        if (keyword == "index") {
            size_t position = 0;
            const std::string numberText = cueToken(rest, position);
            const std::string msfValue = trim(rest.substr(position));
            uint32_t frames = 0;
            if (!parseMsfFrames(msfValue, frames, error)) {
                return false;
            }
            if (!currentValid) {
                error = "cue: INDEX outside TRACK";
                return false;
            }
            const uint32_t indexNumber = static_cast<uint32_t>(std::atoi(numberText.c_str()));
            if (indexNumber == 0) {
                current.index00Frames = frames;
                current.hasIndex00 = true;
            } else if (indexNumber == 1) {
                current.index01Frames = frames;
            }
            continue;
        }
        // Remaining cue commands (CATALOG, CDTEXTFILE, REM, FLAGS, ...) do not
        // affect the sector mapping.
    }
    if (currentValid) {
        records.push_back(current);
    }
    if (records.empty()) {
        error = "cue: no TRACK entries";
        return false;
    }

    // Phase 2: allocate the disc timeline in track order. Every track owns:
    //   virtual pregap silence, then (optionally) INDEX 00 sectors from the
    //   file, then data/audio sectors from INDEX 01 up to the start of the
    //   next track (or the end of the file).
    uint32_t absolute = 0;
    for (size_t index = 0; index < records.size(); ++index) {
        const TrackRecord& track = records[index];
        const MediaFile& file = files_[track.fileIndex];
        const uint32_t fileSectors = static_cast<uint32_t>(file.size / file.sectorSize);

        // Determine the file position where this track's owned sectors end.
        uint32_t endPos = fileSectors;
        if (index + 1 < records.size() && records[index + 1].fileIndex == track.fileIndex) {
            const TrackRecord& next = records[index + 1];
            endPos = next.hasIndex00 ? next.index00Frames : next.index01Frames;
        }
        const uint32_t firstPos = track.hasIndex00 ? track.index00Frames : track.index01Frames;
        if (firstPos >= fileSectors || endPos > fileSectors || endPos <= firstPos) {
            error = "cue: track " + std::to_string(track.number) +
                    " positions fall outside the file";
            return false;
        }

        if (track.pregapFrames != 0) {
            regions_.push_back({CdRegion::Type::Silence, true, absolute, track.pregapFrames,
                                CdRegion::kNoFile, 0, static_cast<uint8_t>(track.number)});
            absolute += track.pregapFrames;
        }

        CdTrack built;
        built.number = static_cast<uint8_t>(track.number);
        built.audio = track.audio;

        if (track.hasIndex00 && track.index00Frames < track.index01Frames) {
            const uint32_t count = track.index01Frames - track.index00Frames;
            regions_.push_back({track.audio ? CdRegion::Type::Audio : CdRegion::Type::Data, true,
                                absolute, count, static_cast<uint16_t>(track.fileIndex),
                                track.index00Frames, static_cast<uint8_t>(track.number)});
            built.index00Lba = absolute;
            absolute += count;
        } else {
            built.index00Lba = absolute;
        }

        built.startLba = absolute;
        const uint32_t dataCount = endPos - track.index01Frames;
        if (dataCount != 0) {
            regions_.push_back({track.audio ? CdRegion::Type::Audio : CdRegion::Type::Data, false,
                                absolute, dataCount, static_cast<uint16_t>(track.fileIndex),
                                track.index01Frames, static_cast<uint8_t>(track.number)});
            absolute += dataCount;
        }
        built.endLba = absolute;
        tracks_.push_back(built);
    }

    if (regions_.empty() || tracks_.empty()) {
        error = "cue: internal error: empty timeline";
        return false;
    }

    sectorCount_ = absolute;
    return true;
}

const CdRegion* CdImage::regionAt(uint32_t lba) const {
    auto it = std::upper_bound(regions_.begin(), regions_.end(), lba,
                               [](uint32_t value, const CdRegion& region) {
                                   return value < region.startLba;
                               });
    if (it == regions_.begin()) {
        return nullptr;
    }
    --it;
    if (lba >= it->startLba + it->sectors) {
        return nullptr;
    }
    return &*it;
}

bool CdImage::readRawSector(uint32_t lba, uint8_t* destination, size_t size) const {
    if (!valid() || lba >= sectorCount_ || destination == nullptr) {
        return false;
    }
    const CdRegion* region = regionAt(lba);
    if (region == nullptr) {
        return false;
    }
    std::memset(destination, 0, size);
    if (region->fileIndex == CdRegion::kNoFile) {
        // Virtual silence (pregap/postgap): no bytes are stored in the files.
        return true;
    }
    const MediaFile& file = files_[region->fileIndex];
    const uint32_t regionIntraLba = lba - region->startLba;
    const uint64_t fileSector = region->fileStartSector + regionIntraLba;
    const uint64_t offset = fileSector * file.sectorSize;
    const size_t available = std::min<size_t>(size, file.sectorSize);
    return readAt(file, offset, destination, available);
}

bool CdImage::readUserSector(uint32_t lba, uint8_t* destination, size_t size) const {
    if (!valid() || lba >= sectorCount_ || destination == nullptr) {
        return false;
    }
    const CdRegion* region = regionAt(lba);
    if (region == nullptr) {
        return false;
    }
    std::memset(destination, 0, size);
    if (region->fileIndex == CdRegion::kNoFile) {
        // Virtual silence (pregap/postgap): no bytes are stored in the files.
        return true;
    }
    if (region->type == CdRegion::Type::Audio) {
        // Audio sectors have no 2048-byte user-data payload; caller must use
        // raw reads to fetch PCM data.
        return false;
    }
    const MediaFile& file = files_[region->fileIndex];
    const uint32_t regionIntraLba = lba - region->startLba;
    const uint64_t fileSector = region->fileStartSector + regionIntraLba;
    const uint64_t offset = fileSector * file.sectorSize + file.dataOffset;
    const size_t available = std::min<size_t>(size, file.dataSize);
    return readAt(file, offset, destination, available);
}

bool CdImage::readSectorData(uint32_t lba, std::array<uint8_t, kDataSectorSize>& data) const {
    return readUserSector(lba, data.data(), data.size());
}

bool CdImage::readDirectory(uint32_t extent, uint32_t size, std::vector<CdDirEntry>& entries) const {
    if (size == 0) {
        return true;
    }
    const uint32_t sectors = (size + kDataSectorSize - 1u) / kDataSectorSize;
    std::array<uint8_t, kDataSectorSize> sector{};
    for (uint32_t index = 0; index < sectors; ++index) {
        if (!readSectorData(extent + index, sector)) {
            return false;
        }
        size_t offset = 0;
        while (offset + 33 < sector.size()) {
            const uint8_t length = sector[offset];
            if (length == 0) {
                break;
            }
            if (offset + length > sector.size() || length < 33) {
                break;
            }
            CdDirEntry entry;
            entry.extent = readLe32(&sector[offset + 2]);
            entry.size = readLe32(&sector[offset + 10]);
            entry.directory = (sector[offset + 25] & 0x02u) != 0;
            const uint8_t nameLength = sector[offset + 32];
            if (offset + 33u + nameLength > sector.size()) {
                break;
            }
            std::string name(reinterpret_cast<const char*>(&sector[offset + 33]), nameLength);
            if (nameLength == 1 && static_cast<uint8_t>(name[0]) == 0) {
                name = ".";
            } else if (nameLength == 1 && static_cast<uint8_t>(name[0]) == 1) {
                name = "..";
            }
            entry.name = normalizeName(name);
            entries.push_back(std::move(entry));
            offset += length;
        }
    }
    return true;
}

std::string CdImage::normalizeName(const std::string& name) {
    std::string normalized = name;
    const size_t semicolon = normalized.find(';');
    if (semicolon != std::string::npos) {
        normalized.resize(semicolon);
    }
    while (!normalized.empty() && normalized.back() == '.') {
        normalized.pop_back();
    }
    return normalized;
}

bool CdImage::nameEquals(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index) {
        if (std::toupper(static_cast<unsigned char>(left[index])) !=
            std::toupper(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

std::optional<CdDirEntry> CdImage::find(const std::string& path) const {
    if (!valid()) {
        return std::nullopt;
    }

    std::array<uint8_t, kDataSectorSize> volume{};
    if (!readSectorData(16, volume)) {
        return std::nullopt;
    }
    const uint32_t rootExtent = readLe32(&volume[156 + 2]);
    const uint32_t rootSize = readLe32(&volume[156 + 10]);

    std::string normalized = path;
    const std::string lowered = lower(normalized);
    const size_t prefix = lowered.find("cdrom:");
    if (prefix != std::string::npos) {
        normalized = normalized.substr(prefix + 6);
    }
    for (char& character : normalized) {
        if (character == '\\') {
            character = '/';
        }
    }

    uint32_t currentExtent = rootExtent;
    uint32_t currentSize = rootSize;
    CdDirEntry result;
    bool found = false;
    size_t position = 0;
    while (position < normalized.size()) {
        while (position < normalized.size() && normalized[position] == '/') {
            ++position;
        }
        const size_t begin = position;
        while (position < normalized.size() && normalized[position] != '/') {
            ++position;
        }
        if (begin == position) {
            continue;
        }
        const std::string component = normalizeName(normalized.substr(begin, position - begin));
        if (component.empty()) {
            continue;
        }
        std::vector<CdDirEntry> entries;
        if (!readDirectory(currentExtent, currentSize, entries)) {
            return std::nullopt;
        }
        bool matched = false;
        for (const CdDirEntry& entry : entries) {
            if (entry.name == "." || entry.name == "..") {
                continue;
            }
            if (!nameEquals(entry.name, component)) {
                continue;
            }
            result = entry;
            currentExtent = entry.extent;
            currentSize = entry.size;
            matched = true;
            found = true;
            break;
        }
        if (!matched) {
            return std::nullopt;
        }
    }
    if (!found) {
        return std::nullopt;
    }
    return result;
}

bool CdImage::readFile(const std::string& path, std::vector<uint8_t>& data) const {
    const std::optional<CdDirEntry> entry = find(path);
    if (!entry || entry->directory) {
        return false;
    }
    data.assign(entry->size, 0);
    const uint32_t sectors = (entry->size + kDataSectorSize - 1u) / kDataSectorSize;
    std::array<uint8_t, kDataSectorSize> sector{};
    size_t written = 0;
    for (uint32_t index = 0; index < sectors; ++index) {
        if (!readSectorData(entry->extent + index, sector)) {
            return false;
        }
        const size_t count = std::min<size_t>(kDataSectorSize, data.size() - written);
        std::memcpy(data.data() + written, sector.data(), count);
        written += count;
    }
    return true;
}

std::optional<std::string> CdImage::bootExecutable(std::string& stackLine) const {
    std::vector<uint8_t> config;
    if (!readFile("SYSTEM.CNF", config)) {
        return std::nullopt;
    }
    std::string text;
    for (uint8_t byte : config) {
        if (byte == 0) {
            break;
        }
        text.push_back(static_cast<char>(byte));
    }

    std::string boot;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = lower(trim(line.substr(0, equals)));
        const std::string value = trim(line.substr(equals + 1));
        if (key == "boot" && boot.empty()) {
            boot = value;
        } else if (key == "stack" && stackLine.empty()) {
            stackLine = value;
        }
    }
    if (boot.empty()) {
        return std::nullopt;
    }
    const size_t colon = boot.find(':');
    if (colon != std::string::npos) {
        boot = boot.substr(colon + 1);
    }
    while (!boot.empty() && (boot.front() == '\\' || boot.front() == '/')) {
        boot.erase(boot.begin());
    }
    if (boot.empty()) {
        return std::nullopt;
    }
    return boot;
}

Cdrom::Cdrom(Interrupts& interrupts) : interrupts_(interrupts) {
    reset();
}

void Cdrom::reset() {
    state_ = State::Idle;
    index_ = 0;
    interruptEnable_ = 0x1F;
    interruptFlag_ = 0;
    mode_ = 0x20;
    setLocMinute_ = 0;
    setLocSecond_ = 2;
    setLocFrame_ = 0;
    currentLba_ = 0;
    seekLba_ = 150;
    readCycles_ = 0;
    playCycles_ = 0;
    cyclesPerSector_ = 33868800u / 75u;
    parameterCount_ = 0;
    response_.clear();
    secondResponseCycles_ = 0;
    secondResponseInterrupt_ = 0;
    secondResponseSize_ = 0;
    sectorPosition_ = 0;
    sectorLength_ = 0;
    sectorReady_ = false;
    motorOn_ = false;
}

uint8_t Cdrom::toBcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10u) << 4u) | (value % 10u));
}

uint8_t Cdrom::fromBcd(uint8_t value) {
    return static_cast<uint8_t>(((value >> 4u) * 10u) + (value & 0x0Fu));
}

uint32_t Cdrom::msfToLba(uint8_t minute, uint8_t second, uint8_t frame) {
    return (static_cast<uint32_t>(minute) * 60u + second) * 75u + frame;
}

uint8_t Cdrom::statusByte() const {
    uint8_t status = 0;
    if (motorOn_) {
        status |= 0x02;
    }
    if (state_ == State::Reading) {
        status |= 0x20;
    } else if (state_ == State::Seeking) {
        status |= 0x40;
    } else if (state_ == State::Playing) {
        status |= 0x80;
    }
    return status;
}

uint8_t Cdrom::indexStatus() const {
    uint8_t status = index_ & 0x03u;
    if (parameterCount_ == 0) {
        status |= 0x08;
    }
    if (parameterCount_ < parameter_.size()) {
        status |= 0x10;
    }
    if (!response_.empty()) {
        status |= 0x20;
    }
    if (sectorReady_ && sectorPosition_ < sectorLength_) {
        status |= 0x40;
    }
    return status;
}

void Cdrom::updateInterrupt() {
    if (interruptFlag_ != 0 && (interruptFlag_ & interruptEnable_) != 0) {
        interrupts_.request(Interrupts::kCdrom);
    }
}

bool Cdrom::read8(uint32_t address, uint8_t& value, bool& handled) {
    handled = false;
    if (address < kBaseAddress || address >= kBaseAddress + kRegisterCount) {
        return false;
    }
    handled = true;
    const uint32_t port = address - kBaseAddress;
    if (port == 0) {
        value = indexStatus();
        if (cdTrace()) {
            std::cerr << "[CD] r status = " << std::hex << static_cast<unsigned>(value) << std::dec << "\n";
        }
        return true;
    }
    if (port == 1) {
        if (!response_.empty()) {
            value = response_.front();
            response_.pop_front();
        } else {
            value = 0;
        }
        if (cdTrace()) {
            std::cerr << "[CD] r resp = " << std::hex << static_cast<unsigned>(value)
                      << std::dec << " left=" << response_.size() << "\n";
        }
        return true;
    }
    if (port == 2) {
        if (sectorReady_ && sectorPosition_ < sectorLength_) {
            value = sector_[sectorPosition_++];
            if (sectorPosition_ >= sectorLength_) {
                sectorReady_ = false;
            }
        } else {
            value = 0;
        }
        return true;
    }
    if (index_ == 1 || index_ == 3) {
        value = static_cast<uint8_t>(interruptFlag_ | 0xE0u);
    } else {
        value = static_cast<uint8_t>(interruptEnable_ | 0xE0u);
    }
    if (cdTrace()) {
        std::cerr << "[CD] r idx" << static_cast<unsigned>(index_) << " port" << port
                  << " = " << std::hex << static_cast<unsigned>(value) << std::dec << "\n";
    }
    return true;
}

bool Cdrom::write8(uint32_t address, uint8_t value, bool& handled) {
    handled = false;
    if (address < kBaseAddress || address >= kBaseAddress + kRegisterCount) {
        return false;
    }
    handled = true;
    const uint32_t port = address - kBaseAddress;
    if (cdTrace() && port != 2) {
        std::cerr << "[CD] w idx" << static_cast<unsigned>(index_) << " port" << port
                  << " = " << std::hex << static_cast<unsigned>(value) << std::dec << "\n";
    }
    if (port == 0) {
        index_ = value & 0x03u;
        return true;
    }
    switch (index_) {
        case 0:
            if (port == 1) {
                executeCommand(value);
            } else if (port == 2) {
                if (parameterCount_ < parameter_.size()) {
                    parameter_[parameterCount_++] = value;
                }
            }
            break;
        case 1:
            if (port == 2) {
                interruptEnable_ = value & 0x1Fu;
                updateInterrupt();
            } else if (port == 3) {
                acknowledgeInterrupt(value & 0x1Fu);
                if ((value & 0x40u) != 0) {
                    parameterCount_ = 0;
                }
            }
            break;
        case 2:
            if (port == 2) {
                interruptEnable_ = value & 0x1Fu;
                updateInterrupt();
            } else if (port == 3) {
                acknowledgeInterrupt(value & 0x1Fu);
                if ((value & 0x40u) != 0) {
                    parameterCount_ = 0;
                }
            }
            break;
        case 3:
            if (port == 3) {
                acknowledgeInterrupt(value & 0x1Fu);
                if ((value & 0x40u) != 0) {
                    parameterCount_ = 0;
                }
            }
            break;
        default:
            break;
    }
    return true;
}

void Cdrom::acknowledgeInterrupt(uint8_t bits) {
    interruptFlag_ = static_cast<uint8_t>(interruptFlag_ & ~(bits & 0x1Fu));
    if (interruptFlag_ == 0) {
        interrupts_.clear(Interrupts::kCdrom);
    }
    updateInterrupt();
}

void Cdrom::pushResponse(uint8_t interrupt, std::initializer_list<uint8_t> data) {
    pushResponse(interrupt, data.begin(), data.size());
}

void Cdrom::pushResponse(uint8_t interrupt, const uint8_t* data, size_t size) {
    if (cdTrace()) {
        std::cerr << "[CD] int " << std::hex << static_cast<unsigned>(interrupt) << std::dec << " data";
        for (size_t index = 0; index < size; ++index) {
            std::cerr << " " << std::hex << static_cast<unsigned>(data[index]) << std::dec;
        }
        std::cerr << "\n";
    }
    response_.clear();
    for (size_t index = 0; index < size; ++index) {
        response_.push_back(data[index]);
    }
    interruptFlag_ = interrupt;
    updateInterrupt();
    if (interruptCallback_) {
        interruptCallback_(interrupt);
    }
}

void Cdrom::scheduleSecondResponse(uint8_t interrupt, uint32_t cycles,
                                   std::initializer_list<uint8_t> data) {
    secondResponseInterrupt_ = interrupt;
    secondResponseCycles_ = std::max<uint32_t>(cycles, 1u);
    secondResponseSize_ = 0;
    for (uint8_t byte : data) {
        if (secondResponseSize_ >= secondResponseData_.size()) {
            break;
        }
        secondResponseData_[secondResponseSize_++] = byte;
    }
}

void Cdrom::setError(uint8_t code) {
    ++stats_.failures;
    pushResponse(0x05u, {statusByte(), code});
}

void Cdrom::executeCommand(uint8_t command) {
    ++stats_.commands;
    if (cdTrace()) {
        std::cerr << "[CD] cmd " << std::hex << static_cast<unsigned>(command) << std::dec
                  << " params=" << parameterCount_;
        for (uint32_t i = 0; i < parameterCount_; ++i) {
            std::cerr << " " << std::hex << static_cast<unsigned>(parameter_[i]) << std::dec;
        }
        std::cerr << " seek=" << seekLba_ << "\n";
    }
    switch (command) {
        case 0x00:  // Sync
            motorOn_ = true;
            pushResponse(0x03u, {statusByte()});
            break;
        case 0x01:  // GetStat
            motorOn_ = true;
            pushResponse(0x03u, {statusByte()});
            break;
        case 0x02: {  // SetLoc
            if (parameterCount_ >= 3) {
                setLocMinute_ = fromBcd(parameter_[0]);
                setLocSecond_ = fromBcd(parameter_[1]);
                setLocFrame_ = fromBcd(parameter_[2]);
                const uint32_t lba = msfToLba(setLocMinute_, setLocSecond_, setLocFrame_);
                seekLba_ = lba >= 150 ? lba - 150 : lba;
            }
            pushResponse(0x03u, {statusByte()});
            break;
        }
        case 0x03:  // Play
            motorOn_ = true;
            state_ = State::Playing;
            currentLba_ = seekLba_;
            pushResponse(0x03u, {statusByte()});
            break;
        case 0x07:  // Standby
            motorOn_ = true;
            state_ = State::Idle;
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u, {statusByte()});
            break;
        case 0x06:  // ReadN
            if (disc_ == nullptr) {
                setError(0x40);
                break;
            }
            motorOn_ = true;
            currentLba_ = seekLba_;
            readCycles_ = 0;
            cyclesPerSector_ = (mode_ & 0x80u) != 0 ? (33868800u / 150u) : (33868800u / 75u);
            state_ = State::Reading;
            pushResponse(0x03u, {statusByte()});
            break;
        case 0x08:  // Stop
            state_ = State::Idle;
            motorOn_ = false;
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u, {statusByte()});
            break;
        case 0x09:  // Pause
            state_ = State::Idle;
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u, {statusByte()});
            break;
        case 0x0A:  // Init
            motorOn_ = true;
            mode_ = 0x20;
            state_ = State::Idle;
            currentLba_ = seekLba_;
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 0x20000u, {statusByte()});
            break;
        case 0x0B:  // Mute
        case 0x0C:  // Demute
        case 0x0D:  // SetFilter
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u, {statusByte()});
            break;
        case 0x0E:  // SetMode
            if (parameterCount_ >= 1) {
                mode_ = parameter_[0];
            }
            pushResponse(0x03u, {statusByte()});
            break;
        case 0x0F:  // GetParam
            pushResponse(0x03u, {statusByte(), mode_, 0, 0});
            break;
        case 0x10: {  // GetLocL
            const uint32_t lba = currentLba_ + 150u;
            pushResponse(0x03u, {toBcd(static_cast<uint8_t>(lba / (60u * 75u))),
                                 toBcd(static_cast<uint8_t>((lba / 75u) % 60u)),
                                 toBcd(static_cast<uint8_t>(lba % 75u)), 0x01, 0x00, 0x00, 0x00, 0x00});
            break;
        }
        case 0x11: {  // GetLocP
            uint8_t trackNumber = 0xAA;
            uint8_t index = 0x01;
            uint32_t relBase = disc_ != nullptr ? disc_->sectorCount() : 0;
            if (disc_ != nullptr && disc_->valid()) {
                const CdRegion* region = disc_->regionAt(currentLba_);
                if (region == nullptr) {
                    // Lead-in area before the first data sector.
                    relBase = 0;
                    trackNumber = 0x00;
                } else {
                    trackNumber = region->trackNumber;
                    index = region->index00 ? 0x00 : 0x01;
                    relBase = region->startLba;
                    if (!region->index00) {
                        for (const CdTrack& track : disc_->tracks()) {
                            if (track.number == trackNumber) {
                                relBase = track.startLba;
                                break;
                            }
                        }
                    }
                }
            }
            const uint32_t relative = currentLba_ - relBase;
            const uint32_t absoluteFrames = currentLba_ + 150u;
            const uint8_t relMinute = toBcd(static_cast<uint8_t>((relative / 75u) / 60u));
            const uint8_t relSecond = toBcd(static_cast<uint8_t>((relative / 75u) % 60u));
            const uint8_t relFrame = toBcd(static_cast<uint8_t>(relative % 75u));
            const uint8_t absMinute = toBcd(static_cast<uint8_t>((absoluteFrames / 75u) / 60u));
            const uint8_t absSecond = toBcd(static_cast<uint8_t>((absoluteFrames / 75u) % 60u));
            const uint8_t absFrame = toBcd(static_cast<uint8_t>(absoluteFrames % 75u));
            pushResponse(0x03u, {trackNumber, index, relMinute, relSecond, relFrame, absMinute,
                                 absSecond, absFrame});
            break;
        }
        case 0x12:  // SetSession
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u, {statusByte()});
            break;
        case 0x13: {  // GetTN
            uint8_t first = 0x01;
            uint8_t last = 0x01;
            if (disc_ != nullptr && !disc_->tracks().empty()) {
                first = toBcd(disc_->tracks().front().number);
                last = toBcd(disc_->tracks().back().number);
            }
            pushResponse(0x03u, {statusByte(), first, last});
            break;
        }
        case 0x14: {  // GetTD
            bool ok = false;
            uint32_t startLba = 0;
            if (parameterCount_ >= 1) {
                const uint8_t track = fromBcd(parameter_[0]);
                if (disc_ != nullptr) {
                    for (const CdTrack& entry : disc_->tracks()) {
                        if (entry.number == track) {
                            startLba = entry.startLba;
                            ok = true;
                            break;
                        }
                    }
                }
                if (track == 0xA9 || track == 0xAA) {
                    // Lead-out: report the end of the last track.
                    startLba = disc_ != nullptr ? disc_->sectorCount() : 0;
                    ok = true;
                }
            }
            if (!ok) {
                setError(0x10);
                break;
            }
            const uint32_t absolute = startLba + 150u;
            pushResponse(0x03u, {statusByte(), toBcd(static_cast<uint8_t>(((absolute / 75u) / 60u) % 100u)),
                                 toBcd(static_cast<uint8_t>((absolute / 75u) % 60u))});
            break;
        }
        case 0x15:  // SeekL
        case 0x16:  // SeekP
            motorOn_ = true;
            currentLba_ = seekLba_;
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u, {statusByte()});
            break;
        case 0x19:  // Test
            if (parameterCount_ >= 1 && parameter_[0] == 0x20) {
                pushResponse(0x03u, {0x94, 0x09, 0x19, 0xC0});
            } else {
                pushResponse(0x03u, {statusByte()});
            }
            break;
        case 0x1A:  // GetID
            motorOn_ = true;
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u,
                                   {statusByte(), 0x00, 0x20, 0x00, 'S', 'C', 'E', 'x'});
            break;
        case 0x1E:  // ReadTOC
            pushResponse(0x03u, {statusByte()});
            scheduleSecondResponse(0x02u, 50000u, {statusByte()});
            break;
        default:
            ++stats_.unknownCommands;
            setError(0x40);
            break;
    }
    parameterCount_ = 0;
}

void Cdrom::tick(uint32_t cycles) {
    if (secondResponseCycles_ != 0) {
        if (cycles >= secondResponseCycles_) {
            secondResponseCycles_ = 0;
            std::array<uint8_t, 8> data = secondResponseData_;
            const uint32_t size = secondResponseSize_;
            secondResponseSize_ = 0;
            pushResponse(secondResponseInterrupt_, data.data(), size);
        } else {
            secondResponseCycles_ -= cycles;
        }
    }
    if (state_ == State::Playing) {
        // CDDA playback: the drive advances through audio sectors. Output of
        // the decoded samples into the SPU/digital audio path is not
        // implemented; position reporting (GetLocP) still reflects playback.
        playCycles_ += cycles;
        while (playCycles_ >= cyclesPerSector_) {
            playCycles_ -= cyclesPerSector_;
            ++currentLba_;
            if (disc_ == nullptr || currentLba_ >= disc_->sectorCount()) {
                state_ = State::Idle;
                break;
            }
        }
        return;
    }
    if (state_ != State::Reading) {
        return;
    }
    readCycles_ += cycles;
    if (readCycles_ < cyclesPerSector_) {
        return;
    }
    readCycles_ -= cyclesPerSector_;
    prepareSector();
}

void Cdrom::prepareSector() {
    if (disc_ == nullptr || currentLba_ >= disc_->sectorCount()) {
        setError(0x04);
        state_ = State::Idle;
        return;
    }
    const CdRegion* region = disc_->regionAt(currentLba_);
    const bool audioSector =
        region != nullptr && region->type == CdRegion::Type::Audio;
    bool ok = false;
    if (audioSector) {
        // Audio sectors are delivered as raw PCM regardless of mode: they do
        // not carry CD-ROM headers or a user-data payload.
        ok = disc_->readRawSector(currentLba_, sector_.data(), sector_.size());
        if (ok) {
            sectorLength_ = 2352;
        }
    } else if ((mode_ & 0x20u) != 0) {
        ok = disc_->readRawSector(currentLba_, sector_.data(), sector_.size());
        if (ok) {
            sectorLength_ = 2340;
            std::memmove(sector_.data(), sector_.data() + 12, sectorLength_);
        }
    } else {
        std::array<uint8_t, CdImage::kDataSectorSize> data{};
        ok = disc_->readSectorData(currentLba_, data);
        if (ok) {
            std::memcpy(sector_.data(), data.data(), data.size());
            sectorLength_ = 2048;
        }
    }
    if (!ok) {
        setError(0x04);
        state_ = State::Idle;
        return;
    }
    sectorPosition_ = 0;
    sectorReady_ = true;
    pushResponse(0x01u, {statusByte()});
    ++stats_.sectorsRead;
    ++currentLba_;
}

uint32_t Cdrom::dmaWordsPending() const {
    if (!sectorReady_ || sectorPosition_ >= sectorLength_) {
        return 0;
    }
    return (sectorLength_ - sectorPosition_ + 3u) / 4u;
}

bool Cdrom::dmaRead(uint32_t& value) {
    value = 0;
    if (!sectorReady_ && sectorPosition_ >= sectorLength_) {
        return false;
    }
    for (unsigned byte = 0; byte < 4; ++byte) {
        uint8_t data = 0;
        if (sectorPosition_ < sectorLength_) {
            data = sector_[sectorPosition_++];
        }
        value |= static_cast<uint32_t>(data) << (byte * 8u);
    }
    ++stats_.dmaWords;
    if (sectorPosition_ >= sectorLength_) {
        sectorReady_ = false;
    }
    return true;
}

bool Cdrom::dmaWrite(uint32_t value) {
    (void)value;
    return false;
}

}  // namespace psxreborn
