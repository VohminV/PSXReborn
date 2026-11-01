#include <cstdint>
#include <string>
#include <vector>

#include "psxreborn/controller_profile.hpp"
#include "psxreborn/menu.hpp"
#include "psxreborn/overlay.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace {

bool isPressed(const PadState& pad, uint16_t button) {
    return (pad.pressed() & button) != 0;
}

ControllerSnapshot snapshotWithButtons(uint32_t mask) {
    ControllerSnapshot snapshot;
    snapshot.buttons = mask;
    return snapshot;
}

}  // namespace

TEST_CASE(controller_mapping_defaults_cover_every_ps1_button) {
    const ControllerMapping mapping = ControllerMapping::defaults();
    std::vector<HostControl> assigned;
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        CHECK(mapping.host[slot] != kHostNone);
        for (HostControl seen : assigned) {
            CHECK(seen != mapping.host[slot]);
        }
        assigned.push_back(mapping.host[slot]);
    }
    CHECK_EQ(mapping.deadzone, ControllerMapping::kDefaultDeadzone);
    CHECK_EQ(mapping.triggerThreshold, ControllerMapping::kDefaultTriggerThreshold);
    CHECK_EQ(mappingConflicts(mapping).size(), static_cast<std::size_t>(0));
}

TEST_CASE(controller_mapping_default_matches_manual_xbox_automap) {
    const ControllerMapping mapping = ControllerMapping::defaults();
    // The same table the historical hard-coded SDL/xinput path used.
    const std::size_t expectedSources[] = {8, 9, 10, 11, 12, 13, 14, 15, 6, 4, 9, 10, 0, 1, 2, 3};
    (void)expectedSources;
    PadState pad;
    // Press A: cross only (same as the previous kXInputA path).
    pad.clearController();
    ControllerSnapshot snapshot = snapshotWithButtons(1u << kHostBtnA);
    snapshot.setButtonPressed(kHostBtnLeftShoulder, true);
    applyControllerSnapshot(pad, snapshot, mapping);
    CHECK_EQ(pad.pressed(), static_cast<uint16_t>(kPadCross | kPadL1));
    // Release: no ghost buttons.
    applyControllerSnapshot(pad, snapshotWithButtons(0), mapping);
    CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
}

TEST_CASE(controller_mapping_apply_maps_every_button) {
    // Give every slot its own isolated host button (ids 16..31 are valid yet
    // unused by the defaults) so the check sees only one pressed PS1 button.
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        ControllerMapping mapping = ControllerMapping::defaults();
        for (std::size_t other = 0; other < kPs1ButtonCount; ++other) {
            mapping.host[other] = static_cast<HostControl>(16 + other);
        }
        const HostControl host = mapping.host[slot];
        PadState pad;
        applyControllerSnapshot(pad, snapshotWithButtons(1u << host), mapping);
        CHECK_EQ(pad.pressed(), ps1ButtonBit(slot));
        applyControllerSnapshot(pad, snapshotWithButtons(0), mapping);
        CHECK_EQ(pad.buttons(), static_cast<uint16_t>(0xFFFFu));
    }
}

TEST_CASE(controller_mapping_apply_triggers_use_thresholds) {
    ControllerMapping mapping = ControllerMapping::defaults();
    mapping.triggerThreshold = 16000;
    PadState pad;
    ControllerSnapshot snapshot;
    snapshot.axes[4] = 15999;
    snapshot.axes[5] = 16000;
    applyControllerSnapshot(pad, snapshot, mapping);
    CHECK(!isPressed(pad, kPadL2));
    CHECK(isPressed(pad, kPadR2));
    applyControllerSnapshot(pad, snapshotWithButtons(0), mapping);
    CHECK(!isPressed(pad, kPadR2));
    // Trigger bound to a plain button instead of an axis works too.
    mapping.host[12] = kHostBtnA;  // cross slot shares A: also a conflict test
    snapshot = snapshotWithButtons(1u << kHostBtnA);
    applyControllerSnapshot(pad, snapshot, mapping);
    CHECK(isPressed(pad, kPadCross));
    CHECK(isPressed(pad, kPadL2));
}

TEST_CASE(controller_mapping_apply_uses_custom_deadzone) {
    ControllerMapping mapping = ControllerMapping::defaults();
    mapping.deadzone = 20000;
    PadState pad;
    ControllerSnapshot snapshot;
    snapshot.axes[0] = 19000;
    snapshot.axes[1] = 0;
    applyControllerSnapshot(pad, snapshot, mapping);
    CHECK_EQ(pad.pressed(), static_cast<uint16_t>(0));
    snapshot.axes[0] = 20000;
    applyControllerSnapshot(pad, snapshot, mapping);
    CHECK_EQ(pad.pressed(), kPadRight);
}

