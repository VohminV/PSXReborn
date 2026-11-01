#include "psxreborn/menu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace psxreborn {

namespace {

constexpr std::array<int, 5> kFpsLimits{{0, 30, 60, 120, 144}};

int wrapIndex(int value, int count) {
    if (count <= 0) {
        return 0;
    }
    int result = value % count;
    if (result < 0) {
        result += count;
    }
    return result;
}

double clampDouble(double value, double minimum, double maximum) {
    return std::max(minimum, std::min(maximum, value));
}

std::string formatBool(bool value) {
    return value ? "ON" : "OFF";
}

std::string formatPercent(double value) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(0);
    stream << value * 100.0 << "%";
    return stream.str();
}

std::string formatFixed(double value, int precision) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(precision);
    stream << value;
    return stream.str();
}

std::string pathLabel(const std::string& path) {
    if (path.empty()) {
        return "NONE";
    }
    const std::filesystem::path value(path);
    return value.filename().string();
}

RenderBackend parseBackend(const std::string& value) {
    if (value == "vulkan") {
        return RenderBackend::Vulkan;
    }
    if (value == "sdl") {
        return RenderBackend::Sdl;
    }
    return RenderBackend::Auto;
}

ScaleMode parseScale(const std::string& value) {
    if (value == "nearest") {
        return ScaleMode::Nearest;
    }
    if (value == "fsr1") {
        return ScaleMode::Fsr1;
    }
    return ScaleMode::Linear;
}

AspectMode parseAspect(const std::string& value) {
    if (value == "4:3" || value == "fourthree") {
        return AspectMode::FourThree;
    }
    if (value == "stretch") {
        return AspectMode::Stretch;
    }
    return AspectMode::Auto;
}

const char* backendKey(RenderBackend backend) {
    switch (backend) {
        case RenderBackend::Vulkan:
            return "vulkan";
        case RenderBackend::Sdl:
            return "sdl";
        case RenderBackend::Auto:
        default:
            return "auto";
    }
}

const char* scaleKey(ScaleMode scale) {
    switch (scale) {
        case ScaleMode::Nearest:
            return "nearest";
        case ScaleMode::Fsr1:
            return "fsr1";
        case ScaleMode::Linear:
        default:
            return "linear";
    }
}

const char* aspectKey(AspectMode aspect) {
    switch (aspect) {
        case AspectMode::FourThree:
            return "4:3";
        case AspectMode::Stretch:
            return "stretch";
        case AspectMode::Auto:
        default:
            return "auto";
    }
}

std::string cardSlotValue(const MenuSettings& settings, const MenuStatus& status,
                          unsigned slot) {
    if (!settings.cardEnabled(slot)) {
        return "DISCONNECTED";
    }
    if (!status.cardPresent[slot]) {
        return "NO CARD";
    }
    if (status.cardErrors[slot]) {
        return "ERROR";
    }
    std::string value = pathLabel(settings.cardPath(slot));
    if (status.cardDirty[slot]) {
        value += " *";
    }
    return value;
}

}  // namespace

const char* menuTabName(MenuTab tab) {
    switch (tab) {
        case MenuTab::Graphics:
            return "GRAPHICS";
        case MenuTab::Upscaling:
            return "UPSCALING";
        case MenuTab::Controller:
            return "CONTROLLER";
        case MenuTab::MouseCamera:
            return "MOUSE / CAMERA";
        case MenuTab::MemoryCards:
            return "MEMORY CARDS";
        case MenuTab::SaveStates:
            return "SAVE STATES";
        case MenuTab::Audio:
            return "AUDIO";
        case MenuTab::Game:
            return "GAME";
        case MenuTab::Count:
        default:
            return "";
    }
}

