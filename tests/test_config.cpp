#include <filesystem>
#include <string>

#include "psxreborn/config.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

TEST_CASE(config_roundtrip_and_per_game_override) {
    Config config;
    config.setString("video.backend", "vulkan");
    config.setInt("video.width", 1920);
    config.setBool("video.fullscreen", true);
    config.setDouble("audio.volume", 0.75);
    config.setGameId("SLES_040.64");
    config.setString("input.mouse_sensitivity", "1.25", true);
    config.setBool("menu.pause", false, true);

    Config loaded;
    std::string error;
    CHECK(loaded.parse(config.serialize(), error));
    CHECK_EQ(loaded.getString("video.backend", ""), std::string("vulkan"));
    CHECK_EQ(loaded.getInt("video.width", 0), 1920);
    CHECK_EQ(loaded.getBool("video.fullscreen", false), true);
    CHECK(loaded.getDouble("audio.volume", 0.0) > 0.74);
    CHECK_EQ(loaded.gameId(), std::string("SLES_040.64"));
    CHECK_EQ(loaded.getString("input.mouse_sensitivity", "1"), std::string("1.25"));
    CHECK_EQ(loaded.getBool("menu.pause", true), false);
    CHECK_EQ(loaded.getInt("video.width", 0, false), 1920);
    CHECK_EQ(loaded.getInt("missing", 7), 7);
}

TEST_CASE(config_version_and_validation) {
    Config config;
    std::string error;
    CHECK(config.parse("version=1\n[global]\na=1\n[game.SLUS_00001]\na=2\n", error));
    CHECK_EQ(config.getInt("a", 0, false), 1);
    CHECK_EQ(config.getInt("a", 0, true), 2);
    CHECK(!config.parse("version=999\n[global]\na=1\n", error));
    CHECK(!error.empty());
    CHECK(!config.parse("this is not a key value pair\n", error));
    CHECK(!config.parse("version=1\n[global]\nbad key=1\n", error));
}

TEST_CASE(config_save_and_load_file) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "psxreborn-tests" / "config.conf";
    std::error_code errorCode;
    std::filesystem::remove_all(path.parent_path(), errorCode);
    Config config;
    config.setString("video.backend", "sdl");
    std::string error;
    CHECK(config.save(path.string(), error));
    CHECK(std::filesystem::exists(path));
    Config loaded;
    CHECK(loaded.load(path.string(), error));
    CHECK_EQ(loaded.getString("video.backend", ""), std::string("sdl"));
    std::filesystem::remove_all(path.parent_path());
}
