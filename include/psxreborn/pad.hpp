#pragma once

#include <cstdint>

namespace psxreborn {

// Active-high bit masks for the PlayStation digital pad button assignments.
// PadState::buttons() converts the combined state to the active-low format
// used by the SIO controller protocol.
inline constexpr uint16_t kPadSelect = 1u << 0;
inline constexpr uint16_t kPadL3 = 1u << 1;
inline constexpr uint16_t kPadR3 = 1u << 2;
inline constexpr uint16_t kPadStart = 1u << 3;
inline constexpr uint16_t kPadUp = 1u << 4;
inline constexpr uint16_t kPadRight = 1u << 5;
inline constexpr uint16_t kPadDown = 1u << 6;
inline constexpr uint16_t kPadLeft = 1u << 7;
inline constexpr uint16_t kPadL2 = 1u << 8;
inline constexpr uint16_t kPadR2 = 1u << 9;
inline constexpr uint16_t kPadL1 = 1u << 10;
inline constexpr uint16_t kPadR1 = 1u << 11;
inline constexpr uint16_t kPadTriangle = 1u << 12;
inline constexpr uint16_t kPadCircle = 1u << 13;
inline constexpr uint16_t kPadCross = 1u << 14;
inline constexpr uint16_t kPadSquare = 1u << 15;

inline constexpr uint16_t kXInputDpadUp = 0x0001;
inline constexpr uint16_t kXInputDpadDown = 0x0002;
inline constexpr uint16_t kXInputDpadLeft = 0x0004;
inline constexpr uint16_t kXInputDpadRight = 0x0008;
inline constexpr uint16_t kXInputStart = 0x0010;
inline constexpr uint16_t kXInputBack = 0x0020;
inline constexpr uint16_t kXInputLeftThumb = 0x0040;
inline constexpr uint16_t kXInputRightThumb = 0x0080;
inline constexpr uint16_t kXInputLeftShoulder = 0x0100;
inline constexpr uint16_t kXInputRightShoulder = 0x0200;
inline constexpr uint16_t kXInputA = 0x1000;
inline constexpr uint16_t kXInputB = 0x2000;
inline constexpr uint16_t kXInputX = 0x4000;
inline constexpr uint16_t kXInputY = 0x8000;

// DualShock stick byte values: 0..255 with 128 at rest.
inline constexpr uint8_t kStickCenter = 128u;

uint16_t xinputToPadButtons(uint16_t xinputMask);

// Point-in-time view of the combined host input state, in the format the SIO
// pad transaction consumes: the active-low button word plus the four stick
// bytes.
struct PadSnapshot {
    uint16_t buttons = 0xFFFFu;
    uint8_t leftX = kStickCenter;
    uint8_t leftY = kStickCenter;
    uint8_t rightX = kStickCenter;
    uint8_t rightY = kStickCenter;
};

// Source of pad state that is sampled when a controller transaction starts.
// Implementations must be cheap: sample() runs on every pad select (potentially
// hundreds of thousands of times per second) and must not block.
// Ownership stays with the caller; PadSource is not a thread-synchronisation
// primitive - the project runs input and SIO on the host main thread.
class PadSource {
public:
    virtual ~PadSource() = default;
    virtual PadSnapshot sample() = 0;
};

// Combines keyboard, controller-button, and analog-stick sources into the
// active-low pad word consumed by the SIO controller emulation.
class PadState {
public:
    static constexpr int32_t kDefaultDeadzone = 8192;
    static constexpr int32_t kDefaultTriggerThreshold = 16384;

    void reset();
    void clearController();
    void setKeyboardButton(uint16_t button, bool pressed);
    void setControllerButton(uint16_t button, bool pressed);
    void setXInputButtons(uint16_t xinputMask);
    void setLeftStick(int32_t x, int32_t y);
    void setRightStick(int32_t x, int32_t y);
    void setSticks(uint8_t leftX, uint8_t leftY, uint8_t rightX, uint8_t rightY);
    void setTrigger(uint16_t button, int32_t value,
                    int32_t threshold = kDefaultTriggerThreshold);

    uint16_t pressed() const;
    uint16_t buttons() const;
    uint8_t leftStickX() const { return leftStickX_; }
    uint8_t leftStickY() const { return leftStickY_; }
    uint8_t rightStickX() const { return rightStickX_; }
    uint8_t rightStickY() const { return rightStickY_; }

    PadSnapshot snapshot() const {
        return PadSnapshot{buttons(), leftStickX_, leftStickY_, rightStickX_, rightStickY_};
    }

private:
    static uint8_t axisToStick(int32_t value);

    uint16_t keyboard_ = 0;
    uint16_t controller_ = 0;
    uint16_t stick_ = 0;
    uint8_t leftStickX_ = kStickCenter;
    uint8_t leftStickY_ = kStickCenter;
    uint8_t rightStickX_ = kStickCenter;
    uint8_t rightStickY_ = kStickCenter;
};

}  // namespace psxreborn