void MenuSettings::load(const Config& config) {
    backend = parseBackend(config.getString("video.backend", "auto"));
    scale = parseScale(config.getString("video.scale", "linear"));
    aspect = parseAspect(config.getString("video.aspect", "auto"));
    fsrEnabled = config.getBool("video.fsr", true);
    fsrSharpness = clampDouble(config.getDouble("video.fsr_sharpness", 2.0), 0.0, 2.0);
    resolutionIndex = std::clamp(config.getInt("video.resolution_index", 2), 0,
                                 static_cast<int>(kResolutionPresets.size()) - 1);
    fullscreen = config.getBool("video.fullscreen", false);
    vsync = config.getBool("video.vsync", true);
    fpsLimit = config.getInt("video.fps_limit", 60);
    bool fpsValid = false;
    for (int limit : kFpsLimits) {
        fpsValid = fpsValid || limit == fpsLimit;
    }
    if (!fpsValid) {
        fpsLimit = 60;
    }
    integerScale = config.getBool("video.integer_scale", false);
    jit = config.getBool("emulation.jit", false);
    pauseOnMenu = config.getBool("menu.pause", true);
    mouseCapture = config.getBool("input.mouse_capture", false);
    mouseSensitivity = clampDouble(config.getDouble("input.mouse_sensitivity", 1.0), 0.1, 10.0);
    invertY = config.getBool("input.invert_y", false);
    smoothing = config.getBool("input.smoothing", false);
    mute = config.getBool("audio.mute", false);
    volume = clampDouble(config.getDouble("audio.volume", 1.0), 0.0, 1.0);
    card1Enabled = config.getBool("memcard.slot1.enabled", true);
    card2Enabled = config.getBool("memcard.slot2.enabled", true);
    card1Path = config.getString("memcard.slot1.path", "");
    card2Path = config.getString("memcard.slot2.path", "");
}

void MenuSettings::save(Config& config, bool gameOverride) const {
    config.setString("video.backend", backendKey(backend), gameOverride);
    config.setString("video.scale", scaleKey(scale), gameOverride);
    config.setString("video.aspect", aspectKey(aspect), gameOverride);
    config.setBool("video.fsr", fsrEnabled, gameOverride);
    config.setDouble("video.fsr_sharpness", fsrSharpness, gameOverride);
    config.setInt("video.resolution_index", resolutionIndex, gameOverride);
    config.setBool("video.fullscreen", fullscreen, gameOverride);
    config.setBool("video.vsync", vsync, gameOverride);
    config.setInt("video.fps_limit", fpsLimit, gameOverride);
    config.setBool("video.integer_scale", integerScale, gameOverride);
    config.setBool("emulation.jit", jit, gameOverride);
    config.setBool("menu.pause", pauseOnMenu, gameOverride);
    config.setBool("input.mouse_capture", mouseCapture, gameOverride);
    config.setDouble("input.mouse_sensitivity", mouseSensitivity, gameOverride);
    config.setBool("input.invert_y", invertY, gameOverride);
    config.setBool("input.smoothing", smoothing, gameOverride);
    config.setBool("audio.mute", mute, gameOverride);
    config.setDouble("audio.volume", volume, gameOverride);
    config.setBool("memcard.slot1.enabled", card1Enabled, gameOverride);
    config.setBool("memcard.slot2.enabled", card2Enabled, gameOverride);
    config.setString("memcard.slot1.path", card1Path, gameOverride);
    config.setString("memcard.slot2.path", card2Path, gameOverride);
}

const ResolutionPreset& MenuSettings::resolution() const {
    const std::size_t index = static_cast<std::size_t>(
        std::clamp(resolutionIndex, 0, static_cast<int>(kResolutionPresets.size()) - 1));
    return kResolutionPresets[index];
}

std::string MenuSettings::cardPath(unsigned slot) const {
    return slot == 0 ? card1Path : slot == 1 ? card2Path : std::string();
}

void MenuSettings::setCardPath(unsigned slot, const std::string& path) {
    if (slot == 0) {
        card1Path = path;
    } else if (slot == 1) {
        card2Path = path;
    }
}

bool MenuSettings::cardEnabled(unsigned slot) const {
    return slot == 0 ? card1Enabled : slot == 1 ? card2Enabled : false;
}

void MenuSettings::setCardEnabled(unsigned slot, bool enabled) {
    if (slot == 0) {
        card1Enabled = enabled;
    } else if (slot == 1) {
        card2Enabled = enabled;
    }
}

