#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "psxreborn/pad.hpp"

namespace psxreborn {

// Controller protocol endpoint behind SIO0 pad transactions.
//
// Owns the byte-accurate pad side of the SIO0 protocol: the per-transaction
// tick counter, the response buffer, and the DualShock configuration/analog
// mode state. SIO0 drives it one byte at a time:
//
//   setInput(snapshot, slot)  -- SIO0 samples the PadSource on pad select
//   transfer(byte)            -- one byte per JOY_DATA write
//   deselect()                -- end of transaction (JOY_CTRL select edge)
//   reset()                   -- full device reset (SIO0 reset)
//
// Ps1PadDevice implements the digital pad: command 0x42 returns ID 0x41 and
// four data bytes (0x5A, buttons lo, buttons hi). DualShockDevice extends
// that with analog reads (ID 0x73/0xF3, 8 bytes with stick data) and the
// configuration command set (0x43, 0x44, 0x45, 0x46, 0x47, 0x4C, 0x4D).
//
// Not thread-safe; called from the host main thread only.
class Ps1PadDevice {
public:
    virtual ~Ps1PadDevice() = default;

    void reset();
    void deselect();
    void setInput(const PadSnapshot& input, unsigned slot) {
        input_ = input;
        slot_ = slot;
    }

    // Handles one transferred byte. SIO0 must only route bytes here while the
    // pad is selected (after the 0x01 address byte).
    uint8_t transfer(uint8_t value);

    // State exposed to SIO0 and tracing.
    std::size_t responseIndex() const { return responseIndex_; }
    uint8_t lastCommand() const { return lastCommand_; }
    uint32_t ticks() const { return ticks_; }
    bool ackPending() const { return responseIndex_ < responseLen_; }
    bool analogMode() const { return analogMode_; }
    bool configMode() const { return configMode_; }
    void setAnalogMode(bool enabled) { analogMode_ = enabled; }

protected:
    // Fills the response buffer for the command byte (tick 1). The common
    // prologue (buffer clear, length/index reset, trace hook) already ran in
    // transfer(); a responseLen_ of zero marks an unknown command (reply 0xFF,
    // no ACK).
    virtual void prepareResponse(uint8_t command);

    // Latches command side effects on the first data byte (tick 3).
    virtual void latchDataByte(uint8_t command, uint8_t value);

    // Shared 4-byte digital read payload used by command 0x42 outside
    // config/analog mode.
    void prepareDigitalRead();

    PadSnapshot input_{};
    unsigned slot_ = 0;
    std::array<uint8_t, 8> response_{};
    std::size_t responseLen_ = 0;
    std::size_t responseIndex_ = 0;
    uint8_t lastCommand_ = 0;
    uint32_t ticks_ = 0;
    bool configMode_ = false;
    bool analogMode_ = false;
    bool configCommandsUsed_ = false;
};

// Full DualShock endpoint: digital reads plus analog/config commands. This is
// the device SIO0 instantiates for controller ports.
class DualShockDevice : public Ps1PadDevice {
protected:
    void prepareResponse(uint8_t command) override;
    void latchDataByte(uint8_t command, uint8_t value) override;
};

}  // namespace psxreborn
