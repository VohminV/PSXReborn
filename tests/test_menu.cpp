#include <algorithm>
#include <ostream>
#include <string>

#include "psxreborn/config.hpp"
#include "psxreborn/menu.hpp"
#include "psxreborn/overlay.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace psxreborn {

std::ostream& operator<<(std::ostream& stream, MenuTab tab) {
    return stream << menuTabName(tab);
}

std::ostream& operator<<(std::ostream& stream, MenuCommand command) {
    return stream << "MenuCommand(" << static_cast<int>(command) << ")";
}

}  // namespace psxreborn

namespace {

MenuStatus baseStatus() {
    MenuStatus status;
    status.gameId = "SLES_040.64";
    status.imagePath = "Five Star Racing.cue";
    status.region = "PAL";
    status.executionMode = "INTERPRETER";
    status.backendName = "SDL";
    status.audioOutputAvailable = false;
    return status;
}

}  // namespace

TEST_CASE(menu_settings_roundtrip_through_config) {
    Config config;
    MenuSettings settings;
    settings.backend = RenderBackend::Vulkan;
    settings.scale = ScaleMode::Nearest;
    settings.aspect = AspectMode::FourThree;
    settings.fsrEnabled = false;
    settings.fsrSharpness = 0.75;
    settings.resolutionIndex = 5;
    settings.fullscreen = true;
    settings.vsync = false;
    settings.fpsLimit = 120;
    settings.integerScale = true;
    settings.jit = true;
    settings.mouseCapture = true;
    settings.mouseSensitivity = 2.25;
    settings.invertY = true;
    settings.smoothing = true;
    settings.mute = true;
    settings.volume = 0.25;
    settings.card1Enabled = false;
    settings.card2Path = "/tmp/card2.mcd";
    settings.save(config);

    MenuSettings loaded;
    loaded.load(config);
    CHECK(loaded.backend == RenderBackend::Vulkan);
    CHECK(loaded.scale == ScaleMode::Nearest);
    CHECK(loaded.aspect == AspectMode::FourThree);
    CHECK_EQ(loaded.fsrEnabled, false);
    CHECK(loaded.fsrSharpness > 0.74 && loaded.fsrSharpness < 0.76);
    CHECK_EQ(loaded.resolutionIndex, 5);
    CHECK_EQ(loaded.fullscreen, true);
    CHECK_EQ(loaded.vsync, false);
    CHECK_EQ(loaded.fpsLimit, 120);
    CHECK_EQ(loaded.integerScale, true);
    CHECK_EQ(loaded.jit, true);
    CHECK_EQ(loaded.mouseCapture, true);
    CHECK(loaded.mouseSensitivity > 2.24);
    CHECK_EQ(loaded.invertY, true);
    CHECK_EQ(loaded.smoothing, true);
    CHECK_EQ(loaded.mute, true);
    CHECK(loaded.volume > 0.24 && loaded.volume < 0.26);
    CHECK_EQ(loaded.card1Enabled, false);
    CHECK_EQ(loaded.card2Path, std::string("/tmp/card2.mcd"));
}

TEST_CASE(menu_tabs_and_navigation) {
    MenuSettings settings;
    MenuStatus status = baseStatus();
    Menu menu;
    CHECK_EQ(menu.tab(), MenuTab::Graphics);
    CHECK(!menu.isOpen());
    menu.toggle();
    CHECK(menu.isOpen());

    const std::vector<MenuItem> graphics = menu.items(settings, status);
    CHECK(graphics.size() >= 10);
    CHECK_EQ(graphics[0].label, std::string("RESOLUTION"));
    CHECK_EQ(graphics[0].value, std::string("1280X960"));

    menu.setSelected(1);
    menu.activate(settings, status);
    CHECK_EQ(settings.fullscreen, true);
    menu.activate(settings, status);
    CHECK_EQ(settings.fullscreen, false);

    menu.move(4);
    CHECK_EQ(menu.selected(), std::size_t(5));
    menu.move(-100);
    CHECK_EQ(menu.selected(), std::size_t(0));
    menu.move(1000);
    CHECK(menu.selected() < graphics.size());

    menu.setTab(MenuTab::MemoryCards);
    CHECK_EQ(menu.tab(), MenuTab::MemoryCards);
    CHECK_EQ(menu.selected(), std::size_t(0));
    menu.moveTab(1);
    CHECK_EQ(menu.tab(), MenuTab::SaveStates);
    menu.moveTab(-2);
    CHECK_EQ(menu.tab(), MenuTab::MouseCamera);
    menu.setTab(MenuTab::Count);
    CHECK_EQ(menu.tab(), MenuTab::MouseCamera);
}

