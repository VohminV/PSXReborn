#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "psxreborn/memory.hpp"

namespace psxreborn {

class Spu {
public:
    static constexpr uint32_t kBaseAddress = 0x1F801C00u;
    static constexpr uint32_t kRamSize = 512u * 1024u;
    static constexpr unsigned kVoiceCount = 24;

    Spu();

    void reset();
    bool read16(uint32_t address, uint16_t& value) const;
    bool write16(uint32_t address, uint16_t value);
    bool read32(uint32_t address, uint32_t& value) const;
    bool write32(uint32_t address, uint32_t value);

    bool dmaWrite(uint32_t word);
    bool dmaRead(uint32_t& word);
    uint32_t transferAddress() const { return transferAddress_; }

    uint8_t ram(uint32_t address) const;
    void setRam(uint32_t address, uint8_t value);
    uint16_t currentEnvelope(unsigned voice) const;
    bool voiceKeyedOn(unsigned voice) const;

    static std::array<int16_t, 28> decodeBlock(const uint8_t* block, int16_t& history1, int16_t& history2);

private:
    struct Voice {
        uint16_t volumeLeft = 0;
        uint16_t volumeRight = 0;
        uint16_t pitch = 0;
        uint16_t startAddress = 0;
        uint16_t adsrLow = 0;
        uint16_t adsrHigh = 0;
        uint16_t envelope = 0;
        uint16_t repeatAddress = 0;
        uint16_t currentAddress = 0;
        int16_t history1 = 0;
        int16_t history2 = 0;
        bool keyedOn = false;
        bool end = false;
    };

    static bool voiceRegister(uint32_t offset, unsigned& voice, uint32_t& index);
    static int16_t clamp16(int32_t value);

    void startVoice(unsigned voice);
    void stopVoice(unsigned voice);
    void writeVoiceRegister(unsigned voice, uint32_t index, uint16_t value);
    uint16_t readVoiceRegister(unsigned voice, uint32_t index) const;
    void updateTransferAddress();
    uint16_t status() const;
    static void setFault(MemoryFault* fault, MemoryFault value);

    std::vector<uint8_t> ram_;
    std::array<Voice, kVoiceCount> voices_{};
    std::array<uint16_t, 0x100> registers_{};
    uint32_t transferAddress_ = 0;
    uint16_t control_ = 0;
    uint16_t keyOn_ = 0;
    uint16_t keyOff_ = 0;
    uint16_t endFlags_ = 0;
    uint16_t irqAddress_ = 0;
    uint16_t ramControl_ = 0;
    bool irqFlag_ = false;
};

}  // namespace psxreborn
