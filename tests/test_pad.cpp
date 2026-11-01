#include <cstdint>

#include "psxreborn/pad.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace {

bool isPressed(const PadState& pad, uint16_t button) {
    return (pad.buttons() & button) == 0;
}

}  // namespace

TEST_CASE(pad_state_starts_released) {
    PadState pad;
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
    CHECK_EQ(pad.pressed(), static_cast<uint16_t>(0));
}

TEST_CASE(pad_state_keyboard_and_controller_sources_combine) {
    PadState pad;
    pad.setKeyboardButton(kPadCross, true);
    CHECK(isPressed(pad, kPadCross));
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    pad.setControllerButton(kPadCross, true);
    pad.setKeyboardButton(kPadCross, false);
    CHECK(isPressed(pad, kPadCross));
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu & ~kPadCross));

    pad.setControllerButton(kPadCross, false);
    CHECK(!isPressed(pad, kPadCross));
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
}

TEST_CASE(pad_state_left_stick_maps_to_dpad_with_deadzone) {
    PadState pad;
    pad.setLeftStick(0, 0);
    CHECK_EQ(pad.pressed(), static_cast<uint16_t>(0));

    pad.setLeftStick(PadState::kDefaultDeadzone - 1, 0);
    CHECK_EQ(pad.pressed(), static_cast<uint16_t>(0));

    pad.setLeftStick(-32768, 0);
    CHECK(isPressed(pad, kPadLeft));
    CHECK(!isPressed(pad, kPadRight));

    pad.setLeftStick(32767, 0);
    CHECK(isPressed(pad, kPadRight));
    CHECK(!isPressed(pad, kPadLeft));

    pad.setLeftStick(0, -32768);
    CHECK(isPressed(pad, kPadUp));
    CHECK(!isPressed(pad, kPadDown));

    pad.setLeftStick(0, 32767);
    CHECK(isPressed(pad, kPadDown));
    CHECK(!isPressed(pad, kPadUp));

    pad.setLeftStick(-32768, 32767);
    CHECK(isPressed(pad, kPadLeft));
    CHECK(isPressed(pad, kPadDown));
}

TEST_CASE(pad_state_stick_release_preserves_held_dpad_button) {
    PadState pad;
    pad.setControllerButton(kPadLeft, true);
    pad.setLeftStick(-32768, 0);
    CHECK(isPressed(pad, kPadLeft));

    pad.setLeftStick(0, 0);
    CHECK(isPressed(pad, kPadLeft));

    pad.setControllerButton(kPadLeft, false);
    CHECK(!isPressed(pad, kPadLeft));
}

TEST_CASE(pad_state_stick_values_map_to_dualshock_bytes) {
    PadState pad;
    CHECK_EQ(static_cast<int>(pad.leftStickX()), 128);
    CHECK_EQ(static_cast<int>(pad.leftStickY()), 128);
    CHECK_EQ(static_cast<int>(pad.rightStickX()), 128);
    CHECK_EQ(static_cast<int>(pad.rightStickY()), 128);

    pad.setLeftStick(0, 0);
    CHECK_EQ(static_cast<int>(pad.leftStickX()), 128);
    CHECK_EQ(static_cast<int>(pad.leftStickY()), 128);

    pad.setLeftStick(-32768, -32768);
    CHECK_EQ(static_cast<int>(pad.leftStickX()), 0);
    CHECK_EQ(static_cast<int>(pad.leftStickY()), 0);

    pad.setLeftStick(32767, 32767);
    CHECK_EQ(static_cast<int>(pad.leftStickX()), 255);
    CHECK_EQ(static_cast<int>(pad.leftStickY()), 255);

    pad.setRightStick(-32768, 32767);
    CHECK_EQ(static_cast<int>(pad.rightStickX()), 0);
    CHECK_EQ(static_cast<int>(pad.rightStickY()), 255);

    pad.setSticks(10, 20, 30, 40);
    CHECK_EQ(static_cast<int>(pad.leftStickX()), 10);
    CHECK_EQ(static_cast<int>(pad.leftStickY()), 20);
    CHECK_EQ(static_cast<int>(pad.rightStickX()), 30);
    CHECK_EQ(static_cast<int>(pad.rightStickY()), 40);

    pad.clearController();
    CHECK_EQ(static_cast<int>(pad.leftStickX()), 128);
    CHECK_EQ(static_cast<int>(pad.rightStickY()), 128);
}

