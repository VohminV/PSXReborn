#include "psxreborn/sio0.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "psxreborn/hardware.hpp"
#include "psxreborn/input_trace.hpp"

namespace psxreborn {

namespace {


constexpr uint32_t kData = 0x1F801040u;
constexpr uint32_t kStatus = 0x1F801044u;
constexpr uint32_t kMode = 0x1F801048u;
constexpr uint32_t kControl = 0x1F80104Au;
constexpr uint32_t kBaud = 0x1F80104Eu;

constexpr uint8_t kPadAddress = 0x01u;
constexpr uint8_t kCardAddress = 0x81u;
constexpr uint8_t kReadCommand = 0x52u;
constexpr uint8_t kGetIdCommand = 0x53u;
constexpr uint8_t kWriteCommand = 0x57u;

constexpr uint8_t kIdle = 0xFFu;
constexpr uint8_t kId1 = 0x5Au;
constexpr uint8_t kId2 = 0x5Du;
constexpr uint8_t kAck1 = 0x5Cu;
constexpr uint8_t kAck2 = 0x5Du;
constexpr uint8_t kGood = 0x47u;
constexpr uint8_t kBadChecksum = 0x4Eu;
constexpr uint8_t kBadSector = 0xFFu;
constexpr uint32_t kAckClearCycles = 130u;

}  // namespace

Sio0::Sio0(Interrupts& interrupts) : interrupts_(interrupts) {}

void Sio0::reset() {
    device_ = Device::None;
    command_ = Command::None;
    selectedSlot_ = 0;
    response_ = kIdle;
    receive_ = kIdle;
    lastRx_ = kIdle;
    clearRx();
    irq_ = false;
    ackAsserted_ = false;
    ackClearDelay_ = 0;
    mode_ = 0;
    control_ = 0;
    baud_ = 0;
    directoryFlag_ = 0x08u;
    commandTicks_ = 0;
    sector_ = 0;
    dataOffset_ = 0;
    checksum_ = 0;
    incomingChecksum_ = 0;
    writeChecksumValid_ = false;
    std::memset(writeBuffer_, 0, sizeof(writeBuffer_));
    transferCount_ = 0;
    padDevice_.reset();
}

void Sio0::tick(uint32_t cycles) {
    if (ackClearDelay_ == 0) {
        return;
    }
    if (ackClearDelay_ > cycles) {
        ackClearDelay_ -= cycles;
        return;
    }
    ackClearDelay_ = 0;
    ackAsserted_ = false;
    if ((control_ & 0x1000u) == 0) {
        return;
    }
    irq_ = true;
    interrupts_.request(Interrupts::kPad);
    static const bool trace = std::getenv("PSXREBORN_TRACE_SIO") != nullptr;
    if (trace) {
        std::fprintf(stderr, "[SIO] ack cleared -> IRQ\n");
    }
}

void Sio0::setMemoryCard(unsigned slot, MemoryCard* card) {
    if (slot < 2) {
        cards_[slot] = card;
    }
    if (device_ == Device::MemoryCard && selectedSlot_ == slot) {
        deselect();
    }
}

void Sio0::deselect() {
    device_ = Device::None;
    command_ = Command::None;
    response_ = kIdle;
    receive_ = kIdle;
    lastRx_ = kIdle;
    clearRx();
    ackAsserted_ = false;
    ackClearDelay_ = 0;
    commandTicks_ = 0;
    padDevice_.deselect();
    dataOffset_ = 0;
    writeChecksumValid_ = false;
}

MemoryCard* Sio0::card() const {
    return selectedSlot_ < 2 ? cards_[selectedSlot_] : nullptr;
}

bool Sio0::rxThresholdReached() const {
    std::size_t threshold = 1;
    switch ((control_ >> 8u) & 0x0003u) {
        case 0:
            threshold = 1;
            break;
        case 1:
            threshold = 2;
            break;
        case 2:
            threshold = 4;
            break;
        default:
            threshold = 8;
            break;
    }
    return rxCount_ >= threshold;
}

void Sio0::pushRx(uint8_t value) {
    lastRx_ = value;
    if (rxCount_ < kRxFifoSize) {
        rxFifo_[(rxHead_ + rxCount_) % kRxFifoSize] = value;
        ++rxCount_;
        return;
    }
    rxFifo_[(rxHead_ + kRxFifoSize - 1) % kRxFifoSize] = value;
}

uint8_t Sio0::popRx() {
    if (rxCount_ == 0) {
        return lastRx_;
    }
    const uint8_t value = rxFifo_[rxHead_];
    rxHead_ = (rxHead_ + 1) % kRxFifoSize;
    --rxCount_;
    return value;
}

void Sio0::clearRx() {
    rxHead_ = 0;
    rxCount_ = 0;
}

void Sio0::requestIrq() {
    // The /ACK interrupt is latched when the device releases /ACK after the
    // transfer delay (see tick()); it must not fire synchronously with the
    // byte transfer. Only the RX FIFO threshold can raise an IRQ immediately.
    const bool rxIrq = (control_ & 0x0800u) != 0 && rxThresholdReached();
    if (rxIrq) {
        irq_ = true;
        interrupts_.request(Interrupts::kPad);
    }
}

uint16_t Sio0::status() const {
    uint16_t value = 0x0005u;
    if (rxCount_ != 0) {
        value |= 0x0002u;
    }
    if (irq_) {
        value |= 0x0200u;
    }
    if (ackAsserted_) {
        value |= 0x0080u;
    }
    return value;
}

uint8_t Sio0::transfer(uint8_t value) {
    static const bool trace = std::getenv("PSXREBORN_TRACE_SIO") != nullptr;
    if (trace) {
        std::fprintf(stderr,
                     "[SIO] tx=%02X dev=%d ticks=%u ctrl=%04X stat=%04X PAD state=0x%04X\n",
                     value, static_cast<int>(device_), padDevice_.ticks(), control_, status(),
                     padButtons_);
    }
    ++transferCount_;
    if ((control_ & 0x0001u) == 0) {
        receive_ = kIdle;
        return kIdle;
    }
    // Device deselection is driven solely by JOY_CTRL SELECT (bit 1) edges.
    // Do not treat a 0x01 data byte as an implicit re-select: that value can
    // legitimately appear inside DualShock command payloads.
    if (device_ == Device::None) {
        selectDevice(value);
        receive_ = kIdle;
        pushRx(receive_);
        requestIrq();
        if (ackAsserted_) {
            ackClearDelay_ = kAckClearCycles;
        }
        return kIdle;
    }
    if (device_ == Device::Pad) {
        response_ = padDevice_.transfer(value);
        receive_ = response_;
        inputtrace::noteTxByte(padDevice_.lastCommand(),
                               padDevice_.responseIndex() > 0
                                   ? padDevice_.responseIndex() - 1u
                                   : 0u,
                               receive_);
        pushRx(receive_);
        // DualShock /ACK is asserted after every byte of a transaction except
        // the final one. The device advances its response index once per data
        // byte after the address byte; when it reaches the response length the
        // last response byte has been delivered and /ACK must stay released.
        ackAsserted_ = padDevice_.ackPending();
    } else {
        const uint8_t previous = response_;
        const bool writeEcho = command_ == Command::Write && commandTicks_ >= 3;
        response_ = transferCard(value);
        receive_ = writeEcho ? response_ : previous;
        pushRx(receive_);
        ackAsserted_ = device_ == Device::MemoryCard;
    }
    if ((control_ & 0x0004u) != 0) {
        control_ &= static_cast<uint16_t>(~0x0004u);
    }
    requestIrq();
    if (ackAsserted_) {
        ackClearDelay_ = kAckClearCycles;
    }
    return receive_;
}

void Sio0::selectDevice(uint8_t addressByte) {
    selectedSlot_ = (control_ & 0x2000u) != 0 ? 1u : 0u;
    if ((control_ & 0x0002u) == 0) {
        deselect();
        return;
    }
    if (addressByte == kPadAddress && selectedSlot_ == padSlot_) {
        device_ = Device::Pad;
        response_ = kIdle;
        // Sample the host input source at pad select: the transaction must
        // reflect the state at the moment the guest starts polling, not the
        // state from the last host frame.
        if (padSource_ != nullptr) {
            const PadSnapshot snapshot = padSource_->sample();
            padButtons_ = snapshot.buttons;
            padLeftX_ = snapshot.leftX;
            padLeftY_ = snapshot.leftY;
            padRightX_ = snapshot.rightX;
            padRightY_ = snapshot.rightY;
        }
        padDevice_.deselect();
        padDevice_.setInput(
            PadSnapshot{padButtons_, padLeftX_, padLeftY_, padRightX_, padRightY_},
            selectedSlot_);
        ackAsserted_ = true;
    } else if (addressByte == kCardAddress && card() != nullptr) {
        device_ = Device::MemoryCard;
        command_ = Command::None;
        commandTicks_ = 0;
        response_ = directoryFlag_;
        ackAsserted_ = true;
    } else {
        deselect();
    }
}

uint8_t Sio0::transferCard(uint8_t value) {
    if (command_ == Command::None) {
        switch (value) {
            case kReadCommand:
                command_ = Command::Read;
                break;
            case kWriteCommand:
                command_ = Command::Write;
                break;
            case kGetIdCommand:
                command_ = Command::GetId;
                break;
            default:
                deselect();
                return kIdle;
        }
        commandTicks_ = 0;
    }

    switch (command_) {
        case Command::Read: {
            switch (commandTicks_) {
                case 0:
                    ++commandTicks_;
                    return kId1;
                case 1:
                    ++commandTicks_;
                    return kId2;
                case 2:
                    ++commandTicks_;
                    return 0x00u;
                case 3:
                    sector_ = static_cast<uint16_t>(value) << 8u;
                    ++commandTicks_;
                    return value;
                case 4:
                    sector_ = static_cast<uint16_t>(sector_ | value);
                    dataOffset_ = static_cast<uint32_t>(sector_) * MemoryCard::kFrameSize;
                    checksum_ = static_cast<uint8_t>((sector_ >> 8u) ^ (sector_ & 0xFFu));
                    ++commandTicks_;
                    return kAck1;
                case 5:
                    ++commandTicks_;
                    return kAck2;
                case 6:
                    ++commandTicks_;
                    return static_cast<uint8_t>(sector_ >> 8u);
                case 7:
                    ++commandTicks_;
                    return static_cast<uint8_t>(sector_ & 0xFFu);
                default: {
                    if (commandTicks_ >= 8 && commandTicks_ <= 135) {
                        uint8_t data = kBadSector;
                        MemoryCard* selected = card();
                        if (selected != nullptr && sector_ < MemoryCard::kSectorCount &&
                            selected->readBytes(dataOffset_, &data, 1)) {
                            ++dataOffset_;
                            checksum_ ^= data;
                        }
                        ++commandTicks_;
                        return data;
                    }
                    if (commandTicks_ == 136) {
                        ++commandTicks_;
                        return checksum_;
                    }
                    if (commandTicks_ == 137) {
                        ++commandTicks_;
                        return kGood;
                    }
                    ++commandTicks_;
                    return kAck1;
                }
            }
        }
        case Command::Write: {
            switch (commandTicks_) {
                case 0:
                    ++commandTicks_;
                    return kId1;
                case 1:
                    ++commandTicks_;
                    return kId2;
                case 2:
                    ++commandTicks_;
                    return 0x00u;
                case 3:
                    sector_ = static_cast<uint16_t>(value) << 8u;
                    ++commandTicks_;
                    return value;
                case 4:
                    sector_ = static_cast<uint16_t>(sector_ | value);
                    dataOffset_ = 0;
                    checksum_ = static_cast<uint8_t>((sector_ >> 8u) ^ (sector_ & 0xFFu));
                    std::memset(writeBuffer_, 0, sizeof(writeBuffer_));
                    ++commandTicks_;
                    return value;
                default:
                    if (commandTicks_ >= 5 && commandTicks_ <= 132) {
                        writeBuffer_[dataOffset_++] = value;
                        checksum_ ^= value;
                        ++commandTicks_;
                        return value;
                    }
                    if (commandTicks_ == 133) {
                        const uint8_t expected = checksum_;
                        incomingChecksum_ = value;
                        writeChecksumValid_ = incomingChecksum_ == expected &&
                                              sector_ < MemoryCard::kSectorCount &&
                                              card() != nullptr;
                        ++commandTicks_;
                        return expected;
                    }
                    if (commandTicks_ == 134) {
                        ++commandTicks_;
                        return writeChecksumValid_ ? kAck1 : kBadChecksum;
                    }
                    if (commandTicks_ == 135) {
                        ++commandTicks_;
                        return kAck2;
                    }
                    if (commandTicks_ == 136) {
                        ++commandTicks_;
                        if (!writeChecksumValid_) {
                            return kBadChecksum;
                        }
                        MemoryCard* selected = card();
                        if (selected == nullptr ||
                            !selected->writeBytes(static_cast<uint32_t>(sector_) *
                                                      MemoryCard::kFrameSize,
                                                  writeBuffer_, MemoryCard::kFrameSize)) {
                            return kBadSector;
                        }
                        directoryFlag_ = 0x00u;
                        return kGood;
                    }
                    return kAck1;
            }
        }
        case Command::GetId:
            switch (commandTicks_) {
                case 0:
                    ++commandTicks_;
                    return kId1;
                case 1:
                    ++commandTicks_;
                    return kId2;
                case 2:
                    ++commandTicks_;
                    return kAck1;
                case 3:
                    ++commandTicks_;
                    return kAck2;
                case 4:
                    ++commandTicks_;
                    return 0x04u;
                case 5:
                case 6:
                    ++commandTicks_;
                    return 0x00u;
                case 7:
                    ++commandTicks_;
                    return 0x80u;
                default:
                    ++commandTicks_;
                    return kIdle;
            }
        case Command::None:
        default:
            return kIdle;
    }
}

void Sio0::writeControl(uint16_t value) {
    static const bool trace = std::getenv("PSXREBORN_TRACE_SIO") != nullptr;
    if (trace) {
        std::fprintf(stderr, "[SIO] ctrl=%04X (prev=%04X irq=%d ack=%d)\n", value, control_,
                     irq_ ? 1 : 0, ackAsserted_ ? 1 : 0);
    }
    const uint16_t previous = control_;
    const bool acknowledge = (value & 0x0010u) != 0;
    control_ = static_cast<uint16_t>(value & 0x3F7Fu);

    if (acknowledge) {
        irq_ = false;
    }
    if ((value & 0x0040u) != 0) {
        reset();
        return;
    }
    if ((previous & 0x0002u) == 0 && (control_ & 0x0002u) != 0) {
        deselect();
    }
    if ((previous & 0x0002u) != 0 && (control_ & 0x0002u) == 0) {
        deselect();
    }
    if ((previous & 0x2000u) != (control_ & 0x2000u) && (control_ & 0x0002u) != 0) {
        deselect();
    }
    if (acknowledge) {
        requestIrq();
    }
}

bool Sio0::read8(uint32_t address, uint8_t& value) {
    if (!handles(address)) {
        return false;
    }
    switch (address) {
        case kData: {
            static const bool trace = std::getenv("PSXREBORN_TRACE_SIO") != nullptr;
            value = popRx();
            if (trace) {
                std::fprintf(stderr, "[SIO] rx=%02X count=%zu\n", value, rxCount_);
            }
            return true;
        }
        case kStatus:
            value = static_cast<uint8_t>(status() & 0xFFu);
            {
                static const bool trace = std::getenv("PSXREBORN_TRACE_SIO") != nullptr;
                if (trace) {
                    std::fprintf(stderr, "[SIO] stat_lo=%02X irq=%d ack=%d rx=%zu\n",
                                 value, irq_ ? 1 : 0, ackAsserted_ ? 1 : 0, rxCount_);
                }
            }
            return true;
        case kStatus + 1:
            value = static_cast<uint8_t>((status() >> 8u) & 0xFFu);
            return true;
        case kMode:
            value = static_cast<uint8_t>(mode_ & 0xFFu);
            return true;
        case kMode + 1:
            value = static_cast<uint8_t>((mode_ >> 8u) & 0xFFu);
            return true;
        case kControl:
            value = static_cast<uint8_t>(control_ & 0xFFu);
            return true;
        case kControl + 1:
            value = static_cast<uint8_t>((control_ >> 8u) & 0xFFu);
            return true;
        case kBaud:
            value = static_cast<uint8_t>(baud_ & 0xFFu);
            return true;
        case kBaud + 1:
            value = static_cast<uint8_t>((baud_ >> 8u) & 0xFFu);
            return true;
        default:
            value = 0;
            return true;
    }
}

bool Sio0::read16(uint32_t address, uint16_t& value) {
    if (!handles(address)) {
        return false;
    }
    switch (address) {
        case kData:
            value = static_cast<uint16_t>(popRx());
            value = static_cast<uint16_t>(value | (static_cast<uint16_t>(popRx()) << 8u));
            return true;
        case kMode:
            value = mode_;
            return true;
        case kControl:
            value = control_;
            return true;
        case kBaud:
            value = baud_;
            return true;
        default: {
            uint8_t low = 0;
            uint8_t high = 0;
            read8(address, low);
            read8(address + 1, high);
            value = static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u));
            return true;
        }
    }
}

