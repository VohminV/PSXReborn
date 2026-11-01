#include "xinput_backend.hpp"

#include "psxreborn/input_trace.hpp"

namespace psxreborn {

PadSnapshot XInputBackend::sample() {
    ++polls_;
    const int64_t now = inputtrace::nowNs();
    if (controller_ != nullptr && now - lastRefreshNs_ >= kMinPollIntervalNs) {
        refresh(now);
    }
    if (suppressed_) {
        return PadSnapshot{};
    }
    return state_.snapshot();
}

void XInputBackend::refresh(int64_t now) {
    lastRefreshNs_ = now;
    ++refreshes_;

    const PadSnapshot before = state_.snapshot();

    // Read pending evdev events and re-read button/axis state so the values
    // are current even though the play loop will not drain SDL events until
    // its next frame.
    SDL_GameControllerUpdate();
    uint16_t mask = 0;
    auto pressed = [&](SDL_GameControllerButton button) {
        return SDL_GameControllerGetButton(controller_, button) != 0;
    };
    if (pressed(SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        mask |= kXInputDpadUp;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        mask |= kXInputDpadDown;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        mask |= kXInputDpadLeft;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        mask |= kXInputDpadRight;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_START)) {
        mask |= kXInputStart;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_BACK)) {
        mask |= kXInputBack;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_LEFTSTICK)) {
        mask |= kXInputLeftThumb;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) {
        mask |= kXInputRightThumb;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
        mask |= kXInputLeftShoulder;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
        mask |= kXInputRightShoulder;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_A)) {
        mask |= kXInputA;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_B)) {
        mask |= kXInputB;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_X)) {
        mask |= kXInputX;
    }
    if (pressed(SDL_CONTROLLER_BUTTON_Y)) {
        mask |= kXInputY;
    }
    state_.setXInputButtons(mask);
    state_.setLeftStick(SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTX),
                        SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_LEFTY));
    state_.setRightStick(SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_RIGHTX),
                         SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_RIGHTY));
    state_.setTrigger(kPadL2,
                      SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    state_.setTrigger(kPadR2,
                      SDL_GameControllerGetAxis(controller_, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    if (suppressed_) {
        return;
    }
    const PadSnapshot after = state_.snapshot();
    if (after.buttons != before.buttons || after.leftX != before.leftX ||
        after.leftY != before.leftY || after.rightX != before.rightX ||
        after.rightY != before.rightY) {
        inputtrace::notePublish(after.buttons, "xinput");
    }
}

}  // namespace psxreborn
