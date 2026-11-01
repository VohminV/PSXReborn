#pragma once

#include <cstdint>

#include <SDL2/SDL.h>

#include "psxreborn/pad.hpp"

namespace psxreborn {

// Samples an SDL game controller at pad-transaction time.
//
// The play loop drains SDL events once per host frame; with slow host frames
// that quantises input latency to the frame period (measured baseline: ~165 ms
// average). This backend is attached to Sio0 as the PadSource: on every pad
// select it refreshes SDL controller state (throttled to kMinPollIntervalNs)
// and publishes it into PadState, so the transaction sees input that is at
// most one poll interval old.
//
// The play loop keeps its event-driven handling for menu navigation, hotplug
// and keyboard; this backend only overwrites the controller-derived part of
// PadState with fresher values right before the guest reads them.
class XInputBackend : public PadSource {
public:
    static constexpr int64_t kMinPollIntervalNs = 250000;  // 250 us

    explicit XInputBackend(PadState& state) : state_(state) {}

    void setController(SDL_GameController* controller) { controller_ = controller; }
    SDL_GameController* controller() const { return controller_; }

    // When suppressed, sample() returns an idle pad (all released, sticks
    // centred) - the play tool suppresses game input while its menu is open,
    // regardless of keyboard/controller state.
    void setSuppressed(bool suppressed) { suppressed_ = suppressed; }
    bool suppressed() const { return suppressed_; }

    PadSnapshot sample() override;

    uint64_t pollCount() const { return polls_; }
    uint64_t refreshCount() const { return refreshes_; }

private:
    void refresh(int64_t now);

    PadState& state_;
    SDL_GameController* controller_ = nullptr;
    bool suppressed_ = false;
    int64_t lastRefreshNs_ = 0;
    uint64_t polls_ = 0;
    uint64_t refreshes_ = 0;
};

}  // namespace psxreborn