const char* Menu::backendName(RenderBackend backend) {
    switch (backend) {
        case RenderBackend::Vulkan:
            return "VULKAN";
        case RenderBackend::Sdl:
            return "SDL";
        case RenderBackend::Auto:
        default:
            return "AUTO";
    }
}

const char* Menu::scaleName(ScaleMode scale) {
    switch (scale) {
        case ScaleMode::Nearest:
            return "NEAREST";
        case ScaleMode::Fsr1:
            return "FSR1";
        case ScaleMode::Linear:
        default:
            return "LINEAR";
    }
}

const char* Menu::aspectName(AspectMode aspect) {
    switch (aspect) {
        case AspectMode::FourThree:
            return "4:3";
        case AspectMode::Stretch:
            return "STRETCH";
        case AspectMode::Auto:
        default:
            return "AUTO";
    }
}

std::vector<MenuItem> Menu::items(const MenuSettings& settings,
                                  const MenuStatus& status) const {
    std::vector<MenuItem> list;
    const auto add = [&list](std::string label, std::string value, bool enabled,
                             bool adjustable, MenuAction action) {
        list.push_back({std::move(label), std::move(value), enabled, adjustable, action});
    };

    switch (tab_) {
        case MenuTab::Graphics:
            add("RESOLUTION", settings.resolution().label, true, true, MenuAction::CycleResolution);
            add("FULLSCREEN", formatBool(settings.fullscreen), true, true,
                MenuAction::ToggleFullscreen);
            add("RENDER BACKEND", Menu::backendName(settings.backend), true, true,
                MenuAction::CycleBackend);
            add("SCALE FILTER",
                settings.scale == ScaleMode::Fsr1 && !status.fsrAvailable
                    ? "FSR1 (UNAVAILABLE)"
                    : Menu::scaleName(settings.scale),
                true, true, MenuAction::CycleScale);
            add("ASPECT RATIO", Menu::aspectName(settings.aspect), true, true,
                MenuAction::CycleAspect);
            add("VSYNC", formatBool(settings.vsync), true, true, MenuAction::ToggleVsync);
            add("FPS LIMIT",
                settings.fpsLimit == 0 ? "UNLIMITED" : std::to_string(settings.fpsLimit),
                true, true, MenuAction::CycleFpsLimit);
            add("INTEGER SCALE", formatBool(settings.integerScale), true, true,
                MenuAction::ToggleIntegerScale);
            add("NATIVE JIT", formatBool(settings.jit), true, true, MenuAction::ToggleJit);
            add("JIT BLOCKS", std::to_string(status.jitBlocks), false, false, MenuAction::None);
            add("JIT EXECUTED", std::to_string(status.jitExecuted), false, false,
                MenuAction::None);
            add("FRAME TIME", formatFixed(status.frameTimeMs, 2) + " MS", false, false,
                MenuAction::None);
            add("CPU TIME", formatFixed(status.cpuTimeMs, 2) + " MS", false, false,
                MenuAction::None);
            add("FPS", formatFixed(status.fps, 1), false, false, MenuAction::None);
            break;
        case MenuTab::Upscaling:
            add("UPSCALER",
                settings.scale == ScaleMode::Fsr1 && settings.fsrEnabled
                    ? "FSR1 EASU + RCAS"
                    : "NATIVE",
                status.fsrAvailable, true, MenuAction::ToggleFsr);
            add("FSR SHARPNESS", formatFixed(settings.fsrSharpness, 2), status.fsrAvailable,
                true, MenuAction::AdjustFsrSharpness);
            add("OUTPUT RESOLUTION", settings.resolution().label, true, true,
                MenuAction::CycleResolution);
            add("INTERNAL RESOLUTION", "1024X512 (FIXED)", false, false, MenuAction::None);
            add("FSR STATUS", status.fsrAvailable ? "AVAILABLE (VULKAN)" : "UNAVAILABLE",
                false, false, MenuAction::None);
            add("DLSS", "UNAVAILABLE", false, false, MenuAction::None);
            add("DLSS DETAIL", status.dlssReason, false, false, MenuAction::None);
            add("NATIVE/FSR COMPARE", "TOGGLE UPSCALER ABOVE", false, false, MenuAction::None);
            break;
        case MenuTab::Controller: {
            const bool hasDevices = !status.controllerDeviceNames.empty();
            const bool hasChoice = hasDevices && status.controllerDeviceNames.size() > 1;
            add("DEVICE", status.controllerDeviceIndex >= 0 &&
                                  status.controllerDeviceIndex <
                                      static_cast<int>(status.controllerDeviceNames.size())
                              ? status.controllerDeviceNames[static_cast<std::size_t>(
                                    status.controllerDeviceIndex)]
                              : status.controllerConnected ? "UNKNOWN" : "NO CONTROLLER",
                hasChoice, hasChoice, MenuAction::CycleControllerDevice);
            add("DEVICE PROFILE",
                status.controllerProfileConfigured
                    ? (status.controllerProfileName.empty() ? "SAVED"
                                                            : status.controllerProfileName)
                    : "NOT SAVED",
                false, false, MenuAction::None);
            add("STICK DEADZONE",
                std::to_string(
                    std::max(0, settings.controllerMapping.deadzone) * 100 / 32768) +
                    "% / " + std::to_string(std::max(0, settings.controllerMapping.deadzone)),
                true, true, MenuAction::AdjustDeadzone);
            add("TRIGGER THRESHOLD",
                std::to_string(
                    std::max(0, settings.controllerMapping.triggerThreshold) * 100 / 32768) +
                    "% / " +
                    std::to_string(std::max(0, settings.controllerMapping.triggerThreshold)),
                true, true, MenuAction::AdjustTriggerThreshold);
            add("BUTTON CONFLICTS", status.controllerConflict.empty() ? "NONE"
                                                                     : status.controllerConflict,
                false, false, MenuAction::None);
            add("TEST INPUT",
                status.controllerConnected && !status.controllerLiveButtons.empty()
                    ? status.controllerLiveButtons
                    : status.controllerConnected ? "-" : "NO CONTROLLER",
                false, false, MenuAction::None);
            for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
                list.push_back(
                    {std::string("MAP ") + ps1ButtonName(slot),
                     hostControlName(settings.controllerMapping.host[slot]), true, false,
                     MenuAction::AdjustMapping, static_cast<int>(slot)});
            }
            add("RESET TO DEFAULT", "XBOX AUTOMAP", true, false,
                MenuAction::ResetControllerMapping);
            add("SAVE PROFILE", "WRITE SETTINGS JSON", true, false,
                MenuAction::SaveControllerProfile);
            break;
        }
        case MenuTab::MouseCamera:
            add("CAPTURE CURSOR", formatBool(settings.mouseCapture), true, true,
                MenuAction::ToggleMouseCapture);
            add("SENSITIVITY", "NOT IMPLEMENTED", false, false, MenuAction::None);
            add("INVERT Y", "NOT IMPLEMENTED", false, false, MenuAction::None);
            add("SMOOTHING", "NOT IMPLEMENTED", false, false, MenuAction::None);
            add("MOUSE TO PAD", "GENERIC MAPPING (NOT VALIDATED)", false, false,
                MenuAction::None);
            add("CAMERA PROFILE", "GAME-SPECIFIC ADAPTER REQUIRED", false, false,
                MenuAction::None);
            break;
        case MenuTab::MemoryCards:
            add("CARD 1 SLOT", formatBool(settings.card1Enabled), true, true,
                MenuAction::ToggleCard1);
            add("CARD 1 FILE", cardSlotValue(settings, status, 0), true, true,
                MenuAction::CycleCard1);
            add("CARD 1 INFO", status.cardInfo[0], false, false, MenuAction::None);
            add("CARD 2 SLOT", formatBool(settings.card2Enabled), true, true,
                MenuAction::ToggleCard2);
            add("CARD 2 FILE", cardSlotValue(settings, status, 1), true, true,
                MenuAction::CycleCard2);
            add("CARD 2 INFO", status.cardInfo[1], false, false, MenuAction::None);
            add("CREATE CARD 1", "CREATE FORMATTED IMAGE", true, false,
                MenuAction::CreateCard1);
            add("CREATE CARD 2", "CREATE FORMATTED IMAGE", true, false,
                MenuAction::CreateCard2);
            add("SAVE DIRTY CARDS", "WRITE ATOMICALLY + BACKUP", true, false,
                MenuAction::SaveCards);
            add("BACKUP CARD 1", "COPY TO .BAK", true, false, MenuAction::BackupCard1);
            add("BACKUP CARD 2", "COPY TO .BAK", true, false, MenuAction::BackupCard2);
            add("EXPORT CARD 1", "EXPORT TO MCS", true, false, MenuAction::ExportCard1);
            add("EXPORT CARD 2", "EXPORT TO MCS", true, false, MenuAction::ExportCard2);
            add("IMPORT CARD 1", "IMPORT LATEST MCS", true, false, MenuAction::ImportCard1);
            add("IMPORT CARD 2", "IMPORT LATEST MCS", true, false, MenuAction::ImportCard2);
            break;
        case MenuTab::SaveStates:
            add("SAVE STATE", status.saveStateAvailable ? "SLOT 1" : "NOT IMPLEMENTED",
                status.saveStateAvailable, false, MenuAction::SaveState);
            add("LOAD STATE", status.saveStateExists ? "SLOT 1" : "EMPTY",
                status.saveStateExists && status.saveStateCompatible && status.saveStateAvailable,
                false, MenuAction::LoadState);
            add("DELETE STATE", status.saveStateExists ? "SLOT 1" : "EMPTY",
                status.saveStateExists, false, MenuAction::DeleteState);
            add("STATE INFO",
                status.saveStateInfo.empty() ? "SAVE STATES ARE NOT IMPLEMENTED"
                                             : status.saveStateInfo,
                false, false, MenuAction::None);
            add("COMPATIBILITY", status.saveStateCompatible ? "RUNTIME VERSION MATCH"
                                                             : "INCOMPATIBLE VERSION",
                false, false, MenuAction::None);
            add("NOTE", "SAVE STATES DO NOT REPLACE MEMORY CARDS", false, false,
                MenuAction::None);
            break;
        case MenuTab::Audio:
            add("MUTE", formatBool(settings.mute), status.audioOutputAvailable, true,
                MenuAction::ToggleMute);
            add("VOLUME", formatPercent(settings.volume), status.audioOutputAvailable, true,
                MenuAction::AdjustVolume);
            add("HOST AUDIO OUTPUT",
                status.audioOutputAvailable ? "AVAILABLE" : "NOT IMPLEMENTED", false, false,
                MenuAction::None);
            add("DEVICE", "DEFAULT", false, false, MenuAction::None);
            break;
        case MenuTab::Game:
            add("RESUME GAME", "RETURN TO GAMEPLAY", true, false, MenuAction::Resume);
            add("RESTART", "NOT IMPLEMENTED", false, false, MenuAction::Restart);
            add("QUIT", "SAVE CARDS AND EXIT", true, false, MenuAction::Quit);
            add("GAME ID", status.gameId, false, false, MenuAction::None);
            add("IMAGE", status.imagePath, false, false, MenuAction::None);
            add("REGION", status.region, false, false, MenuAction::None);
            add("EXECUTION", status.executionMode, false, false, MenuAction::None);
            add("BACKEND", status.backendName, false, false, MenuAction::None);
            add("JIT", status.jitActive ? "ACTIVE" : "INTERPRETER", false, false,
                MenuAction::None);
            add("PERFORMANCE",
                formatFixed(status.fps, 1) + " FPS / " + formatFixed(status.frameTimeMs, 2) + " MS",
                false, false, MenuAction::None);
            add("PAUSE ON MENU", formatBool(settings.pauseOnMenu), true, true,
                MenuAction::TogglePauseOnMenu);
            break;
        case MenuTab::Count:
        default:
            break;
    }
    itemCount_ = list.size();
    return list;
}

