#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "psxreborn/memcard.hpp"
#include "psxreborn/ps1_pad_device.hpp"

namespace psxreborn {

class Interrupts;

// SIO0 controller and memory-card serial interface.
//
// The implementation models the byte-oriented protocol used by the
// PlayStation controller ports. A single transfer is initiated by writing
// JOY_DATA; the response byte is then available in the receive register.
// Controller port 1/2 selection follows JOY_CTRL bit 13.
//
// The pad protocol itself lives in DualShockDevice (see
// ps1_pad_device.hpp). SIO0 routes pad bytes to the device and, when a
// PadSource is attached, samples it when the pad is selected (the 0x01
// address byte) so the transaction always reflects the freshest host input.
// Without a PadSource the values pushed through setPadButtons/setPadSticks
// are used.
class Sio0 {
public:
    explicit Sio0(Interrupts& interrupts);

    void reset();
    void tick(uint32_t cycles);
    void setPadButtons(uint16_t buttons) { padButtons_ = buttons; }
    void setPadSticks(uint8_t leftX, uint8_t leftY, uint8_t rightX, uint8_t rightY) {
        padLeftX_ = leftX;
        padLeftY_ = leftY;
        padRightX_ = rightX;
        padRightY_ = rightY;
    }
    void setPadSource(PadSource* source) { padSource_ = source; }
    PadSource* padSource() const { return padSource_; }
    uint16_t padButtons() const { return padButtons_; }
    uint8_t padLeftX() const { return padLeftX_; }
    uint8_t padLeftY() const { return padLeftY_; }
    uint8_t padRightX() const { return padRightX_; }
    uint8_t padRightY() const { return padRightY_; }
    void setPadAnalogMode(bool enabled) { padDevice_.setAnalogMode(enabled); }
    void setPadSlot(unsigned slot) { padSlot_ = slot < 2 ? slot : 0; }
    unsigned padSlot() const { return padSlot_; }
    void setMemoryCard(unsigned slot, MemoryCard* card);

    bool read8(uint32_t address, uint8_t& value);
    bool read16(uint32_t address, uint16_t& value);
    bool read32(uint32_t address, uint32_t& value);
    bool write8(uint32_t address, uint8_t value);
    bool write16(uint32_t address, uint16_t value);
    bool write32(uint32_t address, uint32_t value);

    bool handles(uint32_t address) const {
        return address >= kBase && address < kBase + kSize;
    }

    uint16_t status() const;
    uint32_t transferCount() const { return transferCount_; }
    unsigned selectedSlot() const { return selectedSlot_; }
    bool cardSelected() const { return device_ == Device::MemoryCard; }
    bool irqRequested() const { return irq_; }
    bool ackAsserted() const { return ackAsserted_; }
    bool padAnalogMode() const { return padDevice_.analogMode(); }
    bool padConfigMode() const { return padDevice_.configMode(); }

private:
    static constexpr uint32_t kBase = 0x1F801040u;
    static constexpr uint32_t kSize = 0x10u;

    enum class Device {
        None,
        Pad,
        MemoryCard,
    };

    enum class Command {
        None,
        Read,
        Write,
        GetId,
    };

    static constexpr std::size_t kRxFifoSize = 8;

    uint8_t transfer(uint8_t value);
    uint8_t transferCard(uint8_t value);
    void selectDevice(uint8_t addressByte);
    void deselect();
    void writeControl(uint16_t value);
    void requestIrq();
    bool rxThresholdReached() const;
    void pushRx(uint8_t value);
    uint8_t popRx();
    void clearRx();
    MemoryCard* card() const;

    Interrupts& interrupts_;
    DualShockDevice padDevice_;
    PadSource* padSource_ = nullptr;
    uint16_t padButtons_ = 0xFFFFu;
    uint8_t padLeftX_ = 128u;
    uint8_t padLeftY_ = 128u;
    uint8_t padRightX_ = 128u;
    uint8_t padRightY_ = 128u;
    MemoryCard* cards_[2] = {nullptr, nullptr};
    Device device_ = Device::None;
    Command command_ = Command::None;
    unsigned selectedSlot_ = 0;
    unsigned padSlot_ = 0;
    uint8_t response_ = 0xFFu;
    uint8_t receive_ = 0xFFu;
    uint8_t lastRx_ = 0xFFu;
    std::array<uint8_t, kRxFifoSize> rxFifo_{};
    std::size_t rxHead_ = 0;
    std::size_t rxCount_ = 0;
    bool irq_ = false;
    bool ackAsserted_ = false;
    uint32_t ackClearDelay_ = 0;
    uint16_t mode_ = 0;
    uint16_t control_ = 0;
    uint16_t baud_ = 0;
    uint8_t directoryFlag_ = 0x08u;
    uint8_t commandTicks_ = 0;
    uint16_t sector_ = 0;
    uint32_t dataOffset_ = 0;
    uint8_t checksum_ = 0;
    uint8_t incomingChecksum_ = 0;
    bool writeChecksumValid_ = false;
    uint8_t writeBuffer_[MemoryCard::kFrameSize]{};
    uint32_t transferCount_ = 0;
};

}  // namespace psxreborn