bool Sio0::read32(uint32_t address, uint32_t& value) {
    if (!handles(address)) {
        return false;
    }
    if (address == kData) {
        value = static_cast<uint32_t>(popRx());
        value |= static_cast<uint32_t>(popRx()) << 8u;
        value |= static_cast<uint32_t>(popRx()) << 16u;
        value |= static_cast<uint32_t>(popRx()) << 24u;
        return true;
    }
    uint16_t low = 0;
    uint16_t high = 0;
    if (!read16(address, low) || !read16(address + 2, high)) {
        return false;
    }
    value = static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16u);
    return true;
}

bool Sio0::write8(uint32_t address, uint8_t value) {
    if (!handles(address)) {
        return false;
    }
    switch (address) {
        case kData:
            transfer(value);
            return true;
        case kStatus:
        case kStatus + 1:
            return true;
        case kMode:
            mode_ = static_cast<uint16_t>((mode_ & 0xFF00u) | value);
            return true;
        case kMode + 1:
            mode_ = static_cast<uint16_t>((mode_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8u));
            return true;
        case kControl:
            writeControl(static_cast<uint16_t>((control_ & 0xFF00u) | value));
            return true;
        case kControl + 1:
            writeControl(static_cast<uint16_t>((control_ & 0x00FFu) |
                                               (static_cast<uint16_t>(value) << 8u)));
            return true;
        case kBaud:
            baud_ = static_cast<uint16_t>((baud_ & 0xFF00u) | value);
            return true;
        case kBaud + 1:
            baud_ = static_cast<uint16_t>((baud_ & 0x00FFu) | (static_cast<uint16_t>(value) << 8u));
            return true;
        default:
            return true;
    }
}

bool Sio0::write16(uint32_t address, uint16_t value) {
    if (!handles(address)) {
        return false;
    }
    switch (address) {
        case kMode:
            mode_ = value;
            return true;
        case kControl:
            writeControl(value);
            return true;
        case kBaud:
            baud_ = value;
            return true;
        case kData:
            write8(kData, static_cast<uint8_t>(value & 0xFFu));
            return true;
        default: {
            write8(address, static_cast<uint8_t>(value & 0xFFu));
            write8(address + 1, static_cast<uint8_t>((value >> 8u) & 0xFFu));
            return true;
        }
    }
}

bool Sio0::write32(uint32_t address, uint32_t value) {
    return write16(address, static_cast<uint16_t>(value & 0xFFFFu)) &&
           write16(address + 2, static_cast<uint16_t>((value >> 16u) & 0xFFFFu));
}

}  // namespace psxreborn
