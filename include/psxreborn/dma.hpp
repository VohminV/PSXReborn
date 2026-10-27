#pragma once

#include <array>
#include <cstdint>

#include "psxreborn/memory.hpp"

namespace psxreborn {

class Cdrom;
class Gpu;
class Interrupts;
class Spu;

class Dma {
public:
    static constexpr uint32_t kBaseAddress = 0x1F801080u;
    static constexpr uint32_t kDpcrAddress = 0x1F8010F0u;
    static constexpr uint32_t kDicrAddress = 0x1F8010F4u;
    static constexpr unsigned kChannelCount = 7;

    struct Channel {
        uint32_t madr = 0;
        uint32_t bcr = 0;
        uint32_t chcr = 0;
    };

    Dma(Memory& memory, Interrupts& interrupts, Gpu& gpu, Spu* spu = nullptr);

    void reset();
    void setSpu(Spu* spu) { spu_ = spu; }
    void setCdrom(Cdrom* cdrom) { cdrom_ = cdrom; }
    bool read32(uint32_t address, uint32_t& value);
    bool write32(uint32_t address, uint32_t value);

    const Channel& channel(unsigned index) const { return channels_[index]; }
    uint32_t dpcr() const { return dpcr_; }
    uint32_t dicr() const;
    bool busy(unsigned index) const { return (channels_[index].chcr & (1u << 24)) != 0; }

private:
    static bool channelRegister(uint32_t address, unsigned& index, uint32_t& offset);
    bool channelEnabled(unsigned index) const;
    void start(unsigned index);
    void complete(unsigned index);
    void fail(unsigned index);
    void updateMasterIrq(bool previous);
    bool transferWord(unsigned index, uint32_t& address, bool toDevice, uint32_t& word);
    bool runNormal(unsigned index);
    bool runLinkedList(unsigned index);
    bool runOtc(unsigned index);
    bool runGpu(unsigned index, uint32_t address, uint32_t words, bool linked);

    Memory& memory_;
    Interrupts& interrupts_;
    Gpu& gpu_;
    Spu* spu_ = nullptr;
    Cdrom* cdrom_ = nullptr;
    std::array<Channel, kChannelCount> channels_{};
    uint32_t dpcr_ = 0x07654321u;
    uint32_t dicr_ = 0;
};

}  // namespace psxreborn
