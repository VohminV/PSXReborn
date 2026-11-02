#pragma once

#include <cstdint>
#include <string>

#include <GLFW/glfw3.h>

#include "psxreborn/controller_profile.hpp"
#include "psxreborn/pad.hpp"

namespace psxreborn {

// GLFW exposes up to 16 joystick slots (ids 0..15); iteration helper below.
inline constexpr int kMaxJoystickId = GLFW_JOYSTICK_LAST;

// GLFW initialization, input-only. Repeated calls are safe: only the first
// one initializes GLFW. GLFW needs a host platform connection (X11, Wayland,
// Win32), so when the call fails (for example headless environments) the SDL
// window and renderer keep working and only controller support is disabled.
bool glfwInputInit(std::string& error);
void glfwInputShutdown();
// True after a successful glfwInputInit and before glfwInputShutdown.
bool glfwInputActive();

// Applies an SDL gamecontrollerdb.txt-format mapping file (regular SDL pad
// database, single line entry) via glfwUpdateGamepadMappings. Reads the path
// from PSXREBORN_GAMEPAD_DB; without it, only GLFW's own built-in mapping
// generation is available. Loading (mapping not found) must not crash play.
void glfwLoadGamepadDatabaseIfRequested();

// Hotplug events captured by the callback installed in glfwInputInit and
// dispatched during glfwPollEvents. are consumed one at a time by the play
// loop.
struct JoystickEvent {
    int jid = -1;
    bool connected = false;
};
bool pollJoystickEvent(JoystickEvent& event);

// GLFW 3.3 exposes no persistent joystick GUID (glfwGetJoystickGUID only
// exists from 3.4), so the device name doubles as profile/config identity and
// a GLFW joystick id never outlives its session. Two identical devices
// therefore share one profile; this is a documented limitation.
std::string glfwJoystickIdentity(int jid);
std::string glfwJoystickDisplayName(int jid);
// Joystick presence in the GLFW input backend (named apart from GLFW's own
// glfwJoystickPresent because both appear with a using-directive).
bool glfwInputJoystickPresent(int jid);
// GLFW-side gamepad detection (device has a gamepad mapping).
bool glfwControllerIsGamepad(int jid);

// Reads the current logical state of the device on jid into a
// ControllerSnapshot:
//  - GLFW gamepad path: buttons 0..14 and axes 0..5 keep their GLFW ids;
//    sticks are scaled to the +-32767 integer range, triggers stay in
//    0..32767 with 0 = released, as SDL triggers did.
//  - GLFW raw path (no gamepad mapping for the device): GLFW joystick
//    buttons/axes/hats fill the raw fields (raw button ids 0x200 + k, hat
//    ids 0x800 + hat*4 + direction, signed raw axes), and the documented
//    XBox/DInput-style layout is transposed onto semantic GLFW ids so the
//    automatic default mapping still works for typical pads.
ControllerSnapshot readGlfwControllerSnapshot(int jid);

// Samples a GLFW joystick at pad-transaction time.
//
// The play loop polls GLFW once per host frame (window and joystick state);
// with slow host frames that quantises input latency to the frame period
// (baseline measurement with the SDL backend: ~165 ms average). This backend
// is attached to Sio0 as the PadSource: on every pad select it refreshes the
// GLFW state (throttled to kMinPollIntervalNs) and publishes it into PadState,
// so the transaction sees input that is at most one poll interval old.
//
// The play loop keeps its per-frame handling for menu navigation, hotplug and
// keyboard; this backend only overwrites the controller-derived part of
// PadState with fresher values right before the guest reads them.
//
// Controller buttons reach PadState through the active ControllerMapping
// (per-device JSON profile); without an assigned mapping the automatic
// Xbox-layout defaults are used.
class GlfwBackend : public PadSource {
public:
    static constexpr int64_t kMinPollIntervalNs = 250000;  // 250 us
    static constexpr int kNoJoystick = -1;

    explicit GlfwBackend(PadState& state) : state_(state) {}

    void setController(int jid) { joystick_ = jid; }
    int controller() const { return joystick_; }

    void setMapping(const ControllerMapping& mapping) { mapping_ = mapping; }
    const ControllerMapping& mapping() const { return mapping_; }

    // True when the last refresh read the device through GLFW's gamepad
    // mapping (false for the raw fallback path and when nothing is open).
    bool gamepadMode() const { return gamepadMode_; }

    // When suppressed, sample() returns an idle pad (all released, sticks
    // centred) - the play tool suppresses game input while its menu is open,
    // regardless of keyboard/controller state.
    void setSuppressed(bool suppressed) { suppressed_ = suppressed; }
    bool suppressed() const { return suppressed_; }

    PadSnapshot sample() override;

    // Forces a GLFW state refresh right now and applies the mapping to
    // PadState. Used by the play loop once per host frame so the mapping also
    // covers frames where no pad transaction runs; leaves lastSnapshot()
    // filled for the menu's live button test view.
    void publish();

    // Latest observed controller state, whether or not it was published yet.
    const ControllerSnapshot& lastSnapshot() const { return lastSnapshot_; }

    uint64_t pollCount() const { return polls_; }
    uint64_t refreshCount() const { return refreshes_; }

private:
    void refresh(int64_t now);

    PadState& state_;
    int joystick_ = kNoJoystick;
    bool suppressed_ = false;
    bool gamepadMode_ = false;
    int64_t lastRefreshNs_ = 0;
    ControllerMapping mapping_ = ControllerMapping::defaults();
    ControllerSnapshot lastSnapshot_{};
    uint64_t polls_ = 0;
    uint64_t refreshes_ = 0;
};

}  // namespace psxreborn
