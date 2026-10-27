#include "psxreborn/cdrom.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "psxreborn/hardware.hpp"

namespace psxreborn {

namespace {

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
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return path;
    }
    const std::filesystem::path requested(path);
    const std::filesystem::path directory = requested.parent_path();
    const std::string filename = requested.filename().string();
    if (directory.empty() || !std::filesystem::is_directory(directory, error)) {
        return path;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (!entry.is_regular_file(error)) {
            continue;
        }
        if (lower(entry.path().filename().string()) == lower(filename)) {
            return entry.path().string();
        }
    }
    return path;
}

bool parseCueFile(const std::string& cuePath, std::string& binPath, std::string& error) {
    std::ifstream cue(cuePath, std::ios::binary);
    if (!cue) {
        error = "cannot open cue file";
        return false;
    }
    std::string line;
    while (std::getline(cue, line)) {
        const std::string normalized = trim(line);
        if (normalized.size() < 4) {
            continue;
        }
        if (lower(normalized.substr(0, 4)) != "file") {
            continue;
        }
        size_t index = 4;
        while (index < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[index])) != 0) {
            ++index;
        }
        std::string name;
        if (index < normalized.size() && normalized[index] == '"') {
            ++index;
            while (index < normalized.size() && normalized[index] != '"') {
                name.push_back(normalized[index++]);
            }
        } else {
            while (index < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[index])) == 0) {
                name.push_back(normalized[index++]);
            }
        }
        if (name.empty()) {
            continue;
        }
        const size_t slash = cuePath.find_last_of("/\\");
        if (slash != std::string::npos && name.find_first_of("/\\") == std::string::npos) {
            name = cuePath.substr(0, slash + 1) + name;
        }
        binPath = name;
        return true;
    }
    error = "cue file has no FILE entry";
    return false;
}

}  // namespace

std::optional<CdImage> CdImage::open(const std::string& path, std::string& error) {
    std::string imagePath = path;
    const std::string extension = lower(std::filesystem::path(path).extension().string());
    if (extension == ".cue") {
        if (!parseCueFile(path, imagePath, error)) {
            return std::nullopt;
        }
    }

    CdImage image;
    imagePath = resolveCaseInsensitive(imagePath);
    image.stream_.open(imagePath, std::ios::binary);
    if (!image.stream_) {
        error = "cannot open disc image: " + imagePath;
        return std::nullopt;
    }
    if (!image.initialize(error)) {
        return std::nullopt;
    }
    return image;
}

std::optional<CdImage> CdImage::fromBytes(std::vector<uint8_t> bytes, std::string& error) {
    CdImage image;
    image.buffer_ = std::move(bytes);
    if (!image.initialize(error)) {
        return std::nullopt;
    }
    return image;
}

bool CdImage::readAt(uint64_t offset, uint8_t* destination, size_t size) const {
    if (size == 0) {
        return true;
    }
    if (!buffer_.empty()) {
        if (offset + size > buffer_.size()) {
            return false;
        }
        std::memcpy(destination, buffer_.data() + offset, size);
        return true;
    }
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream_) {
        return false;
    }
    stream_.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(size));
    return static_cast<size_t>(stream_.gcount()) == size;
}

bool CdImage::initialize(std::string& error) {
    uint64_t totalSize = 0;
    if (!buffer_.empty()) {
        totalSize = buffer_.size();
    } else {
        stream_.seekg(0, std::ios::end);
        if (!stream_) {
            error = "cannot determine disc image size";
            return false;
        }
        totalSize = static_cast<uint64_t>(stream_.tellg());
        stream_.seekg(0, std::ios::beg);
    }

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
        if (!readAt(static_cast<uint64_t>(16u) * layout.sectorSize + layout.dataOffset, sector.data(),
                    sector.size())) {
            continue;
        }
        if (sector[1] != 'C' || sector[2] != 'D' || sector[3] != '0' || sector[4] != '0' ||
            sector[5] != '1') {
            continue;
        }
        if (totalSize % layout.sectorSize != 0) {
            continue;
        }
        sectorSize_ = layout.sectorSize;
        dataOffset_ = layout.dataOffset;
        dataSize_ = layout.dataSize;
        sectorCount_ = static_cast<uint32_t>(totalSize / layout.sectorSize);
        return true;
    }

    error = "disc image is not a recognizable ISO9660 image";
    return false;
}

