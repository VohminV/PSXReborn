#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "psxreborn/bios.hpp"
#include "psxreborn/cdrom.hpp"
#include "psxreborn/config.hpp"
#include "psxreborn/cpu.hpp"
#include "psxreborn/hardware.hpp"
#include "psxreborn/memcard.hpp"
#include "psxreborn/memory.hpp"
#include "psxreborn/menu.hpp"
#include "psxreborn/overlay.hpp"
#include "psxreborn/pad.hpp"
#include "psxreborn/input_trace.hpp"
#include "psxreborn/psx_exe.hpp"
#if defined(PSXREBORN_HAS_VULKAN)
#include "psxreborn/vulkan_presenter.hpp"
#endif

#include "glfw_backend.hpp"

using namespace psxreborn;

namespace {
constexpr double kCpuClock = 33868800.0;

PadState g_padState;
GlfwBackend g_backend(g_padState);
void setButton(uint16_t bit, bool pressed) {
    g_padState.setKeyboardButton(bit, pressed);
}

std::string cardsDirectory() {
    std::filesystem::path base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share";
    } else {
        base = std::filesystem::current_path();
    }
    return (base / "psxreborn" / "cards").string();
}

std::string defaultCardPath(unsigned slot) {
    return (std::filesystem::path(cardsDirectory()) /
            (slot == 0 ? "slot1.mcd" : "slot2.mcd")).string();
}

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return extension;
}

std::vector<std::string> availableCardFiles() {
    std::vector<std::string> result;
    std::error_code error;
    const std::filesystem::path directory(cardsDirectory());
    if (!std::filesystem::is_directory(directory, error)) {
        return result;
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error)) {
            continue;
        }
        const std::string extension = lowercaseExtension(entry.path());
        if (extension == ".mcd" || extension == ".mcr") {
            result.push_back(entry.path().string());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string detectGameId(const std::string& path) {
    const std::string stem = std::filesystem::path(path).stem().string();
    std::string upper = stem;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    static constexpr const char* prefixes[] = {"SLES", "SLUS", "SCES", "SCUS",
                                                "SLPS", "SLPM", "SCPS", "SIPS"};
    for (const char* prefix : prefixes) {
        const std::size_t position = upper.find(prefix);
        if (position == std::string::npos) {
            continue;
        }
        std::string id;
        for (std::size_t index = position; index < upper.size() && id.size() < 11; ++index) {
            const unsigned char character = static_cast<unsigned char>(upper[index]);
            if (std::isalnum(character) != 0 || character == '_' || character == '.' ||
                character == '-') {
                id.push_back(static_cast<char>(character));
            } else {
                break;
            }
        }
        const std::string sanitized = Config::sanitizeGameId(id);
        if (!sanitized.empty()) {
            return sanitized;
        }
    }
    return Config::sanitizeGameId(stem);
}

std::string regionForGameId(const std::string& gameId) {
    if (gameId.rfind("SLES", 0) == 0 || gameId.rfind("SCES", 0) == 0) {
        return "PAL";
    }
    if (gameId.rfind("SLUS", 0) == 0 || gameId.rfind("SCUS", 0) == 0) {
        return "NTSC-U";
    }
    if (gameId.rfind("SLPS", 0) == 0 || gameId.rfind("SLPM", 0) == 0 ||
        gameId.rfind("SCPS", 0) == 0) {
        return "NTSC-J";
    }
    return "UNKNOWN";
}

std::string sanitizeFileStem(const std::string& value) {
    std::string result;
    for (char character : value) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (std::isalnum(code) != 0 || character == '-' || character == '_') {
            result.push_back(character);
        } else if (character == ' ' || character == '.' || character == '/') {
            result.push_back('_');
        }
    }
    if (result.empty()) {
        result = "psxreborn";
    }
    return result;
}

std::unique_ptr<MemoryCard> loadOrCreateCard(const std::string& path, bool createIfMissing,
                                             std::string& message, bool& failed) {
    failed = false;
    if (path.empty()) {
        failed = true;
        message = "MEMORY CARD PATH IS EMPTY";
        return nullptr;
    }
    std::error_code fsError;
    if (std::filesystem::exists(path, fsError)) {
        MemoryCardValidation validation;
        std::string error;
        std::optional<MemoryCard> card = MemoryCard::load(path, error, &validation);
        if (!card) {
            failed = true;
            message = "CARD LOAD FAILED: " + error;
            return nullptr;
        }
        if (validation.readOnly()) {
            message = "CARD IS READ-ONLY: " + validation.message;
        } else {
            message.clear();
        }
        card->setPath(path);
        return std::make_unique<MemoryCard>(std::move(*card));
    }
    if (!createIfMissing) {
        failed = true;
        message = "CARD NOT FOUND: " + path;
        return nullptr;
    }
    MemoryCard card = MemoryCard::formatted();
    std::string error;
    if (!card.save(path, error)) {
        failed = true;
        message = "CARD CREATE FAILED: " + error;
        return nullptr;
    }
    card.setPath(path);
    message = "CARD CREATED: " + path;
    return std::make_unique<MemoryCard>(std::move(card));
}

struct CardRuntime {
    std::unique_ptr<MemoryCard> card;
    std::string path;
    bool enabled = false;
    bool initialized = false;
    bool error = false;
};

void refreshCardStatus(const CardRuntime& runtime, unsigned slot, MenuStatus& status) {
    status.cardPresent[slot] = runtime.card != nullptr;
    status.cardDirty[slot] = runtime.card != nullptr && runtime.card->isDirty();
    status.cardErrors[slot] = runtime.error;
    if (runtime.card == nullptr) {
        status.cardInfo[slot] = runtime.error ? "UNAVAILABLE" : "DISCONNECTED";
        return;
    }
    const std::size_t files = runtime.card->files().size();
    status.cardInfo[slot] = std::to_string(files) + " FILES / " +
                            std::to_string(runtime.card->freeBlockCount()) + " FREE";
    if (runtime.card->isReadOnly()) {
        status.cardInfo[slot] = "READ-ONLY | " + status.cardInfo[slot];
    }
}

void syncCard(CardRuntime& runtime, unsigned slot, const MenuSettings& settings, System& system,
              MenuStatus& status, bool createIfMissing) {
    const bool enabled = settings.cardEnabled(slot);
    const std::string target = enabled ? settings.cardPath(slot) : std::string();
    if (!runtime.initialized || runtime.enabled != enabled || runtime.path != target) {
        system.setMemoryCard(slot, nullptr);
        runtime.card.reset();
        runtime.path = target;
        runtime.enabled = enabled;
        runtime.initialized = true;
        runtime.error = false;
        if (enabled) {
            std::string message;
            bool failed = false;
            std::unique_ptr<MemoryCard> card =
                loadOrCreateCard(target, createIfMissing, message, failed);
            runtime.error = failed;
            if (card) {
                runtime.card = std::move(card);
                system.setMemoryCard(slot, runtime.card.get());
            }
            if (!message.empty()) {
                status.message = message;
            }
        }
    }
    refreshCardStatus(runtime, slot, status);
}

void refreshCardChoices(const MenuSettings& settings, MenuStatus& status) {
    const std::vector<std::string> existing = availableCardFiles();
    for (unsigned slot = 0; slot < 2; ++slot) {
        std::vector<std::string>& choices = status.cardChoices[slot];
        choices.clear();
        if (!settings.cardEnabled(slot)) {
            status.cardChoiceIndex[slot] = -1;
            continue;
        }
        choices = existing;
        const std::string current = settings.cardPath(slot);
        if (!current.empty() &&
            std::find(choices.begin(), choices.end(), current) == choices.end()) {
            choices.push_back(current);
        }
        std::sort(choices.begin(), choices.end());
        choices.erase(std::unique(choices.begin(), choices.end()), choices.end());
        status.cardChoiceIndex[slot] = -1;
        for (std::size_t index = 0; index < choices.size(); ++index) {
            if (choices[index] == current) {
                status.cardChoiceIndex[slot] = static_cast<int>(index);
                break;
            }
        }
    }
}