TEST_CASE(pad_state_triggers_use_threshold) {
    PadState pad;
    pad.setTrigger(kPadL2, PadState::kDefaultTriggerThreshold - 1);
    CHECK(!isPressed(pad, kPadL2));

    pad.setTrigger(kPadL2, PadState::kDefaultTriggerThreshold);
    CHECK(isPressed(pad, kPadL2));

    pad.setTrigger(kPadL2, 0);
    CHECK(!isPressed(pad, kPadL2));

    pad.setTrigger(kPadR2, 32767);
    CHECK(isPressed(pad, kPadR2));
}

TEST_CASE(pad_state_maps_xinput_digital_buttons) {
    struct Mapping {
        uint16_t xinput;
        uint16_t pad;
    };
    const Mapping mappings[] = {
        {kXInputDpadUp, kPadUp},
        {kXInputDpadDown, kPadDown},
        {kXInputDpadLeft, kPadLeft},
        {kXInputDpadRight, kPadRight},
        {kXInputStart, kPadStart},
        {kXInputBack, kPadSelect},
        {kXInputLeftThumb, kPadL3},
        {kXInputRightThumb, kPadR3},
        {kXInputLeftShoulder, kPadL1},
        {kXInputRightShoulder, kPadR1},
        {kXInputA, kPadCross},
        {kXInputB, kPadCircle},
        {kXInputX, kPadSquare},
        {kXInputY, kPadTriangle},
    };

    for (const Mapping& mapping : mappings) {
        PadState pad;
        pad.setXInputButtons(mapping.xinput);
        CHECK_EQ(pad.pressed(), mapping.pad);
        CHECK_EQ(pad.buttons(), static_cast<uint16_t>(~mapping.pad));
    }
}

TEST_CASE(pad_state_xinput_combination_and_replacement) {
    PadState pad;
    pad.setXInputButtons(static_cast<uint16_t>(kXInputA | kXInputLeftShoulder));
    CHECK_EQ(pad.pressed(), static_cast<uint16_t>(kPadCross | kPadL1));
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xBBFFu));

    pad.setXInputButtons(kXInputDpadUp);
    CHECK_EQ(pad.pressed(), kPadUp);
    CHECK(!isPressed(pad, kPadCross));
    CHECK(!isPressed(pad, kPadL1));

    pad.setXInputButtons(0);
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
}

TEST_CASE(pad_state_clear_controller_keeps_keyboard_input) {
    PadState pad;
    pad.setKeyboardButton(kPadStart, true);
    pad.setControllerButton(kPadCircle, true);
    pad.setLeftStick(-32768, 0);
    pad.clearController();
    CHECK(isPressed(pad, kPadStart));
    CHECK(!isPressed(pad, kPadCircle));
    CHECK(!isPressed(pad, kPadLeft));
    CHECK_EQ(pad.pressed(), kPadStart);
}

TEST_CASE(pad_state_reset_releases_everything) {
    PadState pad;
    pad.setKeyboardButton(kPadStart, true);
    pad.setControllerButton(kPadCircle, true);
    pad.setLeftStick(-32768, 0);
    pad.reset();
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
}

TEST_CASE(pad_state_cross_press_release_roundtrip) {
    PadState pad;
    // Idle: all active-low bits high.
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));

    // Press Cross (XInput A -> PS1 Cross, bit 14).
    pad.setXInputButtons(kXInputA);
    CHECK(isPressed(pad, kPadCross));
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu & ~kPadCross));
    CHECK_EQ(static_cast<uint8_t>(pad.buttons() >> 8), static_cast<uint8_t>(0xBFu));

    // Release: bit returns to 1.
    pad.setXInputButtons(0);
    CHECK(!isPressed(pad, kPadCross));
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
    CHECK_EQ(static_cast<uint8_t>(pad.buttons() >> 8), static_cast<uint8_t>(0xFFu));
}

TEST_CASE(pad_state_start_select_roundtrip) {
    PadState pad;
    pad.setXInputButtons(kXInputStart | kXInputBack);
    CHECK(isPressed(pad, kPadStart));
    CHECK(isPressed(pad, kPadSelect));
    // Start bit 3 and Select bit 0 both live in the low byte: 0xF6.
    CHECK_EQ(static_cast<uint8_t>(pad.buttons()), static_cast<uint8_t>(0xF6u));

    pad.setXInputButtons(0);
    CHECK(!isPressed(pad, kPadStart));
    CHECK(!isPressed(pad, kPadSelect));
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
}