TEST_CASE(menu_backend_cycle_respects_vulkan_availability) {
    MenuSettings settings;
    MenuStatus status = baseStatus();
    status.vulkanAvailable = false;
    Menu menu;
    menu.setSelected(2);
    menu.activate(settings, status);
    CHECK(settings.backend == RenderBackend::Sdl);
    menu.activate(settings, status);
    CHECK(settings.backend == RenderBackend::Auto);

    status.vulkanAvailable = true;
    menu.activate(settings, status);
    CHECK(settings.backend == RenderBackend::Sdl);
    menu.activate(settings, status);
    CHECK(settings.backend == RenderBackend::Vulkan);
    menu.activate(settings, status);
    CHECK(settings.backend == RenderBackend::Auto);
}

TEST_CASE(menu_upscaling_requires_fsr_availability) {
    MenuSettings settings;
    MenuStatus status = baseStatus();
    status.fsrAvailable = false;
    Menu menu;
    menu.setTab(MenuTab::Upscaling);
    const std::vector<MenuItem> items = menu.items(settings, status);
    CHECK(!items[0].enabled);
    menu.activate(settings, status);
    CHECK_EQ(settings.fsrEnabled, true);

    status.fsrAvailable = true;
    menu.activate(settings, status);
    CHECK_EQ(settings.fsrEnabled, false);
    CHECK(settings.scale == ScaleMode::Linear);
    menu.activate(settings, status);
    CHECK_EQ(settings.fsrEnabled, true);
    CHECK(settings.scale == ScaleMode::Fsr1);
}

TEST_CASE(menu_memory_card_selection) {
    MenuSettings settings;
    MenuStatus status = baseStatus();
    status.cardChoices[0] = {"alpha.mcd", "beta.mcd", "gamma.mcd"};
    status.cardPresent[0] = true;
    Menu menu;
    menu.setTab(MenuTab::MemoryCards);
    menu.setSelected(1);
    menu.adjust(settings, 1, status);
    CHECK_EQ(settings.card1Path, std::string("alpha.mcd"));
    menu.adjust(settings, 1, status);
    CHECK_EQ(settings.card1Path, std::string("beta.mcd"));
    menu.adjust(settings, -1, status);
    CHECK_EQ(settings.card1Path, std::string("alpha.mcd"));
    menu.adjust(settings, -1, status);
    CHECK_EQ(settings.card1Path, std::string("gamma.mcd"));
    CHECK_EQ(menu.takeCommand(), MenuCommand::None);
    CHECK(menu.takeSettingsChanged());
    CHECK(!menu.takeSettingsChanged());

    menu.setSelected(8);
    menu.activate(settings, status);
    CHECK_EQ(menu.takeCommand(), MenuCommand::SaveCards);
}

TEST_CASE(menu_game_commands) {
    MenuSettings settings;
    MenuStatus status = baseStatus();
    Menu menu;
    menu.open();
    menu.setTab(MenuTab::Game);
    menu.activate(settings, status);
    CHECK_EQ(menu.takeCommand(), MenuCommand::Resume);
    CHECK(!menu.isOpen());

    menu.open();
    menu.setSelected(1);
    menu.activate(settings, status);
    CHECK_EQ(menu.takeCommand(), MenuCommand::None);
    CHECK(menu.isOpen());
    menu.close();

    menu.open();
    menu.setSelected(2);
    menu.activate(settings, status);
    CHECK_EQ(menu.takeCommand(), MenuCommand::Quit);
    CHECK(!menu.isOpen());
}

TEST_CASE(menu_actions_do_not_emit_commands) {
    MenuSettings settings;
    MenuStatus status = baseStatus();
    Menu menu;
    menu.activate(settings, status);
    CHECK_EQ(menu.takeCommand(), MenuCommand::None);
    CHECK_EQ(settings.resolutionIndex, 3);
    CHECK(menu.takeSettingsChanged());
}

TEST_CASE(overlay_renders_panel_and_selection) {
    MenuSettings settings;
    MenuStatus status = baseStatus();
    Menu menu;
    menu.open();
    OverlayCanvas canvas;
    canvas.resize(1280, 720);
    renderMenuOverlay(canvas, menu, settings, status);
    CHECK_EQ(canvas.width, 1280);
    CHECK_EQ(canvas.height, 720);
    CHECK_EQ(static_cast<int>(canvas.pixels.size()), 1280 * 720 * 4);
    CHECK_EQ(canvas.pixels[3], 0);

    std::size_t opaque = 0;
    for (std::size_t index = 3; index < canvas.pixels.size(); index += 4) {
        if (canvas.pixels[index] != 0) {
            ++opaque;
        }
    }
    CHECK(opaque > 100000);

    const std::vector<MenuItem> graphics = menu.items(settings, status);
    CHECK(!graphics.empty());
    menu.move(1);
    OverlayCanvas second;
    second.resize(1280, 720);
    renderMenuOverlay(second, menu, settings, status);
    CHECK(second.pixels != canvas.pixels);

    OverlayCanvas tiny;
    tiny.resize(320, 240);
    renderMenuOverlay(tiny, menu, settings, status);
    CHECK_EQ(static_cast<int>(tiny.pixels.size()), 320 * 240 * 4);
    CHECK(tiny.textWidth("ABC", 2) > 0);
    CHECK(tiny.lineHeight(2) > 0);
}
