#include "psxreborn/pad.hpp"

namespace psxreborn {

uint16_t xinputToPadButtons(uint16_t xinputMask) {
    uint16_t buttons = 0;
    if ((xinputMask & kXInputDpadUp) != 0) {
        buttons |= kPadUp;
    }
    if ((xinputMask & kXInputDpadDown) != 0) {
        buttons |= kPadDown;
    }
    if ((xinputMask & kXInputDpadLeft) != 0) {
        buttons |= kPadLeft;
    }
    if ((xinputMask & kXInputDpadRight) != 0) {
        buttons |= kPadRight;
    }
    if ((xinputMask & kXInputStart) != 0) {
        buttons |= kPadStart;
    }
    if ((xinputMask & kXInputBack) != 0) {
        buttons |= kPadSelect;
    }
    if ((xinputMask & kXInputLeftThumb) != 0) {
        buttons |= kPadL3;
    }
    if ((xinputMask & kXInputRightThumb) != 0) {
        buttons |= kPadR3;
    }
    if ((xinputMask & kXInputLeftShoulder) != 0) {
        buttons |= kPadL1;
    }
    if ((xinputMask & kXInputRightShoulder) != 0) {
        buttons |= kPadR1;
    }
    if ((xinputMask & kXInputA) != 0) {
        buttons |= kPadCross;
    }
    if ((xinputMask & kXInputB) != 0) {
        buttons |= kPadCircle;
    }
    if ((xinputMask & kXInputX) != 0) {
        buttons |= kPadSquare;
    }
    if ((xinputMask & kXInputY) != 0) {
        buttons |= kPadTriangle;
    }
    return buttons;
}

void PadState::reset() {
    keyboard_ = 0;
    clearController();
}

void PadState::clearController() {
    controller_ = 0;
    stick_ = 0;
    leftStickX_ = kStickCenter;
    leftStickY_ = kStickCenter;
    rightStickX_ = kStickCenter;
    rightStickY_ = kStickCenter;
}

void PadState::setKeyboardButton(uint16_t button, bool pressed) {
    if (pressed) {
        keyboard_ |= button;
    } else {
        keyboard_ &= static_cast<uint16_t>(~button);
    }
}

void PadState::setControllerButton(uint16_t button, bool pressed) {
    if (pressed) {
        controller_ |= button;
    } else {
        controller_ &= static_cast<uint16_t>(~button);
    }
}

void PadState::setXInputButtons(uint16_t xinputMask) {
    controller_ = xinputToPadButtons(xinputMask);
}

uint8_t PadState::axisToStick(int32_t value) {
    // SDL joystick axes are -32768..32767; map to DualShock 0..255 (128 = center).
    const int32_t scaled = 128 + (value / 256);
    if (scaled < 0) {
        return 0;
    }
    if (scaled > 255) {
        return 255;
    }
    return static_cast<uint8_t>(scaled);
}

void PadState::setLeftStick(int32_t x, int32_t y, int32_t deadzone) {
    leftStickX_ = axisToStick(x);
    leftStickY_ = axisToStick(y);
    stick_ = 0;
    if (deadzone <= 0) {
        deadzone = 1;
    }
    if (x <= -deadzone) {
        stick_ |= kPadLeft;
    } else if (x >= deadzone) {
        stick_ |= kPadRight;
    }
    if (y <= -deadzone) {
        stick_ |= kPadUp;
    } else if (y >= deadzone) {
        stick_ |= kPadDown;
    }
}

void PadState::setRightStick(int32_t x, int32_t y) {
    rightStickX_ = axisToStick(x);
    rightStickY_ = axisToStick(y);
}

void PadState::setSticks(uint8_t leftX, uint8_t leftY, uint8_t rightX, uint8_t rightY) {
    leftStickX_ = leftX;
    leftStickY_ = leftY;
    rightStickX_ = rightX;
    rightStickY_ = rightY;
}

void PadState::setTrigger(uint16_t button, int32_t value, int32_t threshold) {
    setControllerButton(button, value >= threshold);
}

uint16_t PadState::pressed() const {
    return static_cast<uint16_t>(keyboard_ | controller_ | stick_);
}

uint16_t PadState::buttons() const {
    return static_cast<uint16_t>(~pressed());
}

}  // namespace psxreborn
