#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "psxreborn/pad.hpp"

namespace psxreborn {

// Host-side control identifiers used as the left side of the PS1 button mapping.
//
// Button values 0..14 mirror GLFW_GAMEPAD_BUTTON_* one to one so the GLFW
// input backend can fill the snapshot without a translation table (circles
// follow the Xbox layout: GLFW A = bottom face = PS1 cross); axis controls
// reuse the GLFW axis ordering (LX, LY, RX, RY, LT, RT) at kHostAxisBase.
// The identifiers are plain integers so the mapping stays testable without
// GLFW.
using HostControl = uint16_t;

inline constexpr HostControl kHostNone = 0xFFFFu;

inline constexpr HostControl kHostBtnA = 0;
inline constexpr HostControl kHostBtnB = 1;
inline constexpr HostControl kHostBtnX = 2;
inline constexpr HostControl kHostBtnY = 3;
inline constexpr HostControl kHostBtnLeftBumper = 4;
inline constexpr HostControl kHostBtnRightBumper = 5;
inline constexpr HostControl kHostBtnBack = 6;
inline constexpr HostControl kHostBtnStart = 7;
inline constexpr HostControl kHostBtnGuide = 8;
// Keep these in lockstep with GLFW_GAMEPAD_BUTTON_*: the gamepad backend
// stores GLFW's button index directly in ControllerSnapshot::buttons.
inline constexpr HostControl kHostBtnLeftThumb = 9;
inline constexpr HostControl kHostBtnRightThumb = 10;
inline constexpr HostControl kHostBtnDpadUp = 11;
inline constexpr HostControl kHostBtnDpadRight = 12;
inline constexpr HostControl kHostBtnDpadDown = 13;
inline constexpr HostControl kHostBtnDpadLeft = 14;
inline constexpr HostControl kHostBtnCount = 15;

inline constexpr HostControl kHostAxisBase = 0x100u;
inline constexpr HostControl kHostAxisLeftX = kHostAxisBase + 0;
inline constexpr HostControl kHostAxisLeftY = kHostAxisBase + 1;
inline constexpr HostControl kHostAxisRightX = kHostAxisBase + 2;
inline constexpr HostControl kHostAxisRightY = kHostAxisBase + 3;
inline constexpr HostControl kHostAxisTriggerLeft = kHostAxisBase + 4;
inline constexpr HostControl kHostAxisTriggerRight = kHostAxisBase + 5;
inline constexpr HostControl kHostAxisCount = 6;

// Raw device controls, filled only by the raw (non-gamepad) backend path.
// Raw controls carry the device's own button/axis/hat indexes with no
// database remapping, so a pad whose standard mapping is wrong can still
// be mapped exactly per device profile.
//
// 0x200 + k          raw button k        ("b0".."b31")
// 0x400 + axis*2+d   raw axis edge       ("a0-"/"a0+", d selects the side)
// 0x600 + k          raw bidirectional axis ("axis0".."axis7", sticks)
inline constexpr HostControl kHostRawBtnBase = 0x200u;
inline constexpr std::size_t kHostRawButtonCount = 32;
inline constexpr HostControl kHostRawAxisPairBase = 0x400u;
inline constexpr std::size_t kHostRawAxisCount = 8;
inline constexpr HostControl kHostRawAxisBase = 0x600u;
inline constexpr HostControl kHostRawHatBase = 0x800u;
inline constexpr std::size_t kHostHatCount = 8;

// Number of PS1 buttons exposed by the mapping editor. The order must match
// ps1ButtonBit()/ps1ButtonName() and mirrors the DualShock face/dpad/shoulder
// layout used by the SIO pad device.
inline constexpr std::size_t kPs1ButtonCount = 16;

// PS1 button bit (active-high pad mask) for mapping editor slot index.
uint16_t ps1ButtonBit(std::size_t index);

// Uppercase PS1 button name for editor slot index ("CROSS", "L2", ...).
const char* ps1ButtonName(std::size_t index);

// Stable lowercase token used in profile JSON ("cross", "l2", ...).
const char* ps1ButtonToken(std::size_t index);

// Human-readable, uppercase control name ("A", "DPAD_UP", "AXIS_TRIGGER_LEFT").
const char* hostControlName(HostControl control);

// Parses a control name (case-insensitive) back to a HostControl, and checks
// whether a control id participates in the editor's cycle list.
bool hostControlFromName(const std::string& name, HostControl& result);

// Every schedulable control, in editor display order (buttons then axes).
const std::vector<HostControl>& hostControlChoices();

bool hostControlIsAxis(HostControl control);
HostControl hostControlAxisIndex(HostControl control);

// Read-only view of one controller's raw state, host-side, before mapping.
// Two naming layers share these fields:
//  - semantic: buttons bit k = GLFW gamepad button id k, axes use the signed
//    ±32767 range in GLFW axis order (filled by the GLFW gamepad path);
//  - raw: rawButtons bit k = device raw button k (hats continue from bit
//    kHostRawButtonCount as four-way hat*4+direction), rawAxes carry signed
//    values for device axis k (filled by the GLFW raw backend path).
struct ControllerSnapshot {
    uint32_t buttons = 0;
    std::array<int32_t, kHostAxisCount> axes{};
    uint64_t rawButtons = 0;
    std::array<int32_t, kHostRawAxisCount> rawAxes{};

