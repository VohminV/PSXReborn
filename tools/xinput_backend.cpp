#include "xinput_backend.hpp"

#include "psxreborn/input_trace.hpp"

namespace psxreborn {

ControllerSnapshot readControllerSnapshot(SDL_GameController* controller) {
    ControllerSnapshot snapshot;
    if (controller == nullptr) {
        return snapshot;
    }
    for (int button = 0; button <= SDL_CONTROLLER_BUTTON_TOUCHPAD; ++button) {
        if (SDL_GameControllerGetButton(controller,
                                        static_cast<SDL_GameControllerButton>(button)) != 0) {
            snapshot.setButtonPressed(static_cast<HostControl>(button), true);
        }
    }
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
        snapshot.axes[static_cast<std::size_t>(axis)] =
            SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis));
    }
    return snapshot;
}

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

void XInputBackend::publish() {
    const int64_t now = inputtrace::nowNs();
    if (controller_ == nullptr) {
        lastSnapshot_ = ControllerSnapshot{};
        state_.clearController();
        return;
    }
    refresh(now);
}

void XInputBackend::refresh(int64_t now) {
    lastRefreshNs_ = now;
    ++refreshes_;

    const PadSnapshot before = state_.snapshot();

    // Read pending evdev events and re-read button/axis state so the values
    // are current even though the play loop will not drain SDL events until
    // its next frame.
    SDL_GameControllerUpdate();
    lastSnapshot_ = readControllerSnapshot(controller_);

    // Publish the mapped PlayStation pad state. This replaces the previous
    // hard-coded switch with the active per-device mapping profile.
    applyControllerSnapshot(state_, lastSnapshot_, mapping_);

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