void Menu::ensureValid(const MenuSettings& settings, const MenuStatus& status) {
    const std::size_t count = items(settings, status).size();
    if (count == 0) {
        selected_ = 0;
    } else if (selected_ >= count) {
        selected_ = count - 1;
    }
}

void Menu::move(int delta) {
    if (delta == 0) {
        return;
    }
    long long value = static_cast<long long>(selected_) + delta;
    value = std::max(0LL, value);
    if (itemCount_ > 0) {
        value = std::min(value, static_cast<long long>(itemCount_ - 1));
    }
    selected_ = static_cast<std::size_t>(value);
}

void Menu::setSelected(std::size_t selected) {
    selected_ = selected;
    if (itemCount_ > 0) {
        selected_ = std::min(selected_, itemCount_ - 1);
    }
}

void Menu::moveTab(int delta) {
    const int count = static_cast<int>(kMenuTabCount);
    tab_ = static_cast<MenuTab>(wrapIndex(static_cast<int>(tab_) + delta, count));
    selected_ = 0;
    itemCount_ = 0;
}

void Menu::setTab(MenuTab tab) {
    const int value = static_cast<int>(tab);
    if (value >= 0 && value < static_cast<int>(kMenuTabCount)) {
        tab_ = tab;
        selected_ = 0;
        itemCount_ = 0;
    }
}

