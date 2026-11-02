#include "glfw_backend.hpp"
#include "psxreborn/input_trace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include <GLFW/glfw3.h>

namespace psxreborn {

// The gamepad snapshot stores GLFW button indices directly, and the shared
// controller profile uses the same ids. Home's automap regression came from
// these drifting apart; keep the contract checked at compile time.
static_assert(kHostBtnA == GLFW_GAMEPAD_BUTTON_A);
static_assert(kHostBtnB == GLFW_GAMEPAD_BUTTON_B);
static_assert(kHostBtnX == GLFW_GAMEPAD_BUTTON_X);
static_assert(kHostBtnY == GLFW_GAMEPAD_BUTTON_Y);
static_assert(kHostBtnLeftBumper == GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
static_assert(kHostBtnRightBumper == GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
static_assert(kHostBtnBack == GLFW_GAMEPAD_BUTTON_BACK);
static_assert(kHostBtnStart == GLFW_GAMEPAD_BUTTON_START);
static_assert(kHostBtnGuide == GLFW_GAMEPAD_BUTTON_GUIDE);
static_assert(kHostBtnLeftThumb == GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
static_assert(kHostBtnRightThumb == GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
static_assert(kHostBtnDpadUp == GLFW_GAMEPAD_BUTTON_DPAD_UP);
static_assert(kHostBtnDpadRight == GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
static_assert(kHostBtnDpadDown == GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
static_assert(kHostBtnDpadLeft == GLFW_GAMEPAD_BUTTON_DPAD_LEFT);

namespace {

bool g_inputActive = false;

// Hotplug events dispatched by GLFW during glfwPollEvents; buffered so the
// play loop can act on them at its own cadence.
std::vector<JoystickEvent> g_joystickEvents;

void onJoystickChanged(int jid, int event) {
    g_joystickEvents.push_back({jid, event == GLFW_CONNECTED});
}

std::string glfwErrorText(const char* fallback) {
    const char* description = nullptr;
    ::glfwGetError(&description);
    if (description != nullptr) {
        return description;
    }
    return fallback != nullptr ? fallback : "unknown GLFW error";
}

int32_t scaleStick(float value) {
    value = std::clamp(value, -1.0f, 1.0f);
    return static_cast<int32_t>(std::lround(value * 32767.0));
}

// GLFW normalizes trigger axes towards [0,1] (0 = released); foreign mappings
// may hand back the [-1,1] stick range, so clamp the low half to released.
int32_t scaleTrigger(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int32_t>(std::lround(value * 32767.0));
}

int32_t scaleSignedRawAxis(const float* axes, int index, int count) {
    if (index >= count) {
        return 0;
    }
    return scaleStick(axes[index]);
}

}  // namespace

bool glfwInputActive() {
    return g_inputActive;
}

bool glfwInputInit(std::string& error) {
    if (g_inputActive) {
        return true;
    }
    if (::glfwInit() != GLFW_TRUE) {
        error = "GLFW init failed: " + glfwErrorText(nullptr);
        return false;
    }
    g_inputActive = true;
    ::glfwSetJoystickCallback(&onJoystickChanged);
    return true;
}

void glfwInputShutdown() {
    if (!g_inputActive) {
        return;
    }
    g_inputActive = false;
    g_joystickEvents.clear();
    ::glfwTerminate();
}

void glfwLoadGamepadDatabaseIfRequested() {
    if (!g_inputActive) {
        return;
    }
    if (const char* event = std::getenv("PSXREBORN_GAMEPAD_DB")) {
        std::ifstream file(event, std::ios::binary);
        if (!file) {
            std::cerr << "[CONTROLLER] gamepad db open failed: " << event << "\n";
            return;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (::glfwUpdateGamepadMappings(buffer.str().c_str()) != GLFW_TRUE) {
            std::cerr << "[CONTROLLER] gamepad db rejected by GLFW: " << event << "\n";
        } else {
            std::cerr << "[CONTROLLER] gamepad db loaded: " << event << "\n";
        }
    }
}

bool pollJoystickEvent(JoystickEvent& event) {
    if (!g_inputActive || g_joystickEvents.empty()) {
        return false;
    }
    event = g_joystickEvents.front();
    g_joystickEvents.erase(g_joystickEvents.begin());
    return true;
}

std::string glfwJoystickDisplayName(int jid) {
    if (!g_inputActive || jid < 0) {
        return std::string();
    }
    const char* name = ::glfwGetJoystickName(jid);
    return name != nullptr ? name : "JOYSTICK";
}

std::string glfwJoystickIdentity(int jid) {
    const std::string name = glfwJoystickDisplayName(jid);
    if (name.empty()) {
        return std::string();
    }
    return "glfw:" + name;
}

bool glfwInputJoystickPresent(int jid) {
    return g_inputActive && jid >= 0 &&
           ::glfwJoystickPresent(jid) == GLFW_TRUE;
}

bool glfwControllerIsGamepad(int jid) {
    return g_inputActive && jid >= 0 &&
           ::glfwJoystickIsGamepad(jid) == GLFW_TRUE;
}

ControllerSnapshot readGlfwControllerSnapshot(int jid) {
    ControllerSnapshot snapshot;
    if (!glfwInputJoystickPresent(jid)) {
        return snapshot;
    }
    if (glfwControllerIsGamepad(jid)) {
        GLFWgamepadstate state{};
        if (::glfwGetGamepadState(jid, &state) != GLFW_TRUE) {
            std::cerr << "[CONTROLLER] gamepad state failed: "
                      << glfwErrorText("glfwGetGamepadState") << "\n";
            return snapshot;
        }
        for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
            if (state.buttons[button]) {
                snapshot.setButtonPressed(static_cast<HostControl>(button), true);
            }
        }
        snapshot.axes[0] = scaleStick(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]);
        snapshot.axes[1] = scaleStick(state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
        snapshot.axes[2] = scaleStick(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]);
        snapshot.axes[3] = scaleStick(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
        snapshot.axes[4] = scaleTrigger(state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]);
        snapshot.axes[5] = scaleTrigger(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]);
        return snapshot;
    }

    // Raw path: GLFW has no gamepad mapping for this device.
    int rawButtonCount = 0;
    int rawAxisCount = 0;
    int rawHatCount = 0;
    const unsigned char* rawButtons =
        ::glfwGetJoystickButtons(jid, &rawButtonCount);
    const float* rawAxes = ::glfwGetJoystickAxes(jid, &rawAxisCount);
    const unsigned char* rawHats = ::glfwGetJoystickHats(jid, &rawHatCount);
    if (rawButtons != nullptr) {
        const int usable = std::min(rawButtonCount,
                                    static_cast<int>(kHostRawButtonCount));
        for (int index = 0; index < usable; ++index) {
            if (rawButtons[index]) {
                snapshot.setRawButtonPressed(
                    static_cast<HostControl>(kHostRawBtnBase + index), true);
            }
        }
    }
    if (rawHats != nullptr) {
        static const unsigned char kHatDirections[4] = {
            GLFW_HAT_UP, GLFW_HAT_RIGHT, GLFW_HAT_DOWN, GLFW_HAT_LEFT};
        const int usable = std::min(rawHatCount,
                                    static_cast<int>(kHostHatCount));
        for (int hat = 0; hat < usable; ++hat) {
            for (int direction = 0; direction < 4; ++direction) {
                if ((rawHats[hat] & kHatDirections[direction]) != 0) {
                    snapshot.setRawButtonPressed(
                        static_cast<HostControl>(kHostRawHatBase + hat * 4 +
                                                 direction),
                        true);
                }
            }
        }
    }
    for (std::size_t axis = 0; axis < kHostRawAxisCount; ++axis) {
        snapshot.rawAxes[axis] = scaleSignedRawAxis(
            rawAxes, static_cast<int>(axis), rawAxisCount);
    }

    // Transpose the documented XBox/DInput-style raw layout onto the semantic
    // GLFW button and axis ids so the default mapping keeps working for pads
    // without a gamepad database entry. Ports the previous SDL hard-coded
    // joystick path from the play tool.
    auto rawPressed = [&](int index) {
        return snapshot.rawButtonPressed(
            static_cast<HostControl>(kHostRawBtnBase + index));
    };
    auto rawAxis = [&](int index) {
        return scaleSignedRawAxis(rawAxes, index, rawAxisCount);
    };
    for (int face = 0; face < 4; ++face) {
        if (rawPressed(face)) {
            snapshot.setButtonPressed(static_cast<HostControl>(face), true);
        }
    }
    if (rawPressed(4)) snapshot.setButtonPressed(kHostBtnLeftBumper, true);
    if (rawPressed(5)) snapshot.setButtonPressed(kHostBtnRightBumper, true);
    if (rawPressed(6)) snapshot.setButtonPressed(kHostBtnBack, true);
    if (rawPressed(7)) snapshot.setButtonPressed(kHostBtnStart, true);
    if (rawPressed(9)) snapshot.setButtonPressed(kHostBtnLeftThumb, true);
    if (rawPressed(10)) snapshot.setButtonPressed(kHostBtnRightThumb, true);
    snapshot.axes[0] = rawAxis(0);
    snapshot.axes[1] = rawAxis(1);
    snapshot.axes[2] = rawAxis(3);
    snapshot.axes[3] = rawAxis(4);
    snapshot.axes[4] = rawAxis(2);  // L2 trigger sits at raw axis 2
    snapshot.axes[5] = rawAxis(5);  // R2 trigger sits at raw axis 5
    if (rawAxis(6) < -16384) snapshot.setButtonPressed(kHostBtnDpadLeft, true);
    if (rawAxis(6) > 16384) snapshot.setButtonPressed(kHostBtnDpadRight, true);
    if (rawAxis(7) < -16384) snapshot.setButtonPressed(kHostBtnDpadUp, true);
    if (rawAxis(7) > 16384) snapshot.setButtonPressed(kHostBtnDpadDown, true);
    if (snapshot.rawButtonPressed(static_cast<HostControl>(kHostRawHatBase))) {
        snapshot.setButtonPressed(kHostBtnDpadUp, true);
    }
    if (snapshot.rawButtonPressed(
            static_cast<HostControl>(kHostRawHatBase + 1))) {
        snapshot.setButtonPressed(kHostBtnDpadRight, true);
    }
    if (snapshot.rawButtonPressed(
            static_cast<HostControl>(kHostRawHatBase + 2))) {
        snapshot.setButtonPressed(kHostBtnDpadDown, true);
    }
    if (snapshot.rawButtonPressed(
            static_cast<HostControl>(kHostRawHatBase + 3))) {
        snapshot.setButtonPressed(kHostBtnDpadLeft, true);
    }
    return snapshot;
}

PadSnapshot GlfwBackend::sample() {
    ++polls_;
    const int64_t now = inputtrace::nowNs();
    if (joystick_ != kNoJoystick && now - lastRefreshNs_ >= kMinPollIntervalNs) {
        refresh(now);
    }
    if (suppressed_) {
        return PadSnapshot{};
    }
    return state_.snapshot();
}

void GlfwBackend::publish() {
    const int64_t now = inputtrace::nowNs();
    if (joystick_ == kNoJoystick) {
        lastSnapshot_ = ControllerSnapshot{};
        state_.clearController();
        return;
    }
    refresh(now);
}

void GlfwBackend::refresh(int64_t now) {
    lastRefreshNs_ = now;
    ++refreshes_;

    const PadSnapshot before = state_.snapshot();

    // GLFW refreshes device state during glfwPollEvents only; poll here so
    // the pad transaction sees fresh values despite the play loop's own
    // once-per-frame GLFW poll.
    if (g_inputActive) {
        ::glfwPollEvents();
    }
    gamepadMode_ = glfwControllerIsGamepad(joystick_);
    lastSnapshot_ = readGlfwControllerSnapshot(joystick_);
    if (inputtrace::chainEnabled()) {
        if (gamepadMode_) {
            inputtrace::chainLine(
                "HOST",
                "backend=glfw mode=gamepad buttons=%08X axes=%d,%d,%d,%d "
                "triggers=%d,%d",
                lastSnapshot_.buttons, lastSnapshot_.axes[0], lastSnapshot_.axes[1],
                lastSnapshot_.axes[2], lastSnapshot_.axes[3], lastSnapshot_.axes[4],
                lastSnapshot_.axes[5]);
        } else {
            inputtrace::chainLine(
                "HOST",
                "backend=glfw mode=raw buttons=%08X raw=%016llX "
                "rawAxes=%d,%d,%d,%d,%d,%d,%d,%d",
                lastSnapshot_.buttons,
                static_cast<unsigned long long>(lastSnapshot_.rawButtons),
                lastSnapshot_.rawAxes[0], lastSnapshot_.rawAxes[1],
                lastSnapshot_.rawAxes[2], lastSnapshot_.rawAxes[3],
                lastSnapshot_.rawAxes[4], lastSnapshot_.rawAxes[5],
                lastSnapshot_.rawAxes[6], lastSnapshot_.rawAxes[7]);
        }
    }

    // Publish the mapped PlayStation pad state through the active per-device
    // mapping profile; the play tool feeds keyboard separately.
    applyControllerSnapshot(state_, lastSnapshot_, mapping_);

    if (suppressed_) {
        return;
    }
    const PadSnapshot after = state_.snapshot();
    if (after.buttons != before.buttons || after.leftX != before.leftX ||
        after.leftY != before.leftY || after.rightX != before.rightX ||
        after.rightY != before.rightY) {
        inputtrace::notePublish(after.buttons, "glfw");
    }
}

}  // namespace psxreborn