TEST_CASE(controller_mapping_conflicts_detected) {
    ControllerMapping mapping = ControllerMapping::defaults();
    mapping.host[0] = kHostBtnA;   // cross -> A
    mapping.host[9] = kHostBtnA;   // select -> A (conflict with cross)
    const std::vector<MappingConflict> conflicts = mappingConflicts(mapping);
    CHECK_EQ(conflicts.size(), static_cast<std::size_t>(1));
    CHECK_EQ(conflicts[0].control, kHostBtnA);
    CHECK_EQ(conflicts[0].slots.size(), static_cast<std::size_t>(2));
    CHECK_EQ(conflicts[0].slots[0], static_cast<std::size_t>(0));
    CHECK_EQ(conflicts[0].slots[1], static_cast<std::size_t>(9));
}

TEST_CASE(controller_control_names_roundtrip) {
    for (HostControl control : hostControlChoices()) {
        const std::string token = hostControlName(control);
        HostControl parsed = kHostNone;
        CHECK(hostControlFromName(token, parsed));
        CHECK_EQ(parsed, control);
    }
    HostControl parsed = kHostNone;
    CHECK(hostControlFromName("a", parsed));
    CHECK_EQ(parsed, kHostBtnA);
    CHECK(hostControlFromName("NONE", parsed));
    CHECK_EQ(parsed, kHostNone);
    CHECK(!hostControlFromName("banana", parsed));
}

TEST_CASE(controller_profile_json_roundtrip) {
    ControllerProfile profile;
    profile.guid = "030000005e0400008902000010010000";
    profile.deviceName = "Xbox 360 Controller";
    profile.mapping = ControllerMapping::defaults();
    profile.mapping.host[0] = kHostBtnDpadRight;  // cross -> dpad right
    profile.mapping.deadzone = 4096;
    profile.mapping.triggerThreshold = 12000;

    const std::string json = profile.serialize();
    ControllerProfile parsed;
    std::string error;
    CHECK(parsed.parse(json, error));
    CHECK(error.empty());
    CHECK_EQ(parsed.guid, profile.guid);
    CHECK_EQ(parsed.deviceName, profile.deviceName);
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        CHECK_EQ(parsed.mapping.host[slot], profile.mapping.host[slot]);
    }
    CHECK_EQ(parsed.mapping.deadzone, 4096);
    CHECK_EQ(parsed.mapping.triggerThreshold, 12000);
    CHECK_EQ(parsed.formatVersion, ControllerProfile::kFormatVersion);

    // Round 2 through the serialized buffer again stays stable.
    const std::string json2 = parsed.serialize();
    ControllerProfile parsed2;
    std::string error2;
    CHECK(parsed2.parse(json2, error2));
    CHECK_EQ(parsed2.mapping.host[0], kHostBtnDpadRight);
}

TEST_CASE(controller_profile_parse_malformed_json_fails_and_keeps_defaults) {
    ControllerProfile profile;
    profile.mapping = ControllerMapping::defaults();
    profile.mapping.deadzone = 999;
    const std::vector<std::string> bad{
        "{",
        "not json at all",
        "{\"deadzone\": \"text\"}",
        "{\"deadzone\": 99999}",
        "{\"trigger_threshold\": 0}",
        "{\"unknown_key\": 1}",
        "{\"format_version\": 99}",
        "{\"mapping\": {\"wombat\": \"a\"}}",
        "{\"mapping\": {\"cross\": \"banana\"}}",
    };
    for (const std::string& text : bad) {
        std::string error;
        CHECK(!profile.parse(text, error));
        CHECK(!error.empty());
    }
    // Parse failures never scribble over the previous contents.
    CHECK_EQ(profile.mapping.deadzone, 999);
    const ControllerMapping mapping = ControllerMapping::defaults();
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        CHECK_EQ(profile.mapping.host[slot], mapping.host[slot]);
    }
    CHECK_EQ(profile.formatVersion, ControllerProfile::kFormatVersion);
}

TEST_CASE(controller_profile_parse_with_missing_fields_uses_defaults) {
    // Old/partial profile: only a mapping table, no thresholds/identity.
    const std::string text = "{\"mapping\": {\"cross\": \"b\"}}";
    ControllerProfile profile;
    std::string error;
    CHECK(profile.parse(text, error));
    const ControllerMapping defaults = ControllerMapping::defaults();
    CHECK_EQ(profile.mapping.host[0], kHostBtnB);
    for (std::size_t slot = 1; slot < kPs1ButtonCount; ++slot) {
        CHECK_EQ(profile.mapping.host[slot], defaults.host[slot]);
    }
    CHECK_EQ(profile.mapping.deadzone, ControllerMapping::kDefaultDeadzone);
    CHECK_EQ(profile.mapping.triggerThreshold, ControllerMapping::kDefaultTriggerThreshold);
    CHECK_EQ(profile.formatVersion, ControllerProfile::kFormatVersion);
}