    bool buttonPressed(HostControl button) const {
        return button < 32 && (buttons & (1u << button)) != 0;
    }
    void setButtonPressed(HostControl button, bool pressed) {
        if (button >= 32) {
            return;
        }
        if (pressed) {
            buttons |= 1u << button;
        } else {
            buttons &= ~(1u << button);
        }
    }
    bool rawButtonPressed(HostControl button) const {
        // Plain buttons live at rawButtons bit (button - kHostRawBtnBase);
        // hats continue from bit kHostRawButtonCount, one bit per direction.
        if (button >= kHostRawHatBase && button < kHostRawHatBase + kHostHatCount * 4) {
            const auto bit = static_cast<uint64_t>(kHostRawButtonCount) +
                             (button - kHostRawHatBase);
            return ((rawButtons >> bit) & 1u) != 0;
        }
        if (button >= kHostRawBtnBase &&
            button < kHostRawBtnBase + kHostRawButtonCount) {
            return ((rawButtons >> (button - kHostRawBtnBase)) & 1u) != 0;
        }
        return false;
    }
    void setRawButtonPressed(HostControl button, bool pressed) {
        if (button >= kHostRawHatBase && button < kHostRawHatBase + kHostHatCount * 4) {
            const auto bit = static_cast<uint64_t>(kHostRawButtonCount) +
                             (button - kHostRawHatBase);
            if (pressed) {
                rawButtons |= 1ull << bit;
            } else {
                rawButtons &= ~(1ull << bit);
            }
        } else if (button >= kHostRawBtnBase &&
                   button < kHostRawBtnBase + kHostRawButtonCount) {
            const uint64_t mask = 1ull << (button - kHostRawBtnBase);
            if (pressed) {
                rawButtons |= mask;
            } else {
                rawButtons &= ~mask;
            }
        }
    }
};

// PS1 button -> host control translation plus stick/trigger tuning.
// The default holds the verified automatic Xbox-layout assignment so a fresh
// install starts with exactly the previously hard-coded behaviour.
struct ControllerMapping {
    static constexpr int kDefaultDeadzone = PadState::kDefaultDeadzone;
    static constexpr int kDefaultTriggerThreshold = PadState::kDefaultTriggerThreshold;

    std::array<HostControl, kPs1ButtonCount> host{};
    int deadzone = kDefaultDeadzone;
    int triggerThreshold = kDefaultTriggerThreshold;

    static ControllerMapping defaults();
};

// True when two PS1 buttons share one host control. Returns the control and
// all PS1 slots bound to it for UI reporting.
struct MappingConflict {
    HostControl control = kHostNone;
    std::vector<std::size_t> slots;
};

std::vector<MappingConflict> mappingConflicts(const ControllerMapping& mapping);

// Applies a mapped controller snapshot to the pad state. Replaces the whole
// controller-derived pad area (buttons, dpad-from-stick, stick bytes, L2/R2):
// keyboard input and any previously bound controller buttons not present in
// the mapping are released together with it, exactly like the previous
// hard-coded refresh path.
void applyControllerSnapshot(PadState& pad, const ControllerSnapshot& snapshot,
                             const ControllerMapping& mapping);

// Persistent per-device controller setup, stored as JSON under the project's
// user configuration directory:
//   <config-dir>/profiles/<sanitized-guid>.json
//
// JSON shape (format version 1):
// {
//   "format_version": 1,
//   "device_name": "Xbox 360 Controller",
//   "guid": "030000005e0400008902000010010000",
//   "mapping": {
//     "cross": "a",
//     "circle": "b",
//     "l2": "axis_trigger_left"
//   },
//   "deadzone": 8192,
//   "trigger_threshold": 16384
// }
struct ControllerProfile {
    static constexpr unsigned kFormatVersion = 1;

    unsigned formatVersion = kFormatVersion;
    std::string guid;
    std::string deviceName;
    ControllerMapping mapping = ControllerMapping::defaults();

    std::string serialize() const;
    // Parses profile text. Returns false (with error filled) for malformed
    // JSON, unknown keys, out-of-range values, or future format versions;
    // on failure the profile keeps its previous contents.
    bool parse(const std::string& json, std::string& error);

    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error);

    // Fields absent from older/partial profiles keep these defaults (forward
    // and backward migration).
    void applyMissingFields();

    static std::string profilesDirectory();
    static std::string pathForGuid(const std::string& guid);
    static std::string sanitizeGuid(const std::string& guid);
};

}  // namespace psxreborn
