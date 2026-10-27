#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace psxreborn {

class Interrupts;

struct CdDirEntry {
    uint32_t extent = 0;
    uint32_t size = 0;
    bool directory = false;
    std::string name;
};

class CdImage {
public:
    static constexpr uint32_t kRawSectorSize = 2352;
    static constexpr uint32_t kDataSectorSize = 2048;

    CdImage() = default;

    static std::optional<CdImage> open(const std::string& path, std::string& error);
    static std::optional<CdImage> fromBytes(std::vector<uint8_t> bytes, std::string& error);

    uint32_t sectorCount() const { return sectorCount_; }
    uint32_t sectorSize() const { return sectorSize_; }
    bool valid() const { return sectorSize_ != 0; }

    bool readRawSector(uint32_t lba, uint8_t* destination, size_t size) const;
    bool readUserSector(uint32_t lba, uint8_t* destination, size_t size) const;
    bool readSectorData(uint32_t lba, std::array<uint8_t, kDataSectorSize>& data) const;

    std::optional<CdDirEntry> find(const std::string& path) const;
    bool readFile(const std::string& path, std::vector<uint8_t>& data) const;
    std::optional<std::string> bootExecutable(std::string& stackLine) const;

private:
    bool readAt(uint64_t offset, uint8_t* destination, size_t size) const;
    bool initialize(std::string& error);
    bool readDirectory(uint32_t extent, uint32_t size, std::vector<CdDirEntry>& entries) const;
    static std::string normalizeName(const std::string& name);
    static bool nameEquals(const std::string& left, const std::string& right);

    mutable std::ifstream stream_;
    std::vector<uint8_t> buffer_;
    uint32_t sectorSize_ = 0;
    uint32_t dataOffset_ = 0;
    uint32_t dataSize_ = 0;
    uint32_t sectorCount_ = 0;
};

class Cdrom {
public:
    static constexpr uint32_t kBaseAddress = 0x1F801800u;
    static constexpr uint32_t kRegisterCount = 4;

    explicit Cdrom(Interrupts& interrupts);

    void reset();
    void attachDisc(const CdImage* disc) { disc_ = disc; }
    void setInterruptCallback(std::function<void(uint8_t)> callback) {
        interruptCallback_ = std::move(callback);
    }

    bool read8(uint32_t address, uint8_t& value, bool& handled);
    bool write8(uint32_t address, uint8_t value, bool& handled);
    bool dmaRead(uint32_t& value);
    bool dmaWrite(uint32_t value);

    void tick(uint32_t cycles);
    uint32_t dmaWordsPending() const;
    bool interruptPending() const { return interruptFlag_ != 0; }
    uint8_t interruptFlag() const { return interruptFlag_; }
    void acknowledgeInterrupt(uint8_t bits);

    bool reading() const { return state_ == State::Reading; }

private:
    enum class State {
        Idle,
        Reading,
        Seeking,
        Playing,
    };

    static uint8_t toBcd(uint8_t value);
    static uint8_t fromBcd(uint8_t value);
    static uint32_t msfToLba(uint8_t minute, uint8_t second, uint8_t frame);

    uint8_t statusByte() const;
    uint8_t indexStatus() const;
    void executeCommand(uint8_t command);
    void pushResponse(uint8_t interrupt, std::initializer_list<uint8_t> data);
    void pushResponse(uint8_t interrupt, const uint8_t* data, size_t size);
    void scheduleSecondResponse(uint8_t interrupt, uint32_t cycles,
                                std::initializer_list<uint8_t> data);
    void updateInterrupt();
    void prepareSector();
    void setError(uint8_t code);

    Interrupts& interrupts_;
    const CdImage* disc_ = nullptr;
    State state_ = State::Idle;
    uint8_t index_ = 0;
    uint8_t interruptEnable_ = 0x1F;
    uint8_t interruptFlag_ = 0;
    uint8_t mode_ = 0x20;
    uint8_t setLocMinute_ = 0;
    uint8_t setLocSecond_ = 2;
    uint8_t setLocFrame_ = 0;
    uint32_t currentLba_ = 0;
    uint32_t seekLba_ = 150;
    uint32_t readCycles_ = 0;
    uint32_t cyclesPerSector_ = 33868800u / 75u;
    std::array<uint8_t, 16> parameter_{};
    uint32_t parameterCount_ = 0;
    std::deque<uint8_t> response_;
    uint32_t secondResponseCycles_ = 0;
    uint8_t secondResponseInterrupt_ = 0;
    std::array<uint8_t, 8> secondResponseData_{};
    uint32_t secondResponseSize_ = 0;
    std::array<uint8_t, 2352> sector_{};
    uint32_t sectorPosition_ = 0;
    uint32_t sectorLength_ = 0;
    bool sectorReady_ = false;
    bool motorOn_ = false;
    std::function<void(uint8_t)> interruptCallback_;
};

}  // namespace psxreborn