bool CdImage::readRawSector(uint32_t lba, uint8_t* destination, size_t size) const {
    if (!valid() || lba >= sectorCount_ || destination == nullptr) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(lba) * sectorSize_;
    std::memset(destination, 0, size);
    const size_t available = std::min<size_t>(size, sectorSize_);
    return readAt(offset, destination, available);
}

bool CdImage::readUserSector(uint32_t lba, uint8_t* destination, size_t size) const {
    if (!valid() || lba >= sectorCount_ || destination == nullptr) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(lba) * sectorSize_ + dataOffset_;
    std::memset(destination, 0, size);
    const size_t available = std::min<size_t>(size, dataSize_);
    return readAt(offset, destination, available);
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
    cyclesPerSector_ = 33868800u / 75u;
    parameterCount_ = 0;
    response_.clear();
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
        return true;
    }
    if (port == 1) {
        if (index_ == 0 && !response_.empty()) {
            value = response_.front();
            response_.pop_front();
        } else {
            value = 0;
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
    return true;
}

bool Cdrom::write8(uint32_t address, uint8_t value, bool& handled) {
    handled = false;
    if (address < kBaseAddress || address >= kBaseAddress + kRegisterCount) {
        return false;
    }
    handled = true;
    const uint32_t port = address - kBaseAddress;
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
    response_.clear();
    for (uint8_t byte : data) {
        response_.push_back(byte);
    }
    interruptFlag_ = interrupt;
    updateInterrupt();
    if (interruptCallback_) {
        interruptCallback_(interrupt);
    }
}

void Cdrom::setError(uint8_t code) {
    pushResponse(0x05u, {statusByte(), code});
}

void Cdrom::executeCommand(uint8_t command) {
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
            break;
        case 0x09:  // Pause
            state_ = State::Idle;
            pushResponse(0x03u, {statusByte()});
            break;
        case 0x0A:  // Init
            motorOn_ = true;
            mode_ = 0x20;
            state_ = State::Idle;
            currentLba_ = seekLba_;
            pushResponse(0x03u, {statusByte()});
            break;
        case 0x0B:  // Mute
        case 0x0C:  // Demute
        case 0x0D:  // SetFilter
            pushResponse(0x03u, {statusByte()});
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
            const uint32_t lba = currentLba_ + 150u;
            const uint8_t minute = toBcd(static_cast<uint8_t>(lba / (60u * 75u)));
            const uint8_t second = toBcd(static_cast<uint8_t>((lba / 75u) % 60u));
            const uint8_t frame = toBcd(static_cast<uint8_t>(lba % 75u));
            pushResponse(0x03u, {0x01, 0x01, minute, second, frame, minute, second, frame});
            break;
        }
        case 0x13:  // GetTN
            pushResponse(0x03u, {statusByte(), 0x01, 0x01});
            break;
        case 0x14:  // GetTD
            pushResponse(0x03u, {statusByte(), toBcd(74), toBcd(0)});
            break;
        case 0x15:  // SeekL
        case 0x16:  // SeekP
            motorOn_ = true;
            currentLba_ = seekLba_;
            pushResponse(0x03u, {statusByte()});
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
            break;
        case 0x1E:  // ReadTOC
            pushResponse(0x03u, {statusByte()});
            break;
        default:
            setError(0x40);
            break;
    }
    parameterCount_ = 0;
}

void Cdrom::tick(uint32_t cycles) {
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
    if ((mode_ & 0x20u) != 0) {
        if (!disc_->readRawSector(currentLba_, sector_.data(), sector_.size())) {
            setError(0x04);
            state_ = State::Idle;
            return;
        }
        sectorLength_ = 2340;
        std::memmove(sector_.data(), sector_.data() + 12, sectorLength_);
    } else {
        std::array<uint8_t, CdImage::kDataSectorSize> data{};
        if (!disc_->readSectorData(currentLba_, data)) {
            setError(0x04);
            state_ = State::Idle;
            return;
        }
        std::memcpy(sector_.data(), data.data(), data.size());
        sectorLength_ = 2048;
    }
    sectorPosition_ = 0;
    sectorReady_ = true;
    pushResponse(0x01u, {statusByte()});
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