uint32_t color555To888(uint16_t p) {
    uint8_t r = static_cast<uint8_t>((p & 0x1Fu) << 3);
    uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x1Fu) << 3);
    uint8_t b = static_cast<uint8_t>(((p >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: psxreborn_play <disc.cue|disc.iso|file.exe>\n";
        return 2;
    }

    const std::string path = argv[1];

    Memory memory;
    std::unique_ptr<Cpu> cpuStorage;
    std::unique_ptr<System> systemStorage;
    std::unique_ptr<Bios> biosStorage;
    std::unique_ptr<CdImage> disc;

    if (path.size() > 4 && path.substr(path.size() - 4) == ".exe") {
        std::vector<uint8_t> fileData;
        std::ifstream f(path, std::ios::binary);
        fileData.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        const PsxExeResult parsed = parsePsxExe(fileData);
        if (!parsed) {
            std::cerr << "parse error\n";
            return 1;
        }
        const PsxExeImage& image = *parsed.image;
        const PsxExeLoadResult loaded = loadPsxExe(image, memory);
        if (!loaded.success) {
            std::cerr << "load error\n";
            return 1;
        }
        cpuStorage = std::make_unique<Cpu>(memory);
        systemStorage = std::make_unique<System>(memory);
        biosStorage = std::make_unique<Bios>(*cpuStorage, memory, *systemStorage);
        systemStorage->reset();
        systemStorage->bind(*cpuStorage);
        biosStorage->reset();
        biosStorage->install();
        cpuStorage->setExecutionMode(CpuExecutionMode::Hardware);
        cpuStorage->resetFromExe(image.header.entryPoint, image.header.globalPointer,
                                 image.header.stackBase, image.header.stackOffset);
    } else {
        std::string error;
        std::optional<CdImage> opened = CdImage::open(path, error);
        if (!opened) {
            std::cerr << "disc error: " << error << "\n";
            return 1;
        }
        disc = std::make_unique<CdImage>(std::move(*opened));
        std::string stackLine;
        const std::optional<std::string> boot = disc->bootExecutable(stackLine);
        if (!boot) {
            std::cerr << "no boot exe\n";
            return 1;
        }
        std::vector<uint8_t> executable;
        if (!disc->readFile(*boot, executable)) {
            std::cerr << "read exe error\n";
            return 1;
        }
        const PsxExeResult parsed = parsePsxExe(executable);
        if (!parsed) {
            std::cerr << "parse error\n";
            return 1;
        }
        const PsxExeImage& image = *parsed.image;
        const PsxExeLoadResult loaded = loadPsxExe(image, memory);
        if (!loaded.success) {
            std::cerr << "load error\n";
            return 1;
        }
        cpuStorage = std::make_unique<Cpu>(memory);
        systemStorage = std::make_unique<System>(memory);
        biosStorage = std::make_unique<Bios>(*cpuStorage, memory, *systemStorage);
        systemStorage->reset();
        systemStorage->bind(*cpuStorage);
        biosStorage->reset();
        biosStorage->attachDisc(disc.get());
        systemStorage->cdrom().attachDisc(disc.get());
        biosStorage->install();
        cpuStorage->setExecutionMode(CpuExecutionMode::Hardware);

        uint32_t stackBase = image.header.stackBase;
        uint32_t stackOffset = image.header.stackOffset;
        if (!stackLine.empty()) {
            try {
                stackBase = std::stoul(stackLine, nullptr, 16);
                stackOffset = 0;
            } catch (...) {
            }
        }
        cpuStorage->resetFromExe(image.header.entryPoint, image.header.globalPointer, stackBase,
                                 stackOffset);
    }

    // Fresh input at transaction time: Sio0 samples the backend when the guest
    // selects the pad; the BIOS pad path samples it at vblank cadence.
    systemStorage->sio0().setPadSource(&g_backend);

    Cpu& cpu = *cpuStorage;
    System& system = *systemStorage;
    Bios& bios = *biosStorage;

    Config config;
    const char* configOverride = std::getenv("PSXREBORN_CONFIG");
    const std::string configPath =
        configOverride != nullptr && *configOverride != '\0' ? configOverride : Config::defaultPath();
    std::string configError;
    if (!config.load(configPath, configError)) {
        std::cerr << "[CONFIG] " << configError << " (using defaults)\n";
    }
    const std::string gameId = detectGameId(path);
    config.setGameId(gameId);

    MenuSettings settings;
    settings.load(config);
    if (settings.card1Path.empty()) {
        settings.card1Path = defaultCardPath(0);
    }
    if (settings.card2Path.empty()) {
        settings.card2Path = defaultCardPath(1);
    }
    if (const char* jit = std::getenv("PSXREBORN_JIT")) {
        settings.jit = jit[0] != '0';
    }
    settings.controllerGuid = config.getString("input.controller.guid", "");
    cpu.setJitEnabled(settings.jit);

    Menu menu;
    MenuStatus status;
    status.gameId = gameId;
    status.imagePath = path;
    status.region = regionForGameId(gameId);
    status.executionMode = cpu.jitEnabled() ? "JIT" : "INTERPRETER";
    status.backendName = "SDL";
    status.vulkanAvailable = false;
    status.vulkanActive = false;
    status.fsrAvailable = false;
    status.dlssAvailable = false;
    status.audioOutputAvailable = false;
    status.analogControllerAvailable = true;
    status.vibrationAvailable = false;
    status.saveStateAvailable = false;
    status.saveStateExists = false;
    status.saveStateInfo = "SAVE STATES ARE NOT IMPLEMENTED";

    CardRuntime card1;
    CardRuntime card2;
    syncCard(card1, 0, settings, system, status, true);
    syncCard(card2, 1, settings, system, status, true);
    refreshCardChoices(settings, status);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // GLFW takes over all controller/joystick input; SDL keeps owning the
    // window, renderer and Vulkan surface.
    {
        std::string glfwError;
        if (glfwInputInit(glfwError)) {
            glfwLoadGamepadDatabaseIfRequested();
            int joysticks = 0;
            for (int jid = 0; jid <= kMaxJoystickId; ++jid) {
                joysticks += glfwInputJoystickPresent(jid) ? 1 : 0;
            }
            std::cerr << "[CONTROLLER] glfw ready, joysticks=" << joysticks << "\n";
        } else {
            status.message = "CONTROLLER INPUT UNAVAILABLE: " + glfwError;
            std::cerr << "[CONTROLLER] " << glfwError << "\n";
        }
    }

    const ResolutionPreset& initialResolution = settings.resolution();
    uint32_t windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (settings.fullscreen) {
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    const std::string title = "PSXReborn - " + gameId;
#if defined(PSXREBORN_HAS_VULKAN)
    std::unique_ptr<VulkanPresenter> vulkanPresenter;
#endif
    bool useVulkan = false;
    SDL_Window* window = nullptr;
#if defined(PSXREBORN_HAS_VULKAN)
    if (settings.backend != RenderBackend::Sdl) {
        window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, initialResolution.width,
                                  initialResolution.height, windowFlags | SDL_WINDOW_VULKAN);
        if (window != nullptr) {
            auto candidate = std::make_unique<VulkanPresenter>();
            std::string vulkanError;
            if (candidate->initialize(window, settings.vsync,
                                      settings.scale == ScaleMode::Nearest, &vulkanError)) {
                vulkanPresenter = std::move(candidate);
                useVulkan = true;
                status.vulkanAvailable = true;
                status.vulkanActive = true;
                status.backendName = "VULKAN";
                std::cerr << "[VIDEO] Vulkan backend: " << vulkanPresenter->deviceName() << "\n";
            } else {
                status.message = "VULKAN UNAVAILABLE: " + vulkanError;
                std::cerr << "[VIDEO] Vulkan unavailable: " << vulkanError << "\n";
                SDL_DestroyWindow(window);
                window = nullptr;
            }
        }
    }
#else
    if (settings.backend == RenderBackend::Vulkan) {
        status.message = "VULKAN SUPPORT IS NOT COMPILED IN";
    }
#endif
    if (window == nullptr) {
        window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, initialResolution.width,
                                  initialResolution.height, windowFlags);
    }
    if (!window) {
        std::cerr << "window fail: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    if (!useVulkan) {
        uint32_t rendererFlags = SDL_RENDERER_ACCELERATED;
        if (settings.vsync) {
            rendererFlags |= SDL_RENDERER_PRESENTVSYNC;
        }
        renderer = SDL_CreateRenderer(window, -1, rendererFlags);
        if (!renderer) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        }
        if (!renderer) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!renderer) {
            std::cerr << "renderer fail: " << SDL_GetError() << "\n";
            return 1;
        }
        SDL_RenderSetVSync(renderer, settings.vsync ? 1 : 0);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                    settings.scale == ScaleMode::Nearest ? "0" : "1");
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, 1024, 512);
        if (!texture) {
            std::cerr << "texture fail: " << SDL_GetError() << "\n";
            return 1;
        }
        status.backendName = "SDL";
        status.vulkanActive = false;
    }

    SDL_Texture* overlayTexture = nullptr;
    int overlayWidth = 0;
    int overlayHeight = 0;
    OverlayCanvas overlayCanvas;

    int controllerJid = -1;
    std::vector<std::string> deviceNames;
    std::vector<std::string> deviceGuids;
    bool profileSaved = false;
    bool controllerListDirty = true;
    // Menu navigation edge state: GLFW is polled, not event-driven, so menu
    // actions fire on rising-edge transitions between frames.
    uint32_t menuPrevGamepadButtons = 0;
    uint64_t menuPrevRawButtons = 0;
    auto enumerateControllers = [&]() {
        deviceNames.clear();
        deviceGuids.clear();
        if (!glfwInputActive()) {
            return;
        }
        for (int jid = 0; jid <= kMaxJoystickId; ++jid) {
            if (!glfwInputJoystickPresent(jid)) {
                continue;
            }
            deviceNames.push_back(glfwJoystickDisplayName(jid));
            deviceGuids.push_back(glfwJoystickIdentity(jid));
        }
    };
    auto loadControllerProfile = [&](const std::string& guid) -> bool {
        ControllerProfile profile;
        std::string error;
        if (!profile.load(ControllerProfile::pathForGuid(guid), error)) {
            return false;
        }
        profile.guid = guid;
        settings.controllerMapping = profile.mapping;
        status.controllerProfileName = profile.deviceName.empty() ? "SAVED" : profile.deviceName;
        profileSaved = true;
        // The backend holds its own copy of the mapping. Without this push a
        // saved profile is visible in HOME but the pad keeps using the
        // defaults until the next settings edit.
        g_backend.setMapping(settings.controllerMapping);
        inputtrace::chainLine("MAPPING", "profile loaded guid=%s", guid.c_str());
        return true;
    };
    auto saveControllerProfile = [&](bool announce) {
        if (settings.controllerGuid.empty()) {
            if (announce) {
                status.message = "CONTROLLER PROFILE SAVE FAILED: NO DEVICE";
            }
            return;
        }
        ControllerProfile profile;
        profile.formatVersion = ControllerProfile::kFormatVersion;
        profile.guid = settings.controllerGuid;
        profile.mapping = settings.controllerMapping;
        profile.deviceName =
            status.controllerDeviceIndex >= 0 &&
                    status.controllerDeviceIndex < static_cast<int>(deviceNames.size())
                ? deviceNames[static_cast<std::size_t>(status.controllerDeviceIndex)]
                : (controllerJid >= 0 ? glfwJoystickDisplayName(controllerJid)
                                      : std::string());
        std::string error;
        const std::string target = ControllerProfile::pathForGuid(profile.guid);
        if (!profile.save(target, error)) {
            std::cerr << "[CONTROLLER] profile save failed: " << error << "\n";
            if (announce) {
                status.message = "CONTROLLER PROFILE SAVE FAILED: " + error;
            }
            return;
        }
        status.controllerProfileName =
            profile.deviceName.empty() ? "SAVED" : profile.deviceName;
        profileSaved = true;
        if (announce) {
            status.message = "CONTROLLER PROFILE SAVED: " +
                             std::filesystem::path(target).filename().string();
        }
    };
    auto updateControllerStatus = [&]() {
        // Device enumeration opens/closes GLFW-side probes; only scan when
        // the device list may have changed (hotplug) or the HOME menu shows
        // it.
        if (controllerListDirty || menu.isOpen()) {
            enumerateControllers();
            status.controllerDeviceNames = deviceNames;
            status.controllerDeviceGuids = deviceGuids;
            controllerListDirty = false;
        }
        status.controllerDeviceIndex = -1;
        for (std::size_t index = 0; index < deviceGuids.size(); ++index) {
            if ((settings.controllerGuid.empty() && index == 0) ||
                settings.controllerGuid == deviceGuids[index]) {
                status.controllerDeviceIndex = static_cast<int>(index);
                if (settings.controllerGuid.empty()) {
                    settings.controllerGuid = deviceGuids[index];
                }
            }
        }
        if (status.controllerDeviceIndex < 0 && controllerJid >= 0) {
            // The saved identity does not match any GLFW device. GLFW 3.3
            // exposes no stable GUID (identities are "glfw:" + device name),
            // so pre-GLFW SDL-GUID config entries cannot resolve; migrate to
            // the open device's identity so its profile still applies.
            settings.controllerGuid = glfwJoystickIdentity(controllerJid);
            for (std::size_t index = 0; index < deviceGuids.size(); ++index) {
                if (deviceGuids[index] == settings.controllerGuid) {
                    status.controllerDeviceIndex = static_cast<int>(index);
                    break;
                }
            }
        }
        status.controllerConnected = controllerJid >= 0;
        const std::vector<MappingConflict> conflicts =
            mappingConflicts(settings.controllerMapping);
        std::string conflictText;
        for (const MappingConflict& conflict : conflicts) {
            if (!conflictText.empty()) {
                conflictText += "; ";
            }
            conflictText += std::to_string(conflict.slots.size()) + " BUTTONS ON " +
                            hostControlName(conflict.control);
        }
        status.controllerConflict = conflictText;
        std::string liveText;
        const ControllerSnapshot& live = g_backend.lastSnapshot();
        for (std::size_t slot = 0; slot < kPs1ButtonCount && liveText.size() < 40; ++slot) {
            const HostControl control = settings.controllerMapping.host[slot];
            bool pressed = false;
            if (control < kHostAxisBase) {
                pressed = live.buttonPressed(control);
            } else if (control >= kHostAxisBase &&
                       control < kHostAxisBase + kHostAxisCount) {
                pressed = live.axes[control - kHostAxisBase] >=
                          settings.controllerMapping.triggerThreshold;
            }
            if (pressed) {
                if (!liveText.empty()) {
                    liveText += "+";
                }
                liveText += ps1ButtonName(slot);
            }
        }
        status.controllerLiveButtons = liveText;
        status.controllerProfileConfigured = profileSaved;
    };
    auto updateControllerName = [&]() {
        status.controllerName =
            controllerJid >= 0
                ? (glfwJoystickDisplayName(controllerJid).empty()
                       ? std::string("GAMEPAD")
                       : glfwJoystickDisplayName(controllerJid))
                : std::string("KEYBOARD");
    };
    auto closeController = [&]() {
        if (controllerJid >= 0) {
            g_backend.setController(GlfwBackend::kNoJoystick);
            controllerJid = -1;
            g_padState.setLeftStick(0, 0);
            g_padState.setRightStick(0, 0);
            g_padState.clearController();
        }
        menuPrevGamepadButtons = 0;
        menuPrevRawButtons = 0;
        updateControllerName();
    };
    auto openController = [&](int index) -> bool {
        if (controllerJid >= 0) {
            return controllerJid == index;
        }
        if (!glfwInputJoystickPresent(index)) {
            return false;
        }
        // PSXREBORN_PAD_INDEX selects which game controller (in GLFW device
        // order, counting gamepad-configurable devices) feeds the pad;
        // index 0 is the default.
        if (const char* padIndex = std::getenv("PSXREBORN_PAD_INDEX")) {
            const int wanted = std::atoi(padIndex);
            int seen = 0;
            for (int other = 0; other < index; ++other) {
                if (glfwControllerIsGamepad(other)) {
                    ++seen;
                }
            }
            if (seen != wanted) {
                return false;
            }
        }
        controllerJid = index;
        g_backend.setController(index);
        std::cerr << "[CONTROLLER] opened joystick " << index << " name="
                  << glfwJoystickDisplayName(index) << " gamepad="
                  << (glfwControllerIsGamepad(index) ? "yes" : "no (raw layout)")
                  << "\n";
        g_padState.setLeftStick(0, 0);
        g_padState.setRightStick(0, 0);
        updateControllerName();
        g_backend.setMapping(settings.controllerMapping);
        return true;
    };
    auto openControllerByGuid = [&](const std::string& guid) -> bool {
        closeController();
        int wanted = -1;
        if (glfwInputActive()) {
            for (int index = 0; index <= kMaxJoystickId; ++index) {
                if (!glfwInputJoystickPresent(index)) {
                    continue;
                }
                if (glfwJoystickIdentity(index) == guid) {
                    wanted = index;
                    break;
                }
            }
        }
        if (wanted < 0) {
            return false;
        }
        return openController(wanted);
    };
    std::cerr << "[DEBUG] controller enumeration done, devices="
              << [&]() {
                     int count = 0;
                     if (glfwInputActive()) {
                         for (int jid = 0; jid <= kMaxJoystickId; ++jid) {
                             count += glfwInputJoystickPresent(jid) ? 1 : 0;
                         }
                     }
                     return count;
                 }() << "\n";
    updateControllerStatus();
    // Preferred device: the identity saved in the config; fall back to the
    // first detected controller. Reopening honours PSXREBORN_PAD_INDEX for jid
    // selection.
    bool controllerReady = false;
    if (controllerJid < 0 && !settings.controllerGuid.empty()) {
        controllerReady = openControllerByGuid(settings.controllerGuid);
    }
    if (glfwInputActive()) {
        for (int index = 0; index <= kMaxJoystickId && !controllerReady; ++index) {
            controllerReady = openController(index) || controllerReady;
        }
    }
    updateControllerStatus();
    updateControllerName();

    int appliedResolution = -1;
    bool appliedFullscreen = false;
    ScaleMode appliedScale = settings.scale;
    auto applyRuntimeSettings = [&](bool force) {
        const ResolutionPreset& resolution = settings.resolution();
        if (settings.fullscreen) {
            if (force || !appliedFullscreen) {
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                appliedFullscreen = true;
            }
        } else {
            if (force || appliedFullscreen) {
                SDL_SetWindowFullscreen(window, 0);
                appliedFullscreen = false;
            }
            if (force || appliedResolution != settings.resolutionIndex) {
                SDL_SetWindowSize(window, resolution.width, resolution.height);
            }
        }
        appliedResolution = settings.resolutionIndex;
        if (settings.scale != appliedScale) {
#if defined(PSXREBORN_HAS_VULKAN)
            if (vulkanPresenter) {
                vulkanPresenter->setFilter(settings.scale == ScaleMode::Nearest);
                appliedScale = settings.scale;
            } else
#endif
            {
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                            settings.scale == ScaleMode::Nearest ? "0" : "1");
                SDL_Texture* replacement =
                    SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, 1024, 512);
                if (replacement != nullptr) {
                    SDL_DestroyTexture(texture);
                    texture = replacement;
                    appliedScale = settings.scale;
                } else {
                    status.message = "SCALE FILTER CHANGE FAILED: " +
                                     std::string(SDL_GetError());
                }
            }
        }