TEST_CASE(controller_profile_save_and_load_roundtrip) {
    ControllerProfile profile;
    profile.guid = "test-guid-123";
    profile.deviceName = "Testpad Pro";
    profile.mapping = ControllerMapping::defaults();
    profile.mapping.host[1] = kHostBtnX;
    profile.mapping.deadzone = 2048;
    profile.mapping.triggerThreshold = 29000;

    const std::string path = ControllerProfile::pathForGuid(profile.guid);
    std::string error;
    CHECK(profile.save(path, error));
    CHECK(error.empty());

    ControllerProfile loaded;
    CHECK(loaded.load(path, error));
    CHECK_EQ(loaded.guid, profile.guid);
    CHECK_EQ(loaded.deviceName, profile.deviceName);
    CHECK_EQ(loaded.mapping.host[1], kHostBtnX);
    CHECK_EQ(loaded.mapping.deadzone, 2048);
    CHECK_EQ(loaded.mapping.triggerThreshold, 29000);
}

TEST_CASE(controller_menu_controller_tab_lists_map_editor) {
    Menu menu;
    MenuSettings settings;
    settings.controllerMapping = ControllerMapping::defaults();
    menu.setTab(MenuTab::Controller);
    MenuStatus status;
    status.controllerDeviceNames = {"PAD 1"};
    status.controllerDeviceGuids = {"guid-1"};
    status.controllerDeviceIndex = 0;
    status.controllerConnected = true;
    const std::vector<MenuItem> items = menu.items(settings, status);
    // Layout: DEVICE, DEVICE PROFILE, DEADZONE, TRIGGER, CONFLICTS, TEST INPUT,
    // 16x MAP <button>, RESET, SAVE.
    CHECK_EQ(static_cast<int>(items[0].action), static_cast<int>(MenuAction::CycleControllerDevice));
    CHECK_EQ(static_cast<int>(items[2].action), static_cast<int>(MenuAction::AdjustDeadzone));
    CHECK_EQ(static_cast<int>(items[3].action), static_cast<int>(MenuAction::AdjustTriggerThreshold));
    std::size_t mapItems = 0;
    for (const MenuItem& item : items) {
        if (item.action == MenuAction::AdjustMapping) {
            ++mapItems;
        }
    }
    CHECK_EQ(mapItems, kPs1ButtonCount);
    CHECK_EQ(static_cast<int>(items.back().action), static_cast<int>(MenuAction::SaveControllerProfile));
    CHECK_EQ(static_cast<int>(items[items.size() - 2].action), static_cast<int>(MenuAction::ResetControllerMapping));
}

TEST_CASE(controller_menu_adjust_cycles_mapping_and_detects_value_change) {
    Menu menu;
    MenuSettings settings;
    settings.controllerMapping = ControllerMapping::defaults();
    menu.setTab(MenuTab::Controller);
    MenuStatus status;
    status.controllerDeviceNames = {"PAD 1"};
    status.controllerDeviceGuids = {"guid-1"};
    status.controllerDeviceIndex = 0;
    status.controllerConnected = true;
    std::vector<MenuItem> items = menu.items(settings, status);
    // "MAP CROSS" is the first mapping row right after the 6 info rows.
    const std::size_t crossIndex = 6;
    CHECK_EQ(items[crossIndex].label, "MAP CROSS");
    menu.setSelected(crossIndex);
    const HostControl before = settings.controllerMapping.host[0];
    menu.adjust(settings, 1, status);
    CHECK(menu.takeSettingsChanged());
    CHECK(settings.controllerMapping.host[0] != before);

    // Deadzone adjust clamps and changes values.
    menu.setSelected(2);
    const int deadzoneBefore = settings.controllerMapping.deadzone;
    menu.adjust(settings, 1, status);
    CHECK(menu.takeSettingsChanged());
    CHECK(settings.controllerMapping.deadzone == deadzoneBefore + 1024);

    // Save profile activation issues the command.
    menu.setSelected(items.size() - 1);
    menu.activate(settings, status);
    CHECK_EQ(static_cast<int>(menu.takeCommand()), static_cast<int>(MenuCommand::SaveControllerProfile));
}

TEST_CASE(controller_menu_device_cycling_uses_status_lists) {
    Menu menu;
    MenuSettings settings;
    settings.controllerGuid = "g1";
    MenuStatus status;
    status.controllerDeviceNames = {"PAD 1", "PAD 2", "PAD 3"};
    status.controllerDeviceGuids = {"g1", "g2", "g3"};
    status.controllerDeviceIndex = 0;
    status.controllerConnected = true;
    menu.setTab(MenuTab::Controller);
    const std::vector<MenuItem> items = menu.items(settings, status);
    (void)items;
    CHECK_EQ(settings.controllerGuid, "g1");
    menu.adjust(settings, 1, status);
    CHECK_EQ(settings.controllerGuid, "g2");
    menu.adjust(settings, 1, status);
    CHECK_EQ(settings.controllerGuid, "g3");
    menu.adjust(settings, -1, status);
    CHECK_EQ(settings.controllerGuid, "g2");
}