void Menu::adjust(MenuSettings& settings, int delta, const MenuStatus& status) {
    if (delta == 0) {
        return;
    }
    const std::vector<MenuItem> list = items(settings, status);
    if (list.empty()) {
        return;
    }
    ensureValid(settings, status);
    const MenuItem& item = list[selected_];
    if (!item.enabled) {
        return;
    }
    bool changed = true;
    switch (item.action) {
        case MenuAction::CycleResolution:
            settings.resolutionIndex = wrapIndex(
                settings.resolutionIndex + delta,
                static_cast<int>(kResolutionPresets.size()));
            break;
        case MenuAction::ToggleFullscreen:
            settings.fullscreen = !settings.fullscreen;
            break;
        case MenuAction::CycleBackend: {
            std::array<RenderBackend, 3> candidates{{RenderBackend::Auto, RenderBackend::Sdl,
                                                     RenderBackend::Vulkan}};
            int count = status.vulkanAvailable ? 3 : 2;
            int index = 0;
            for (int candidate = 0; candidate < count; ++candidate) {
                if (candidates[static_cast<std::size_t>(candidate)] == settings.backend) {
                    index = candidate;
                }
            }
            index = wrapIndex(index + delta, count);
            settings.backend = candidates[static_cast<std::size_t>(index)];
            break;
        }
        case MenuAction::CycleScale: {
            std::array<ScaleMode, 3> candidates{{ScaleMode::Nearest, ScaleMode::Linear,
                                                  ScaleMode::Fsr1}};
            const int count = status.fsrAvailable ? 3 : 2;
            int index = 0;
            for (int candidate = 0; candidate < count; ++candidate) {
                if (candidates[static_cast<std::size_t>(candidate)] == settings.scale) {
                    index = candidate;
                }
            }
            index = wrapIndex(index + delta, count);
            settings.scale = candidates[static_cast<std::size_t>(index)];
            if (settings.scale == ScaleMode::Fsr1) {
                settings.fsrEnabled = true;
            }
            break;
        }
        case MenuAction::CycleAspect: {
            const int count = 3;
            const int index = wrapIndex(static_cast<int>(settings.aspect) + delta, count);
            settings.aspect = static_cast<AspectMode>(index);
            break;
        }
        case MenuAction::ToggleVsync:
            settings.vsync = !settings.vsync;
            break;
        case MenuAction::CycleFpsLimit: {
            int index = 0;
            for (std::size_t candidate = 0; candidate < kFpsLimits.size(); ++candidate) {
                if (kFpsLimits[candidate] == settings.fpsLimit) {
                    index = static_cast<int>(candidate);
                }
            }
            index = wrapIndex(index + delta, static_cast<int>(kFpsLimits.size()));
            settings.fpsLimit = kFpsLimits[static_cast<std::size_t>(index)];
            break;
        }
        case MenuAction::ToggleIntegerScale:
            settings.integerScale = !settings.integerScale;
            break;
        case MenuAction::ToggleJit:
            settings.jit = !settings.jit;
            break;
        case MenuAction::TogglePauseOnMenu:
            settings.pauseOnMenu = !settings.pauseOnMenu;
            break;
        case MenuAction::ToggleFsr:
            if (status.fsrAvailable) {
                settings.fsrEnabled = !settings.fsrEnabled;
                settings.scale = settings.fsrEnabled ? ScaleMode::Fsr1 : ScaleMode::Linear;
            }
            break;
        case MenuAction::AdjustFsrSharpness:
            settings.fsrSharpness =
                clampDouble(settings.fsrSharpness + delta * 0.05, 0.0, 2.0);
            break;
        case MenuAction::ToggleMouseCapture:
            settings.mouseCapture = !settings.mouseCapture;
            break;
        case MenuAction::AdjustMouseSensitivity:
            settings.mouseSensitivity =
                clampDouble(settings.mouseSensitivity + delta * 0.05, 0.1, 10.0);
            break;
        case MenuAction::ToggleInvertY:
            settings.invertY = !settings.invertY;
            break;
        case MenuAction::ToggleSmoothing:
            settings.smoothing = !settings.smoothing;
            break;
        case MenuAction::ToggleMute:
            if (status.audioOutputAvailable) {
                settings.mute = !settings.mute;
            }
            break;
        case MenuAction::AdjustVolume:
            if (status.audioOutputAvailable) {
                settings.volume = clampDouble(settings.volume + delta * 0.05, 0.0, 1.0);
            }
            break;
        case MenuAction::ToggleCard1:
            settings.card1Enabled = !settings.card1Enabled;
            break;
        case MenuAction::ToggleCard2:
            settings.card2Enabled = !settings.card2Enabled;
            break;
        case MenuAction::CycleCard1:
        case MenuAction::CycleCard2: {
            const unsigned slot = item.action == MenuAction::CycleCard1 ? 0u : 1u;
            const std::vector<std::string>& choices = status.cardChoices[slot];
            if (choices.empty()) {
                changed = false;
                break;
            }
            int index = -1;
            const std::string current = settings.cardPath(slot);
            for (std::size_t candidate = 0; candidate < choices.size(); ++candidate) {
                if (choices[candidate] == current) {
                    index = static_cast<int>(candidate);
                }
            }
            index = wrapIndex(index + delta, static_cast<int>(choices.size()));
            settings.setCardPath(slot, choices[static_cast<std::size_t>(index)]);
            break;
        }
        case MenuAction::CycleControllerDevice: {
            const std::size_t count = status.controllerDeviceGuids.size();
            if (count == 0) {
                changed = false;
                break;
            }
            // Prefer the actually selected GUID; fall back to the runtime's
            // device index when it is out of the current list.
            int index = -1;
            for (std::size_t candidate = 0; candidate < count; ++candidate) {
                if (settings.controllerGuid == status.controllerDeviceGuids[candidate]) {
                    index = static_cast<int>(candidate);
                    break;
                }
            }
            if (index < 0 && status.controllerDeviceIndex >= 0 &&
                status.controllerDeviceIndex < static_cast<int>(count)) {
                index = status.controllerDeviceIndex;
            }
            index = wrapIndex(index + delta, static_cast<int>(count));
            settings.controllerGuid = status.controllerDeviceGuids[static_cast<std::size_t>(index)];
            break;
        }
        case MenuAction::AdjustMapping: {
            const int slot = item.parameter;
            if (slot < 0 || slot >= static_cast<int>(kPs1ButtonCount)) {
                changed = false;
                break;
            }
            const std::vector<HostControl>& choices = hostControlChoices();
            const HostControl current =
                settings.controllerMapping.host[static_cast<std::size_t>(slot)];
            std::size_t index = choices.size();
            if (current == kHostNone) {
                index = delta < 0 ? choices.size() - 1 : 0;
            } else {
                for (std::size_t candidate = 0; candidate < choices.size(); ++candidate) {
                    if (choices[candidate] == current) {
                        index = candidate;
                        break;
                    }
                }
                if (index == choices.size()) {
                    index = 0;
                }
            }
            index = wrapIndex(static_cast<int>(index) + delta, static_cast<int>(choices.size()));
            settings.controllerMapping.host[static_cast<std::size_t>(slot)] =
                choices[static_cast<std::size_t>(index)];
            break;
        }
        case MenuAction::AdjustDeadzone:
            settings.controllerMapping.deadzone = std::clamp(
                settings.controllerMapping.deadzone + delta * 1024, 0, 16384);
            break;
        case MenuAction::AdjustTriggerThreshold:
            settings.controllerMapping.triggerThreshold = std::clamp(
                settings.controllerMapping.triggerThreshold + delta * 1024, 1024, 32000);
            break;
        case MenuAction::ResetControllerMapping:
            settings.controllerMapping = ControllerMapping::defaults();
            break;
        case MenuAction::SaveControllerProfile:
        case MenuAction::SaveState:
        case MenuAction::LoadState:
        case MenuAction::DeleteState:
        case MenuAction::CreateCard1:
        case MenuAction::CreateCard2:
        case MenuAction::SaveCards:
        case MenuAction::BackupCard1:
        case MenuAction::BackupCard2:
        case MenuAction::ExportCard1:
        case MenuAction::ExportCard2:
        case MenuAction::ImportCard1:
        case MenuAction::ImportCard2:
        case MenuAction::Resume:
        case MenuAction::Restart:
        case MenuAction::Quit:
            changed = false;
            break;
        case MenuAction::None:
        case MenuAction::Last:
        default:
            changed = false;
            break;
    }
    if (changed) {
        markSettingsChanged();
    }
    ensureValid(settings, status);
}