#if defined(PSXREBORN_HAS_VULKAN)
        if (vulkanPresenter) {
            vulkanPresenter->setVsync(settings.vsync);
        } else
#endif
        if (SDL_RenderSetVSync(renderer, settings.vsync ? 1 : 0) != 0) {
            status.message = "VSYNC IS NOT SUPPORTED BY THE ACTIVE RENDERER";
        }
        cpu.setJitEnabled(settings.jit);
        if (!menu.isOpen()) {
            SDL_SetRelativeMouseMode(settings.mouseCapture ? SDL_TRUE : SDL_FALSE);
        }
        status.executionMode = cpu.jitEnabled() ? "JIT" : "INTERPRETER";
    };
    applyRuntimeSettings(true);

    auto saveSettings = [&]() {
        settings.save(config, false);
        std::string error;
        if (!config.save(configPath, error)) {
            std::cerr << "[CONFIG] " << error << "\n";
            status.message = "CONFIG SAVE FAILED: " + error;
        }
    };

    auto syncPad = [&]() {
        if (menu.isOpen()) {
            g_backend.setSuppressed(true);
            bios.setPadButtons(static_cast<uint16_t>(0xFFFFu));
            bios.setPadSticks(128, 128, 128, 128);
        } else {
            g_backend.setSuppressed(false);
            bios.setPadButtons(g_padState.buttons());
            bios.setPadSticks(g_padState.leftStickX(), g_padState.leftStickY(),
                              g_padState.rightStickX(), g_padState.rightStickY());
        }
    };

    auto setMenuOpen = [&](bool open) {
        if (open == menu.isOpen()) {
            return;
        }
        if (open) {
            inputtrace::chainLine("MENU", "open");
            menu.open();
            refreshCardStatus(card1, 0, status);
            refreshCardStatus(card2, 1, status);
            refreshCardChoices(settings, status);
            updateControllerStatus();
            SDL_SetRelativeMouseMode(SDL_FALSE);
        } else {
            inputtrace::chainLine("MENU", "close");
            menu.close();
            SDL_SetRelativeMouseMode(settings.mouseCapture ? SDL_TRUE : SDL_FALSE);
        }
        syncPad();
    };

    // First-run flow: a connected gamepad without a saved profile gets the
    // verified automatic Xbox -> PS1 mapping, it is persisted immediately so
    // it shows as configured at the next start, and HOME opens on the
    // Controller tab for review. Without a gamepad nothing blocks startup;
    // setup stays reachable through HOME (keyboard: HOME/F1, gamepad: GUIDE).
    if (controllerJid >= 0) {
        std::cerr << "[CONTROLLER] using gamepad identity=" << settings.controllerGuid << "\n";
        if (!loadControllerProfile(settings.controllerGuid)) {
            settings.controllerMapping = ControllerMapping::defaults();
            g_backend.setMapping(settings.controllerMapping);
            saveControllerProfile(false);
            menu.setTab(MenuTab::Controller);
            inputtrace::chainLine("MENU", "open first-run");
            menu.open();
            updateControllerStatus();
            syncPad();
            status.message = "NEW GAMEPAD DETECTED: XBOX AUTOMAP APPLIED - REVIEW IN HOME";
            if (config.has("input.controller.guid")) {
                config.erase("input.controller.guid");
            }
            config.setString("input.controller.guid", settings.controllerGuid);
            saveSettings();
        }
        updateControllerStatus();
    } else {
        std::cerr << "[CONTROLLER] no gamepad connected at startup; use HOME > CONTROLLER "
                     "to set one up\n";
    }
    std::string activeControllerGuid = settings.controllerGuid;

    auto saveAllCards = [&]() {
        bool savedAny = false;
        for (CardRuntime* runtime : {&card1, &card2}) {
            if (runtime->card == nullptr || !runtime->card->isDirty()) {
                continue;
            }
            std::string error;
            if (!runtime->card->saveIfDirty(error)) {
                status.message = "CARD SAVE FAILED: " + error;
                return;
            }
            savedAny = true;
        }
        status.message = savedAny ? "MEMORY CARDS SAVED" : "NO DIRTY MEMORY CARDS";
    };

    auto createCard = [&](unsigned slot) {
        CardRuntime& runtime = slot == 0 ? card1 : card2;
        std::string target = settings.cardPath(slot);
        if (target.empty()) {
            target = defaultCardPath(slot);
            settings.setCardPath(slot, target);
            refreshCardChoices(settings, status);
            saveSettings();
        }
        std::string error;
        MemoryCard card = MemoryCard::formatted();
        if (!card.save(target, error)) {
            status.message = "CARD CREATE FAILED: " + error;
            return;
        }
        card.setPath(target);
        system.setMemoryCard(slot, nullptr);
        runtime.card = std::make_unique<MemoryCard>(std::move(card));
        runtime.path = target;
        runtime.enabled = true;
        runtime.initialized = true;
        runtime.error = false;
        system.setMemoryCard(slot, runtime.card.get());
        refreshCardStatus(runtime, slot, status);
        refreshCardChoices(settings, status);
        status.message = "CARD " + std::to_string(slot + 1) + " CREATED";
    };

    auto backupCard = [&](unsigned slot) {
        CardRuntime& runtime = slot == 0 ? card1 : card2;
        if (runtime.card == nullptr) {
            status.message = "CARD " + std::to_string(slot + 1) + " IS NOT CONNECTED";
            return;
        }
        std::string error;
        if (!runtime.card->createBackup(runtime.path + ".bak", error)) {
            status.message = "CARD BACKUP FAILED: " + error;
            return;
        }
        status.message = "CARD " + std::to_string(slot + 1) + " BACKED UP";
    };

    auto exportCard = [&](unsigned slot) {
        CardRuntime& runtime = slot == 0 ? card1 : card2;
        if (runtime.card == nullptr) {
            status.message = "CARD " + std::to_string(slot + 1) + " IS NOT CONNECTED";
            return;
        }
        const std::vector<MemoryCardFileInfo> files = runtime.card->files();
        if (files.empty()) {
            status.message = "CARD HAS NO SAVE FILES TO EXPORT";
            return;
        }
        const std::filesystem::path output = std::filesystem::path(cardsDirectory()) /
                                             (sanitizeFileStem(gameId + "-" + files.front().name) +
                                              ".mcs");
        std::string error;
        if (!runtime.card->exportMcs(output.string(), files.front().name, error)) {
            status.message = "MCS EXPORT FAILED: " + error;
            return;
        }
        status.message = "EXPORTED " + files.front().name + " TO " + output.filename().string();
    };

    auto importCard = [&](unsigned slot) {
        CardRuntime& runtime = slot == 0 ? card1 : card2;
        std::error_code fsError;
        const std::filesystem::path directory(cardsDirectory());
        std::filesystem::path latest;
        std::filesystem::file_time_type latestTime{};
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(directory, fsError)) {
            if (fsError || !entry.is_regular_file(fsError) ||
                lowercaseExtension(entry.path()) != ".mcs") {
                continue;
            }
            const std::filesystem::file_time_type time = entry.last_write_time(fsError);
            if (fsError) {
                continue;
            }
            if (latest.empty() || time > latestTime) {
                latest = entry.path();
                latestTime = time;
            }
        }
        if (latest.empty()) {
            status.message = "NO MCS FILE FOUND IN " + directory.string();
            return;
        }
        if (runtime.card == nullptr) {
            std::string message;
            bool failed = false;
            std::unique_ptr<MemoryCard> card =
                loadOrCreateCard(runtime.path, true, message, failed);
            if (!card) {
                status.message = message;
                return;
            }
            runtime.card = std::move(card);
            system.setMemoryCard(slot, runtime.card.get());
            runtime.error = false;
        }
        std::string error;
        if (!runtime.card->importMcs(latest.string(), error)) {
            status.message = "MCS IMPORT FAILED: " + error;
            return;
        }
        if (!runtime.card->saveIfDirty(error)) {
            status.message = "CARD SAVE FAILED: " + error;
            return;
        }
        refreshCardStatus(runtime, slot, status);
        status.message = "IMPORTED " + latest.filename().string();
    };

    const char* dumpPath = std::getenv("PSXREBORN_PLAY_DUMP");
    double dumpSteps = 0;
    if (const char* value = std::getenv("PSXREBORN_PLAY_STEPS")) {
        dumpSteps = std::strtod(value, nullptr);
    }
    double snapshotPeriodSeconds = 0.0;
    if (const char* value = std::getenv("PSXREBORN_SNAPSHOT")) {
        snapshotPeriodSeconds = std::strtod(value, nullptr);
        if (snapshotPeriodSeconds > 0.0) {
            std::fprintf(stderr, "[SNAPSHOT] period=%.3fs\n", snapshotPeriodSeconds);
        }
    }
    double dumpPeriodSeconds = 0.0;
    if (const char* value = std::getenv("PSXREBORN_PERIODIC_DUMP")) {
        dumpPeriodSeconds = std::strtod(value, nullptr);
    }
    double autoStartPeriodSeconds = 0.0;
    if (const char* value = std::getenv("PSXREBORN_AUTO_START")) {
        autoStartPeriodSeconds = std::strtod(value, nullptr);
        if (autoStartPeriodSeconds > 0.0) {
            std::fprintf(stderr, "[PLAY] auto START every %.1fs\n", autoStartPeriodSeconds);
        }
    }
    uint64_t dumpIndex = 0;
    double nextSnapshotAt = 0.0;
    double nextDumpAt = 0.0;
    double elapsedVirtualSeconds = 0.0;
    uint64_t totalSteps = 0;

    bool running = true;
    auto handleCommand = [&](MenuCommand command) {
        switch (command) {
            case MenuCommand::None:
                break;
            case MenuCommand::Resume:
                setMenuOpen(false);
                status.message = "RESUMED";
                break;
            case MenuCommand::Restart:
                status.message = "RESTART IS NOT IMPLEMENTED";
                break;
            case MenuCommand::Quit:
                running = false;
                break;
            case MenuCommand::SaveState:
            case MenuCommand::LoadState:
            case MenuCommand::DeleteState:
                status.message = "SAVE STATES ARE NOT IMPLEMENTED";
                break;
            case MenuCommand::CreateCard1:
                createCard(0);
                break;
            case MenuCommand::CreateCard2:
                createCard(1);
                break;
            case MenuCommand::SaveCards:
                saveAllCards();
                break;
            case MenuCommand::BackupCard1:
                backupCard(0);
                break;
            case MenuCommand::BackupCard2:
                backupCard(1);
                break;
            case MenuCommand::ExportCard1:
                exportCard(0);
                break;
            case MenuCommand::ExportCard2:
                exportCard(1);
                break;
            case MenuCommand::ImportCard1:
                importCard(0);
                break;
            case MenuCommand::ImportCard2:
                importCard(1);
                break;
            case MenuCommand::SaveControllerProfile:
                saveControllerProfile(true);
                updateControllerStatus();
                break;
        }
    };

    // Per-frame GLFW joystick state: GLFW is polled, so menu navigation and
    // game input use rising-edge transitions between frames instead of the
    // former SDL button/axis events. Hotplug arrives buffered by the backend
    // callback during its refresh polls.
    auto handleControllerFrameInput = [&]() {
        JoystickEvent joyEvent;
        bool controllerListChanged = false;
        while (pollJoystickEvent(joyEvent)) {
            controllerListChanged = true;
            if (joyEvent.connected) {
                if (controllerJid < 0) {
                    openController(joyEvent.jid);
                    status.message = std::string("JOYSTICK CONNECTED: ") +
                                     glfwJoystickDisplayName(joyEvent.jid);
                }
            } else if (controllerJid == joyEvent.jid) {
                closeController();
                status.message = "GAMEPAD DISCONNECTED";
            }
        }
        controllerListDirty = controllerListDirty || controllerListChanged;

        // The refresh inside publish is also where GLFW gets its poll; keep
        // it unconditional so joystick events keep flowing while the pad is
        // closed.
        g_backend.publish();

        const ControllerSnapshot& snapshot = g_backend.lastSnapshot();
        if (g_backend.gamepadMode()) {
            const uint32_t buttons = snapshot.buttons;
            const uint32_t rising = buttons & ~menuPrevGamepadButtons;
            menuPrevGamepadButtons = buttons;
            if ((rising & (1u << kHostBtnGuide)) != 0) {
                setMenuOpen(!menu.isOpen());
            }
            if (menu.isOpen()) {
                if ((rising & (1u << kHostBtnDpadUp)) != 0) {
                    menu.move(-1);
                }
                if ((rising & (1u << kHostBtnDpadDown)) != 0) {
                    menu.move(1);
                }
                if ((rising & (1u << kHostBtnDpadLeft)) != 0) {
                    menu.adjust(settings, -1, status);
                }
                if ((rising & (1u << kHostBtnDpadRight)) != 0) {
                    menu.adjust(settings, 1, status);
                }
                if ((rising & (1u << kHostBtnA)) != 0) {
                    menu.activate(settings, status);
                }
                if ((rising & ((1u << kHostBtnB) | (1u << kHostBtnStart))) != 0) {
                    setMenuOpen(false);
                }
                if ((rising & (1u << kHostBtnRightBumper)) != 0) {
                    menu.moveTab(1);
                }
                if ((rising & (1u << kHostBtnLeftBumper)) != 0) {
                    menu.moveTab(-1);
                }
            }
            // Non-menu gamepad buttons are not translated here: the per-frame
            // publish + transaction-time refresh (glfw backend) apply the
            // active mapping instead, so HOME edits take effect immediately
            // with a single input path.
        } else {
            const uint64_t rising = snapshot.rawButtons & ~menuPrevRawButtons;
            menuPrevRawButtons = snapshot.rawButtons;
            const auto rawRising = [&](int index) {
                return (rising & (1ull << (kHostRawBtnBase + index))) != 0;
            };
            // Raw-layout guide button sits at physical button 8.
            if (rawRising(8)) {
                setMenuOpen(!menu.isOpen());
            }
            if (menu.isOpen()) {
                if (rawRising(0)) {
                    menu.activate(settings, status);
                }
                if (rawRising(1) || rawRising(7)) {
                    setMenuOpen(false);
                }
            }
        }
    };

    auto lastFrame = std::chrono::steady_clock::now();
    auto lastInputSummary = lastFrame;
    uint64_t lastSummaryTx = 0;
    uint64_t lastSummaryFrames = 0;
    uint64_t hostFrameIndex = 0;
    double cycleAccumulator = 0.0;
    double smoothedFps = 60.0;

    while (running) {
        const auto frameStart = std::chrono::steady_clock::now();
        ++hostFrameIndex;
        inputtrace::beginFrame(hostFrameIndex);
        const double elapsedSinceFrame =
            std::chrono::duration<double>(frameStart - lastFrame).count();
        lastFrame = frameStart;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                std::cerr << "SDL_QUIT received\n";
                running = false;
                continue;
            }
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                const bool pressed = event.type == SDL_KEYDOWN;
                const SDL_Keycode key = event.key.keysym.sym;
                if (pressed && (key == SDLK_HOME || key == SDLK_F1)) {
                    setMenuOpen(!menu.isOpen());
                    continue;
                }
                if (menu.isOpen()) {
                    if (pressed) {
                        switch (key) {
                            case SDLK_UP:
                                menu.move(-1);
                                break;
                            case SDLK_DOWN:
                                menu.move(1);
                                break;
                            case SDLK_LEFT:
                                menu.adjust(settings, -1, status);
                                break;
                            case SDLK_RIGHT:
                                menu.adjust(settings, 1, status);
                                break;
                            case SDLK_RETURN:
                            case SDLK_KP_ENTER:
                                menu.activate(settings, status);
                                break;
                            case SDLK_TAB:
                            case SDLK_e:
                                menu.moveTab(1);
                                break;
                            case SDLK_q:
                                menu.moveTab(-1);
                                break;
                            case SDLK_ESCAPE:
                                setMenuOpen(false);
                                break;
                            default:
                                break;
                        }
                    }
                    continue;
                }
                if (!pressed) {
                    switch (key) {
                        case SDLK_UP:
                            setButton(kPadUp, false);
                            break;
                        case SDLK_DOWN:
                            setButton(kPadDown, false);
                            break;
                        case SDLK_LEFT:
                            setButton(kPadLeft, false);
                            break;
                        case SDLK_RIGHT:
                            setButton(kPadRight, false);
                            break;
                        case SDLK_RETURN:
                            setButton(kPadStart, false);
                            break;
                        case SDLK_BACKSPACE:
                            setButton(kPadSelect, false);
                            break;
                        case SDLK_z:
                            setButton(kPadCross, false);
                            break;
                        case SDLK_x:
                            setButton(kPadCircle, false);
                            break;
                        case SDLK_a:
                            setButton(kPadSquare, false);
                            break;
                        case SDLK_s:
                            setButton(kPadTriangle, false);
                            break;
                        case SDLK_q:
                            setButton(kPadL1, false);
                            break;
                        case SDLK_w:
                            setButton(kPadL2, false);
                            break;
                        case SDLK_e:
                            setButton(kPadR1, false);
                            break;
                        case SDLK_r:
                            setButton(kPadR2, false);
                            break;
                        default:
                            break;
                    }
                    continue;
                }
                switch (key) {
                    case SDLK_UP:
                        setButton(kPadUp, true);
                        break;
                    case SDLK_DOWN:
                        setButton(kPadDown, true);
                        break;
                    case SDLK_LEFT:
                        setButton(kPadLeft, true);
                        break;
                    case SDLK_RIGHT:
                        setButton(kPadRight, true);
                        break;
                    case SDLK_RETURN:
                        setButton(kPadStart, true);
                        break;
                    case SDLK_BACKSPACE:
                        setButton(kPadSelect, true);
                        break;
                    case SDLK_z:
                        setButton(kPadCross, true);
                        break;
                    case SDLK_x:
                        setButton(kPadCircle, true);
                        break;
                    case SDLK_a:
                        setButton(kPadSquare, true);
                        break;
                    case SDLK_s:
                        setButton(kPadTriangle, true);
                        break;
                    case SDLK_q:
                        setButton(kPadL1, true);
                        break;
                    case SDLK_w:
                        setButton(kPadL2, true);
                        break;
                    case SDLK_e:
                        setButton(kPadR1, true);
                        break;
                    case SDLK_r:
                        setButton(kPadR2, true);
                        break;
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                    default:
                        break;
                }
                continue;
            }
        }
        handleControllerFrameInput();
        updateControllerStatus();
        if (menu.isOpen()) {
            status.controllerName =
                controllerJid >= 0
                    ? (glfwJoystickDisplayName(controllerJid).empty()
                           ? std::string("GAMEPAD")
                           : glfwJoystickDisplayName(controllerJid))
                    : std::string("KEYBOARD");
        }

        handleCommand(menu.takeCommand());
        if (menu.takeSettingsChanged()) {
            if (settings.controllerGuid != activeControllerGuid) {
                if (openControllerByGuid(settings.controllerGuid)) {
                    activeControllerGuid = settings.controllerGuid;
                    if (!loadControllerProfile(activeControllerGuid)) {
                        settings.controllerMapping = ControllerMapping::defaults();
                        saveControllerProfile(false);
                    }
                    updateControllerName();
                    status.message = "GAMEPAD SWITCHED: " + status.controllerName;
                } else {
                    status.message = "GAMEPAD NOT FOUND: " + settings.controllerGuid;
                    settings.controllerGuid = activeControllerGuid;
                }
            }
            g_backend.setMapping(settings.controllerMapping);
            config.setString("input.controller.guid", settings.controllerGuid);
            saveControllerProfile(false);
            saveSettings();
            applyRuntimeSettings(false);
            syncCard(card1, 0, settings, system, status, false);
            syncCard(card2, 1, settings, system, status, false);
            refreshCardChoices(settings, status);
            updateControllerStatus();
        }

        syncPad();

        if (autoStartPeriodSeconds > 0.0 && !menu.isOpen()) {
            const uint64_t lattice = system.vblankFrames() %
                                     static_cast<uint64_t>(autoStartPeriodSeconds * 60.0 + 0.5);
            g_padState.setKeyboardButton(kPadStart, lattice < 12u);
        }

        auto writeVramDump = [&](const std::string& target) {
            std::ofstream out(target, std::ios::binary);
            if (!out) {
                return;
            }
            const auto& vram = system.gpu().vram();
            out << "P6\n" << Gpu::kVramWidth << " " << Gpu::kVramHeight << "\n255\n";
            for (uint16_t pixel : vram) {
                const uint8_t r = static_cast<uint8_t>((pixel & 0x1Fu) << 3);
                const uint8_t g = static_cast<uint8_t>(((pixel >> 5u) & 0x1Fu) << 3);
                const uint8_t b = static_cast<uint8_t>(((pixel >> 10u) & 0x1Fu) << 3);
                const char rgb[3] = {static_cast<char>(r), static_cast<char>(g),
                                     static_cast<char>(b)};
                out.write(rgb, 3);
            }
        };

        double cpuTimeMs = 0.0;
        if (!menu.isOpen() || !settings.pauseOnMenu) {
            double delta = elapsedSinceFrame;
            delta = std::clamp(delta, 0.0, 0.1);
            cycleAccumulator += delta * kCpuClock;
            const double maximumCycles = kCpuClock / 10.0;
            const uint32_t cycles = static_cast<uint32_t>(
                std::min(cycleAccumulator, maximumCycles));
            cycleAccumulator -= static_cast<double>(cycles);
            if (cycles > 0) {
                const auto cpuStart = std::chrono::steady_clock::now();
                const CpuRunResult result = cpu.run(cycles);
                cpuTimeMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - cpuStart)
                                .count();
                totalSteps += result.steps;
                if (result.reason != CpuStopReason::StepLimit &&
                    result.reason != CpuStopReason::Syscall &&
                    result.reason != CpuStopReason::Breakpoint) {
                    std::cerr << "CPU stop: " << toString(result.reason)
                              << " pc=" << result.pc << "\n";
                    running = false;
                }
            }
        } else {
            cycleAccumulator = 0.0;
        }

        uint32_t srcX = system.gpu().displayStartX();
        uint32_t srcY = system.gpu().displayStartY();
        uint32_t srcW = system.gpu().displayWidth();
        uint32_t srcH = system.gpu().displayHeight();
        if (srcX >= Gpu::kVramWidth) {
            srcW = 0;
        } else if (srcW > Gpu::kVramWidth - srcX) {
            srcW = Gpu::kVramWidth - srcX;
        }
        if (srcY >= Gpu::kVramHeight) {
            srcH = 0;
        } else if (srcH > Gpu::kVramHeight - srcY) {
            srcH = Gpu::kVramHeight - srcY;
        }

        if (useVulkan) {
#if defined(PSXREBORN_HAS_VULKAN)
            const int outputWidth = vulkanPresenter->width();
            const int outputHeight = vulkanPresenter->height();
            if (menu.isOpen() && outputWidth > 0 && outputHeight > 0) {
                overlayCanvas.resize(outputWidth, outputHeight);
                renderMenuOverlay(overlayCanvas, menu, settings, status);
            }
            const uint8_t* overlayPixels =
                menu.isOpen() && !overlayCanvas.pixels.empty() ? overlayCanvas.pixels.data()
                                                               : nullptr;
            std::string vulkanError;
            if (!vulkanPresenter->present(
                    system.gpu().vram().data(), srcX, srcY, srcW, srcH,
                    !system.gpu().displayDisabled(), menu.isOpen(), overlayPixels,
                    overlayCanvas.width, overlayCanvas.height, settings.integerScale,
                    settings.aspect != AspectMode::Stretch, &vulkanError)) {
                std::cerr << "[VIDEO] Vulkan present failed: " << vulkanError << "\n";
                running = false;
            }
#endif
        } else {
            void* pixels = nullptr;
            int pitch = 0;
            if (SDL_LockTexture(texture, nullptr, &pixels, &pitch) == 0) {
                uint32_t* destination = static_cast<uint32_t*>(pixels);
                const auto& vram = system.gpu().vram();
                for (uint32_t y = 0; y < 512; ++y) {
                    for (uint32_t x = 0; x < 1024; ++x) {
                        destination[y * (pitch / 4) + x] = color555To888(vram[y * 1024 + x]);
                    }
                }
                SDL_UnlockTexture(texture);
            }

            int outputWidth = 0;
            int outputHeight = 0;
            SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight);
            SDL_RenderClear(renderer);
            if (!system.gpu().displayDisabled()) {
                if (srcW != 0 && srcH != 0 && outputWidth > 0 && outputHeight > 0) {
                    const SDL_Rect source{static_cast<int>(srcX), static_cast<int>(srcY),
                                          static_cast<int>(srcW), static_cast<int>(srcH)};
                    SDL_Rect destination{0, 0, outputWidth, outputHeight};
                    if (settings.aspect != AspectMode::Stretch) {
                        const double targetAspect = 4.0 / 3.0;
                        int width = outputWidth;
                        int height = static_cast<int>(width / targetAspect);
                        if (height > outputHeight) {
                            height = outputHeight;
                            width = static_cast<int>(height * targetAspect);
                        }
                        destination = {(outputWidth - width) / 2,
                                       (outputHeight - height) / 2, width, height};
                    }
                    if (settings.integerScale) {
                        const int scaleX = outputWidth / static_cast<int>(srcW);
                        const int scaleY = outputHeight / static_cast<int>(srcH);
                        const int scale = std::min(scaleX, scaleY);
                        if (scale >= 1) {
                            const int width = static_cast<int>(srcW) * scale;
                            const int height = static_cast<int>(srcH) * scale;
                            destination = {(outputWidth - width) / 2,
                                           (outputHeight - height) / 2, width, height};
                        }
                    }
                    SDL_RenderCopy(renderer, texture, &source, &destination);
                }
            }

            if (menu.isOpen() && outputWidth > 0 && outputHeight > 0) {
                if (overlayTexture == nullptr || overlayWidth != outputWidth ||
                    overlayHeight != outputHeight) {
                    if (overlayTexture != nullptr) {
                        SDL_DestroyTexture(overlayTexture);
                    }
                    overlayTexture =
                        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, outputWidth,
                                          outputHeight);
                    overlayWidth = outputWidth;
                    overlayHeight = outputHeight;
                    if (overlayTexture != nullptr) {
                        SDL_SetTextureBlendMode(overlayTexture, SDL_BLENDMODE_BLEND);
                    }
                }
                if (overlayTexture != nullptr) {
                    overlayCanvas.resize(outputWidth, outputHeight);
                    renderMenuOverlay(overlayCanvas, menu, settings, status);
                    SDL_UpdateTexture(overlayTexture, nullptr, overlayCanvas.pixels.data(),
                                      overlayCanvas.width * 4);
                    const SDL_Rect destination{0, 0, outputWidth, outputHeight};
                    SDL_RenderCopy(renderer, overlayTexture, nullptr, &destination);
                }
            }
            SDL_RenderPresent(renderer);
        }

        status.fps = smoothedFps;
        status.frameTimeMs = elapsedSinceFrame * 1000.0;
        status.cpuTimeMs = cpuTimeMs;
        status.executionMode = cpu.jitEnabled() ? "JIT" : "INTERPRETER";
        status.jitActive = cpu.jitEnabled();
        status.jitBlocks = cpu.jitCompiledBlocks();
        status.jitExecuted = cpu.jitExecutedBlocks();

        elapsedVirtualSeconds = static_cast<double>(system.vblankFrames()) / 60.0;
        if (snapshotPeriodSeconds > 0.0 && elapsedVirtualSeconds >= nextSnapshotAt) {
            nextSnapshotAt = elapsedVirtualSeconds + snapshotPeriodSeconds;
            const GpuStats& gpuStats = system.gpu().stats();
            const DmaStats& dmaStats = system.dma().stats();
            const CdromStats& cdromStats = system.cdrom().stats();
            const InterruptStats& irqStats = system.interrupts().stats();
            const CpuStats& cpuStats = cpu.stats();
            const auto u = [](uint64_t v) { return static_cast<unsigned long long>(v); };
            // Non-black fraction inside the currently displayed VRAM window.
            auto displayWindowFill = [&]() {
                uint32_t originX = system.gpu().displayStartX();
                uint32_t originY = system.gpu().displayStartY();
                uint32_t width = system.gpu().displayWidth();
                uint32_t height = system.gpu().displayHeight();
                const uint32_t doubleHeightPixels =
                    system.gpu().displayMode() & 0x10u ? 0 : height * 2;
                (void)doubleHeightPixels;
                if (originX >= Gpu::kVramWidth) return 0.0;
                if (originX + width > Gpu::kVramWidth) width = Gpu::kVramWidth - originX;
                if (originY >= Gpu::kVramHeight) return 0.0;
                if (originY + height > Gpu::kVramHeight) height = Gpu::kVramHeight - originY;
                if (width == 0 || height == 0) return 0.0;
                const auto& vram = system.gpu().vram();
                uint64_t nonblack = 0;
                uint64_t total = 0;
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        const uint16_t pixel = vram[(originY + y) * Gpu::kVramWidth + originX + x];
                        ++total;
                        if (pixel != 0) {
                            ++nonblack;
                        }
                    }
                }
                return total != 0 ? 100.0 * static_cast<double>(nonblack) /
                                        static_cast<double>(total)
                                  : 0.0;
            };
            std::fprintf(stderr,
                         "[SNAPSHOT] vt=%.1fs fps=%.1f pc=%08x steps=%llu vblanks=%llu "
                         "disp=%ux%u@(%u,%u) mode=%u dis=%u fill=%.1f%%\n"
                         "  gpu: cmd=%llu poly=%llu rect=%llu fill=%llu copy=%llu cpuv=%llu "
                         "vramc=%llu gp1=%llu twr=%llu px=%llu\n"
                         "  irq: v=%llu g=%llu c=%llu d=%llu t0=%llu t1=%llu t2=%llu "
                         "pad=%llu sio=%llu spu=%llu\n"
                         "  exc: int=%llu oth=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                         "dma: st=%llu,%llu,%llu,%llu,%llu,%llu,%llu "
                         "w=%llu,%llu,%llu,%llu,%llu,%llu,%llu f=%llu,%llu,%llu,%llu,%llu,%llu,%llu\n"
                         "  cd: cmd=%llu unk=%llu err=%llu sec=%llu dmaw=%llu\n",
                         elapsedVirtualSeconds, smoothedFps, cpu.pc(), u(totalSteps),
                         u(system.vblankFrames()), system.gpu().displayWidth(),
                         system.gpu().displayHeight(), system.gpu().displayStartX(),
                         system.gpu().displayStartY(), system.gpu().displayMode() & 0xFu,
                         system.gpu().displayDisabled() ? 1u : 0u, displayWindowFill(),
                         u(gpuStats.gp0Commands), u(gpuStats.polygons), u(gpuStats.rectangles),
                         u(gpuStats.fills), u(gpuStats.copies), u(gpuStats.cpuToVram),
                         u(gpuStats.vramToCpu), u(gpuStats.gp1Commands), u(gpuStats.transferWords),
                         u(gpuStats.pixelsDrawn), u(irqStats.requests[0]),
                         u(irqStats.requests[1]), u(irqStats.requests[2]),
                         u(irqStats.requests[3]), u(irqStats.requests[4]),
                         u(irqStats.requests[5]), u(irqStats.requests[6]),
                         u(irqStats.requests[7]), u(irqStats.requests[8]),
                         u(irqStats.requests[9]), u(cpuStats.interruptExceptions),
                         u(cpuStats.exceptions[4]), u(cpuStats.exceptions[5]),
                         u(cpuStats.exceptions[6]), u(cpuStats.exceptions[7]),
                         u(cpuStats.exceptions[8]), u(cpuStats.exceptions[9]),
                         u(cpuStats.exceptions[10]), u(cpuStats.exceptions[12]),
                         u(dmaStats.starts[0]), u(dmaStats.starts[1]), u(dmaStats.starts[2]),
                         u(dmaStats.starts[3]), u(dmaStats.starts[4]), u(dmaStats.starts[5]),
                         u(dmaStats.starts[6]), u(dmaStats.words[0]), u(dmaStats.words[1]),
                         u(dmaStats.words[2]), u(dmaStats.words[3]), u(dmaStats.words[4]),
                         u(dmaStats.words[5]), u(dmaStats.words[6]), u(dmaStats.fails[0]),
                         u(dmaStats.fails[1]), u(dmaStats.fails[2]), u(dmaStats.fails[3]),
                         u(dmaStats.fails[4]), u(dmaStats.fails[5]), u(dmaStats.fails[6]),
                         u(cdromStats.commands), u(cdromStats.unknownCommands),
                         u(cdromStats.failures), u(cdromStats.sectorsRead),
                         u(cdromStats.dmaWords));
            std::fflush(stderr);
        }
        if (dumpPeriodSeconds > 0.0 && dumpPath != nullptr &&
            elapsedVirtualSeconds >= nextDumpAt) {
            nextDumpAt = elapsedVirtualSeconds + dumpPeriodSeconds;
            writeVramDump(std::string(dumpPath) + "." + std::to_string(dumpIndex) + ".ppm");
            ++dumpIndex;
        }

        if (dumpPath != nullptr && dumpSteps != 0.0 && totalSteps >= dumpSteps) {
            std::cerr << "[PLAY] display x=" << system.gpu().displayStartX()
                      << " y=" << system.gpu().displayStartY()
                      << " w=" << system.gpu().displayWidth()
                      << " h=" << system.gpu().displayHeight()
                      << " mode=0x" << std::hex << system.gpu().displayMode() << std::dec
                      << " disabled=" << system.gpu().displayDisabled() << "\n";
            writeVramDump(dumpPath);
            running = false;
        }

        const double measuredFps =
            elapsedSinceFrame > 0.0 ? 1.0 / elapsedSinceFrame : smoothedFps;
        smoothedFps = smoothedFps * 0.9 + measuredFps * 0.1;

        if (inputtrace::enabled() && frameStart >= lastInputSummary + std::chrono::seconds(1)) {
            const inputtrace::Summary summaryNow = inputtrace::summary();
            const double window =
                std::chrono::duration<double>(frameStart - lastInputSummary).count();
            const double txPerSecond =
                window > 0.0
                    ? static_cast<double>(summaryNow.transactions - lastSummaryTx) / window
                    : 0.0;
            const double hostFps =
                window > 0.0
                    ? static_cast<double>(hostFrameIndex - lastSummaryFrames) / window
                    : 0.0;
            inputtrace::logLine(
                "ev=frame n=%llu fps=%.1f pub=%llu push=%llu tx=%llu tps=%.0f avg_us=%llu "
                "max_us=%llu",
                static_cast<unsigned long long>(hostFrameIndex), hostFps,
                static_cast<unsigned long long>(summaryNow.publishes),
                static_cast<unsigned long long>(summaryNow.pushes),
                static_cast<unsigned long long>(summaryNow.transactions), txPerSecond,
                static_cast<unsigned long long>(summaryNow.avgTxIntervalUs),
                static_cast<unsigned long long>(summaryNow.maxTxIntervalUs));
            lastInputSummary = frameStart;
            lastSummaryTx = summaryNow.transactions;
            lastSummaryFrames = hostFrameIndex;
        }

        const uint32_t elapsed =
            static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - frameStart)
                                      .count());
        const uint32_t target =
            settings.fpsLimit > 0 ? static_cast<uint32_t>(1000 / settings.fpsLimit) : 0;
        if (target > 0 && elapsed < target) {
            SDL_Delay(target - elapsed);
        }
        lastFrame = frameStart;
    }

    system.saveDirtyMemoryCards();
    if (menu.takeSettingsChanged()) {
        saveSettings();
    }
    if (overlayTexture != nullptr) {
        SDL_DestroyTexture(overlayTexture);
    }
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
    }
    if (renderer != nullptr) {
        SDL_DestroyRenderer(renderer);
    }
#if defined(PSXREBORN_HAS_VULKAN)
    vulkanPresenter.reset();
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    glfwInputShutdown();
    return 0;
}
