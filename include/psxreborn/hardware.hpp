#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "psxreborn/cdrom.hpp"
#include "psxreborn/dma.hpp"
#include "psxreborn/gpu.hpp"
#include "psxreborn/input_trace.hpp"
#include "psxreborn/spu.hpp"
#include "psxreborn/io.hpp"
#include "psxreborn/memcard.hpp"
#include "psxreborn/sio0.hpp"

namespace psxreborn {

struct InterruptStats {
    std::array<uint64_t, 11> requests{};
};

class Cpu;
class Memory;

class Interrupts {
public:
    static constexpr uint32_t kVBlank = 1u << 0;
    static constexpr uint32_t kGpu = 1u << 1;
    static constexpr uint32_t kCdrom = 1u << 2;
    static constexpr uint32_t kDma = 1u << 3;
    static constexpr uint32_t kTimer0 = 1u << 4;
    static constexpr uint32_t kTimer1 = 1u << 5;
    static constexpr uint32_t kTimer2 = 1u << 6;
    static constexpr uint32_t kPad = 1u << 7;
    static constexpr uint32_t kSio = 1u << 8;
    static constexpr uint32_t kSpu = 1u << 9;
    static constexpr uint32_t kLightpen = 1u << 10;

    static constexpr uint32_t kStatAddress = 0x1F801070u;
    static constexpr uint32_t kMaskAddress = 0x1F801074u;

    void reset();
    void request(uint32_t bits) {
        status_ |= bits & 0x7FFu;
        for (uint32_t bit = 0; bit < 11; ++bit) {
            if ((bits & (1u << bit)) != 0) {
                ++stats_.requests[bit];
            }
        }
    }
    void clear(uint32_t bits) { status_ &= ~(bits & 0x7FFu); }
    bool pending() const { return (status_ & mask_ & 0x7FFu) != 0; }
    uint32_t status() const { return status_; }
    uint32_t mask() const { return mask_; }
    const InterruptStats& stats() const { return stats_; }

    bool read32(uint32_t address, uint32_t& value) const;
    bool write32(uint32_t address, uint32_t value);

private:
    uint32_t status_ = 0;
    uint32_t mask_ = 0;
    InterruptStats stats_{};
};

class Timers {
public:
    explicit Timers(Interrupts& interrupts);

    void reset();
    void tick(uint32_t cycles);
    void tickHblank();
    bool read32(uint32_t address, uint32_t& value) const;
    bool write32(uint32_t address, uint32_t value);
    uint16_t count(unsigned index) const;
    uint16_t mode(unsigned index) const;
    uint16_t target(unsigned index) const;

private:
    struct Counter {
        uint16_t count = 0;
        uint16_t target = 0;
        uint16_t mode = 0;
        bool irqActive = false;
        bool equal = false;
        bool overflow = false;
        bool oneShotDone = false;
        uint32_t prescale = 0;
    };

    static bool counterIndex(uint32_t address, unsigned& index, uint32_t& offset);
    void stepCounter(unsigned index);
    void triggerIrq(unsigned index);

    std::array<Counter, 3> counters_{};
    Interrupts& interrupts_;
};

class System final : public IoDevice {
public:
    explicit System(Memory& memory);
    ~System() override;

    void reset();
    void bind(Cpu& cpu);
    void tick(uint32_t cycles);

    bool read8(uint32_t address, uint8_t& value, MemoryFault* fault) override;
    bool read16(uint32_t address, uint16_t& value, MemoryFault* fault) override;
    bool read32(uint32_t address, uint32_t& value, MemoryFault* fault) override;
    bool write8(uint32_t address, uint8_t value, MemoryFault* fault) override;
    bool write16(uint32_t address, uint16_t value, MemoryFault* fault) override;
    bool write32(uint32_t address, uint32_t value, MemoryFault* fault) override;
    bool irqPending() const override;

    Interrupts& interrupts() { return interrupts_; }
    const Interrupts& interrupts() const { return interrupts_; }
    Timers& timers() { return timers_; }
    const Timers& timers() const { return timers_; }
    Gpu& gpu() { return gpu_; }
    const Gpu& gpu() const { return gpu_; }
    Dma& dma() { return dma_; }
    const Dma& dma() const { return dma_; }
    Spu& spu() { return spu_; }
    const Spu& spu() const { return spu_; }
    Cdrom& cdrom() { return cdrom_; }
    const Cdrom& cdrom() const { return cdrom_; }
    Sio0& sio0() { return sio0_; }
    const Sio0& sio0() const { return sio0_; }
    void setMemoryCard(unsigned slot, MemoryCard* card) {
        if (slot < 2) {
            cards_[slot] = card;
            sio0_.setMemoryCard(slot, card);
        }
    }
    MemoryCard* memoryCard(unsigned slot) {
        return slot < 2 ? cards_[slot] : nullptr;
    }
    const MemoryCard* memoryCard(unsigned slot) const {
        return slot < 2 ? cards_[slot] : nullptr;
    }
    void saveDirtyMemoryCards();
    void setVBlankHandler(std::function<void()> handler) { vblankHandler_ = std::move(handler); }
    void setIrqHandler(std::function<void()> handler) { irqHandler_ = std::move(handler); }
    void setPadButtons(uint16_t buttons) {
        padButtons_ = buttons;
        sio0_.setPadButtons(buttons);
        inputtrace::notePush(buttons);
    }
    // Samples the configured PadSource (if any) into the system-visible pad
    // state. Called before BIOS pad services read the state so guest code
    // polling through the BIOS sees fresh input at vblank cadence.
    void refreshPadFromSource() {
        PadSource* source = sio0_.padSource();
        if (source == nullptr) {
            return;
        }
        const PadSnapshot snapshot = source->sample();
        padButtons_ = snapshot.buttons;
        sio0_.setPadButtons(snapshot.buttons);
        sio0_.setPadSticks(snapshot.leftX, snapshot.leftY, snapshot.rightX, snapshot.rightY);
    }
    void setPadSticks(uint8_t leftX, uint8_t leftY, uint8_t rightX, uint8_t rightY) {
        sio0_.setPadSticks(leftX, leftY, rightX, rightY);
    }
    uint8_t padLeftX() const { return sio0_.padLeftX(); }
    uint8_t padLeftY() const { return sio0_.padLeftY(); }
    uint8_t padRightX() const { return sio0_.padRightX(); }
    uint8_t padRightY() const { return sio0_.padRightY(); }
    uint16_t padButtons() const { return padButtons_; }
    uint64_t vblankFrames() const { return vblankFrames_; }

private:
    static constexpr uint32_t kMiscBase = 0x1F801000u;
    static constexpr uint32_t kMiscSize = 0x70u;

    static void setFault(MemoryFault* fault, MemoryFault value);
    static uint32_t extract(uint32_t word, uint32_t address, unsigned size);
    static bool miscIndex(uint32_t address, uint32_t& offset);

    Memory& memory_;
    Interrupts interrupts_;
    Timers timers_;
    Gpu gpu_;
    Spu spu_;
    Dma dma_;
    Cdrom cdrom_;
    Sio0 sio0_;
    std::array<MemoryCard*, 2> cards_{};
    std::array<uint8_t, kMiscSize> misc_{};
    std::function<void()> vblankHandler_;
    std::function<void()> irqHandler_;
    uint32_t vblankCycles_ = 0;
    uint32_t hblankCycles_ = 0;
    uint64_t vblankFrames_ = 0;
    uint16_t padButtons_ = 0xFFFFu;
};

}  // namespace psxreborn