void Menu::activate(MenuSettings& settings, const MenuStatus& status) {
    const std::vector<MenuItem> list = items(settings, status);
    if (list.empty()) {
        return;
    }
    ensureValid(settings, status);
    const MenuItem& item = list[selected_];
    if (!item.enabled) {
        return;
    }
    switch (item.action) {
        case MenuAction::Resume:
            command_ = MenuCommand::Resume;
            close();
            return;
        case MenuAction::Restart:
            command_ = MenuCommand::Restart;
            close();
            return;
        case MenuAction::Quit:
            command_ = MenuCommand::Quit;
            close();
            return;
        case MenuAction::SaveState:
            command_ = MenuCommand::SaveState;
            return;
        case MenuAction::LoadState:
            command_ = MenuCommand::LoadState;
            return;
        case MenuAction::DeleteState:
            command_ = MenuCommand::DeleteState;
            return;
        case MenuAction::CreateCard1:
            command_ = MenuCommand::CreateCard1;
            return;
        case MenuAction::CreateCard2:
            command_ = MenuCommand::CreateCard2;
            return;
        case MenuAction::SaveCards:
            command_ = MenuCommand::SaveCards;
            return;
        case MenuAction::BackupCard1:
            command_ = MenuCommand::BackupCard1;
            return;
        case MenuAction::BackupCard2:
            command_ = MenuCommand::BackupCard2;
            return;
        case MenuAction::ExportCard1:
            command_ = MenuCommand::ExportCard1;
            return;
        case MenuAction::ExportCard2:
            command_ = MenuCommand::ExportCard2;
            return;
        case MenuAction::ImportCard1:
            command_ = MenuCommand::ImportCard1;
            return;
        case MenuAction::ImportCard2:
            command_ = MenuCommand::ImportCard2;
            return;
        case MenuAction::SaveControllerProfile:
            command_ = MenuCommand::SaveControllerProfile;
            markSettingsChanged();
            return;
        case MenuAction::None:
        case MenuAction::Last:
        default:
            break;
    }
    adjust(settings, 1, status);
}

MenuCommand Menu::takeCommand() {
    const MenuCommand command = command_;
    command_ = MenuCommand::None;
    return command;
}

bool Menu::takeSettingsChanged() {
    const bool changed = settingsChanged_;
    settingsChanged_ = false;
    return changed;
}

}  // namespace psxreborn
