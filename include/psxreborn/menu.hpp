#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "psxreborn/config.hpp"
#include "psxreborn/controller_profile.hpp"

namespace psxreborn {

enum class RenderBackend {
    Auto,
    Vulkan,
    Sdl,
};

enum class ScaleMode {
    Nearest,
    Linear,
    Fsr1,
};

enum class AspectMode {
    Auto,
    FourThree,
    Stretch,
};

enum class MenuTab : uint8_t {
    Graphics,
    Upscaling,
    Controller,
    MouseCamera,
    MemoryCards,
    SaveStates,
    Audio,
    Game,
    Count,
};

inline constexpr std::size_t kMenuTabCount = static_cast<std::size_t>(MenuTab::Count);

const char* menuTabName(MenuTab tab);

struct ResolutionPreset {
    const char* label;
    int width;
    int height;
};

inline constexpr std::array<ResolutionPreset, 7> kResolutionPresets{{
    {"640X480", 640, 480},
    {"1280X720", 1280, 720},
    {"1280X960", 1280, 960},
    {"1920X1080", 1920, 1080},
    {"1920X1440", 1920, 1440},
    {"2560X1440", 2560, 1440},
    {"3840X2160", 3840, 2160},
}};

struct MenuSettings {
    RenderBackend backend = RenderBackend::Auto;
    ScaleMode scale = ScaleMode::Linear;
    AspectMode aspect = AspectMode::Auto;
    bool fsrEnabled = true;
    double fsrSharpness = 2.0;
    int resolutionIndex = 2;
    bool fullscreen = false;
    bool vsync = true;
    int fpsLimit = 60;
    bool integerScale = false;
    bool jit = false;
    bool pauseOnMenu = true;
    bool mouseCapture = false;
    double mouseSensitivity = 1.0;
    bool invertY = false;
    bool smoothing = false;
    bool mute = false;
    double volume = 1.0;
    bool card1Enabled = true;
    bool card2Enabled = true;
    std::string card1Path;
    std::string card2Path;
    // Controller setup: the guided mapping edited through HOME plus the device
    // it applies to (host controller GUID). Persisted as a per-device JSON
    // profile by the runtime, not through the key=value Config file.
    std::string controllerGuid;
    ControllerMapping controllerMapping = ControllerMapping::defaults();

    void load(const Config& config);
    void save(Config& config, bool gameOverride = false) const;

    const ResolutionPreset& resolution() const;
    std::string cardPath(unsigned slot) const;
    void setCardPath(unsigned slot, const std::string& path);
    bool cardEnabled(unsigned slot) const;
    void setCardEnabled(unsigned slot, bool enabled);
};

enum class ControllerType {
    Digital,
};

struct MenuStatus {
    std::string gameId;
    std::string imagePath;
    std::string region;
    std::string executionMode;
    std::string controllerName = "None";
    std::string backendName;
    std::string cardInfo[2];
    std::string saveStateInfo;
    std::string message;
    std::vector<std::string> cardChoices[2];
    int cardChoiceIndex[2] = {-1, -1};
    bool cardPresent[2] = {false, false};
    bool cardDirty[2] = {false, false};
    bool cardEnabled[2] = {true, true};
    bool cardErrors[2] = {false, false};
    bool saveStateExists = false;
    bool saveStateAvailable = false;
    bool saveStateCompatible = true;
    bool vulkanAvailable = false;
    bool vulkanActive = false;
    bool fsrAvailable = false;
    bool dlssAvailable = false;
    std::string dlssReason = "DLSS SDK is not available";
    bool audioOutputAvailable = false;
    bool analogControllerAvailable = false;
    bool vibrationAvailable = false;
    bool jitActive = false;
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double cpuTimeMs = 0.0;
    uint64_t jitBlocks = 0;
    uint64_t jitExecuted = 0;
    // Controller settings tab inputs, refreshed by the runtime each frame.
    std::vector<std::string> controllerDeviceNames;
    std::vector<std::string> controllerDeviceGuids;
    int controllerDeviceIndex = -1;
    std::string controllerProfileGuid;
    std::string controllerProfileName;
    std::string controllerConflict;
    std::string controllerLiveButtons;
    bool controllerConnected = false;
    bool controllerProfileConfigured = false;
};

enum class MenuAction : uint8_t {
    None,
    Resume,
    ToggleFullscreen,
    CycleResolution,
    CycleBackend,
    CycleScale,
    CycleAspect,
    ToggleVsync,
    CycleFpsLimit,
    ToggleIntegerScale,
    ToggleJit,
    TogglePauseOnMenu,
    ToggleFsr,
    AdjustFsrSharpness,
    ToggleMouseCapture,
    AdjustMouseSensitivity,
    ToggleInvertY,
    ToggleSmoothing,
    ToggleMute,
    AdjustVolume,
    ToggleCard1,
    ToggleCard2,
    CycleCard1,
    CycleCard2,
    CreateCard1,
    CreateCard2,
    SaveCards,
    BackupCard1,
    BackupCard2,
    ExportCard1,
    ExportCard2,
    ImportCard1,
    ImportCard2,
    SaveState,
    LoadState,
    DeleteState,
    Restart,
    Quit,
    // Controller tab
    CycleControllerDevice,
    AdjustMapping,
    AdjustDeadzone,
    AdjustTriggerThreshold,
    ResetControllerMapping,
    SaveControllerProfile,
    Last,
};

enum class MenuCommand {
    None,
    Resume,
    Restart,
    Quit,
    SaveState,
    LoadState,
    DeleteState,
    CreateCard1,
    CreateCard2,
    SaveCards,
    BackupCard1,
    BackupCard2,
    ExportCard1,
    ExportCard2,
    ImportCard1,
    ImportCard2,
    SaveControllerProfile,
};

struct MenuItem {
    std::string label;
    std::string value;
    bool enabled = true;
    bool adjustable = false;
    MenuAction action = MenuAction::None;
    // Optional slot qualifier for parameterised actions (e.g. the PS1 button
    // slot for MenuAction::AdjustMapping).
    int parameter = 0;
};

class Menu {
public:
    Menu() = default;

    void open() { open_ = true; }
    void close() { open_ = false; }
    void toggle() { open_ = !open_; }
    bool isOpen() const { return open_; }

    void move(int delta);
    void moveTab(int delta);
    void setTab(MenuTab tab);
    MenuTab tab() const { return tab_; }

    void activate(MenuSettings& settings, const MenuStatus& status);
    void adjust(MenuSettings& settings, int delta, const MenuStatus& status);

    MenuCommand takeCommand();
    bool takeSettingsChanged();
    void markDirty() { settingsChanged_ = true; }

    std::vector<MenuItem> items(const MenuSettings& settings, const MenuStatus& status) const;
    std::size_t selected() const { return selected_; }
    void setSelected(std::size_t selected);

    static const char* backendName(RenderBackend backend);
    static const char* scaleName(ScaleMode scale);
    static const char* aspectName(AspectMode aspect);

private:
    void ensureValid(const MenuSettings& settings, const MenuStatus& status);
    void markSettingsChanged() { settingsChanged_ = true; }

    bool open_ = false;
    MenuTab tab_ = MenuTab::Graphics;
    std::size_t selected_ = 0;
    mutable std::size_t itemCount_ = 0;
    MenuCommand command_ = MenuCommand::None;
    bool settingsChanged_ = false;
};

}  // namespace psxreborn
