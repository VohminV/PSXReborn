#include "psxreborn/ps1_pad_device.hpp"

#include "psxreborn/input_trace.hpp"

namespace psxreborn {

namespace {

constexpr uint8_t kIdle = 0xFFu;

}  // namespace

void Ps1PadDevice::reset() {
    deselect();
    response_.fill(0);
    configMode_ = false;
    analogMode_ = false;
    configCommandsUsed_ = false;
}

void Ps1PadDevice::deselect() {
    ticks_ = 0;
    responseLen_ = 0;
    responseIndex_ = 0;
    lastCommand_ = 0;
}

uint8_t Ps1PadDevice::transfer(uint8_t value) {
    ++ticks_;
    if (ticks_ == 1) {
        // Common prologue of the response build (mirrors the historical
        // preparePadResponse order): clear state, latch the trace event, then
        // let the device fill the buffer.
        lastCommand_ = value;
        response_.fill(0);
        responseLen_ = 0;
        responseIndex_ = 0;
        inputtrace::noteTx(value, input_.buttons, slot_);
        prepareResponse(value);
        if (responseLen_ == 0) {
            // Unknown command: reply 0xFF, stay selected, no ACK.
            return kIdle;
        }
        return response_[0];
    }
    if (ticks_ == 3) {
        latchDataByte(lastCommand_, value);
    }
    if (responseIndex_ < responseLen_) {
        return response_[responseIndex_++];
    }
    return kIdle;
}

void Ps1PadDevice::prepareResponse(uint8_t command) {
    if (command == 0x42u) {
        prepareDigitalRead();
        return;
    }
    responseLen_ = 0;
}

void Ps1PadDevice::latchDataByte(uint8_t /*command*/, uint8_t /*value*/) {}

void Ps1PadDevice::prepareDigitalRead() {
    response_[0] = 0x41u;
    response_[1] = 0x5Au;
    response_[2] = static_cast<uint8_t>(input_.buttons & 0xFFu);
    response_[3] = static_cast<uint8_t>((input_.buttons >> 8u) & 0xFFu);
    responseLen_ = 4;
    responseIndex_ = 1;
    inputtrace::chainLine("SIO0", "response cmd=42 id=41 pad=%04X bytes=41 5A %02X %02X",
                          input_.buttons, response_[2], response_[3]);
}

void DualShockDevice::prepareResponse(uint8_t command) {
    const uint16_t buttons = input_.buttons;
    switch (command) {
        case 0x42u: {  // Read
            if (analogMode_ || configMode_) {
                response_[0] = configMode_ ? 0xF3u : 0x73u;
                response_[1] = 0x5Au;
                response_[2] = static_cast<uint8_t>(buttons & 0xFFu);
                response_[3] = static_cast<uint8_t>((buttons >> 8u) & 0xFFu);
                response_[4] = input_.rightX;
                response_[5] = input_.rightY;
                response_[6] = input_.leftX;
                response_[7] = input_.leftY;
                responseLen_ = 8;
                responseIndex_ = 1;
                inputtrace::chainLine(
                    "SIO0",
                    "response cmd=42 id=%02X pad=%04X bytes=%02X 5A %02X %02X %02X %02X %02X %02X",
                    response_[0], buttons, response_[0], response_[2], response_[3],
                    response_[4], response_[5], response_[6], response_[7]);
            } else {
                prepareDigitalRead();
            }
            break;
        }
        case 0x43u: {  // SetConfigMode
            configCommandsUsed_ = true;
            if (configMode_) {
                response_[0] = 0xF3u;
                response_[1] = 0x5Au;
                responseLen_ = 8;
                responseIndex_ = 1;
            } else if (analogMode_) {
                response_[0] = 0x73u;
                response_[1] = 0x5Au;
                response_[2] = static_cast<uint8_t>(buttons & 0xFFu);
                response_[3] = static_cast<uint8_t>((buttons >> 8u) & 0xFFu);
                response_[4] = input_.rightX;
                response_[5] = input_.rightY;
                response_[6] = input_.leftX;
                response_[7] = input_.leftY;
                responseLen_ = 8;
                responseIndex_ = 1;
            } else {
                prepareDigitalRead();
            }
            break;
        }
        case 0x44u:  // SetAnalogMode
        case 0x45u:  // GetAnalogMode
        case 0x46u:  // Unknown46
        case 0x47u:  // Unknown47
        case 0x4Cu:  // Unknown4C
        case 0x4Du:  // UnlockRumble
            if (!configMode_) {
                responseLen_ = 0;
                break;
            }
            response_[0] = 0xF3u;
            response_[1] = 0x5Au;
            responseLen_ = 8;
            responseIndex_ = 1;
            if (command == 0x45u) {
                response_[2] = 0x01u;
                response_[3] = 0x02u;
                response_[4] = analogMode_ ? 1u : 0u;
                response_[5] = 0x02u;
                response_[6] = 0x01u;
            } else if (command == 0x46u) {
                response_[4] = 0x01u;
                response_[5] = 0x02u;
                response_[6] = 0x00u;
                response_[7] = 0x0Au;
            } else if (command == 0x47u) {
                response_[4] = 0x00u;
                response_[5] = 0x00u;
                response_[6] = 0x00u;
                response_[7] = 0x00u;
            } else if (command == 0x4Cu) {
                response_[5] = 0x04u;
            }
            break;
        default:
            responseLen_ = 0;
            break;
    }
}

void DualShockDevice::latchDataByte(uint8_t command, uint8_t value) {
    // Config-mode side effects latch on the first data byte (tick 3).
    switch (command) {
        case 0x43u:
            configMode_ = value == 1u;
            if (configMode_) {
                configCommandsUsed_ = true;
            }
            break;
        case 0x44u:
            if (configMode_) {
                analogMode_ = value == 1u;
            }
            break;
        case 0x46u:
            if (configMode_) {
                if (value == 0u) {
                    response_[4] = 0x01u;
                    response_[5] = 0x02u;
                    response_[6] = 0x00u;
                    response_[7] = 0x0Au;
                } else if (value == 1u) {
                    response_[4] = 0x01u;
                    response_[5] = 0x01u;
                    response_[6] = 0x01u;
                    response_[7] = 0x14u;
                }
            }
            break;
        case 0x47u:
            if (configMode_ && value != 0u) {
                response_[4] = 0;
                response_[5] = 0;
                response_[6] = 0;
                response_[7] = 0;
            }
            break;
        case 0x4Cu:
            if (configMode_) {
                if (value == 0u) {
                    response_[5] = 0x04u;
                } else if (value == 1u) {
                    response_[5] = 0x07u;
                }
            }
            break;
        default:
            break;
    }
}

}  // namespace psxreborn
