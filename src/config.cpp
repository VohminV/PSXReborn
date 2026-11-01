#include "psxreborn/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace psxreborn {

namespace {

bool parseBool(const std::string& value, bool& result) {
    std::string normalized;
    for (char character : value) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        result = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        result = false;
        return true;
    }
    return false;
}

}  // namespace

Config::Config() = default;

std::string Config::defaultPath() {
#if defined(_WIN32)
    const char* appData = std::getenv("APPDATA");
    if (appData != nullptr && *appData != '\0') {
        return (std::filesystem::path(appData) / "PSXReborn" / "psxreborn.conf").string();
    }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return (std::filesystem::path(xdg) / "psxreborn" / "psxreborn.conf").string();
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return (std::filesystem::path(home) / ".config" / "psxreborn" / "psxreborn.conf").string();
    }
#endif
    return "psxreborn.conf";
}

std::string Config::trim(const std::string& value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool Config::validateKey(const std::string& key, std::string& error) const {
    if (key.empty()) {
        error = "configuration key is empty";
        return false;
    }
    for (char character : key) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (code < 0x21 || code > 0x7E || character == '=' || character == '[' ||
            character == ']' || character == '#' || character == ';') {
            error = "configuration key contains invalid characters: " + key;
            return false;
        }
    }
    return true;
}

void Config::setGameId(const std::string& gameId) {
    gameId_ = sanitizeGameId(gameId);
}

std::string Config::sanitizeGameId(const std::string& id) {
    std::string result;
    for (char character : id) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (std::isalnum(code) != 0 || character == '-' || character == '_' || character == '.') {
            result.push_back(static_cast<char>(std::toupper(code)));
        } else if (character == ' ') {
            result.push_back('_');
        }
    }
    return result;
}

const std::map<std::string, std::string>& Config::values(bool gameOverride) const {
    if (gameOverride && !game_.empty()) {
        return game_;
    }
    return global_;
}

bool Config::has(const std::string& key, bool gameOverride) const {
    if (gameOverride && game_.find(key) != game_.end()) {
        return true;
    }
    return global_.find(key) != global_.end();
}

std::string Config::getString(const std::string& key, const std::string& fallback,
                              bool gameOverride) const {
    if (gameOverride) {
        const auto found = game_.find(key);
        if (found != game_.end()) {
            return found->second;
        }
    }
    const auto found = global_.find(key);
    return found == global_.end() ? fallback : found->second;
}

int Config::getInt(const std::string& key, int fallback, bool gameOverride) const {
    const std::string value = getString(key, "", gameOverride);
    if (value.empty()) {
        return fallback;
    }
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

bool Config::getBool(const std::string& key, bool fallback, bool gameOverride) const {
    const std::string value = getString(key, "", gameOverride);
    bool parsed = fallback;
    return parseBool(value, parsed) ? parsed : fallback;
}

double Config::getDouble(const std::string& key, double fallback, bool gameOverride) const {
    const std::string value = getString(key, "", gameOverride);
    if (value.empty()) {
        return fallback;
    }
    try {
        size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

void Config::setString(const std::string& key, const std::string& value, bool gameOverride) {
    std::string error;
    if (!validateKey(key, error) || value.find('\n') != std::string::npos ||
        value.find('\r') != std::string::npos) {
        return;
    }
    if (gameOverride && !gameId_.empty()) {
        game_[key] = value;
    } else {
        global_[key] = value;
    }
}

void Config::setInt(const std::string& key, int value, bool gameOverride) {
    setString(key, std::to_string(value), gameOverride);
}

void Config::setBool(const std::string& key, bool value, bool gameOverride) {
    setString(key, value ? "true" : "false", gameOverride);
}

void Config::setDouble(const std::string& key, double value, bool gameOverride) {
    std::ostringstream stream;
    stream << value;
    setString(key, stream.str(), gameOverride);
}

void Config::erase(const std::string& key, bool gameOverride) {
    if (gameOverride) {
        game_.erase(key);
    } else {
        global_.erase(key);
    }
}

void Config::clear() {
    global_.clear();
    game_.clear();
    gameId_.clear();
    version_ = kCurrentVersion;
}

std::string Config::serialize() const {
    std::ostringstream stream;
    stream << "version=" << version_ << "\n";
    stream << "[global]\n";
    for (const auto& [key, value] : global_) {
        stream << key << "=" << value << "\n";
    }
    if (!gameId_.empty()) {
        stream << "[game." << gameId_ << "]\n";
        for (const auto& [key, value] : game_) {
            stream << key << "=" << value << "\n";
        }
    }
    return stream.str();
}

bool Config::parse(const std::string& text, std::string& error) {
    std::map<std::string, std::string> global;
    std::map<std::string, std::string> game;
    std::string gameId;
    unsigned version = 1;
    bool inGlobal = true;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string normalized = trim(line);
        if (normalized.empty() || normalized[0] == '#' || normalized[0] == ';') {
            continue;
        }
        if (normalized.front() == '[' && normalized.back() == ']') {
            const std::string section = normalized.substr(1, normalized.size() - 2);
            if (section == "global") {
                inGlobal = true;
            } else if (section.rfind("game.", 0) == 0) {
                inGlobal = false;
                gameId = sanitizeGameId(section.substr(5));
            } else {
                inGlobal = true;
            }
            continue;
        }
        const size_t equals = normalized.find('=');
        if (equals == std::string::npos) {
            error = "invalid configuration line " + std::to_string(lineNumber);
            return false;
        }
        const std::string key = trim(normalized.substr(0, equals));
        const std::string value = trim(normalized.substr(equals + 1));
        if (key == "version") {
            try {
                version = static_cast<unsigned>(std::stoul(value));
            } catch (...) {
                version = 1;
            }
            continue;
        }
        std::string keyError;
        if (!validateKey(key, keyError)) {
            error = keyError;
            return false;
        }
        if (inGlobal) {
            global[key] = value;
        } else {
            game[key] = value;
        }
    }
    if (version > kCurrentVersion) {
        error = "configuration version " + std::to_string(version) +
                " is newer than supported version " + std::to_string(kCurrentVersion);
        return false;
    }
    version_ = kCurrentVersion;
    global_ = std::move(global);
    game_ = std::move(game);
    gameId_ = std::move(gameId);
    return true;
}

bool Config::load(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open configuration " + path;
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return parse(contents.str(), error);
}

bool Config::save(const std::string& path, std::string& error) const {
    const std::filesystem::path target(path);
    std::error_code fsError;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), fsError);
        if (fsError) {
            error = "cannot create configuration directory: " + fsError.message();
            return false;
        }
    }
    const std::filesystem::path temporary = target.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "cannot write configuration " + temporary.string();
            return false;
        }
        const std::string text = serialize();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            error = "cannot write configuration " + temporary.string();
            return false;
        }
    }
    std::filesystem::rename(temporary, target, fsError);
    if (fsError) {
        std::filesystem::remove(target, fsError);
        fsError.clear();
        std::filesystem::rename(temporary, target, fsError);
    }
    if (fsError) {
        error = "cannot replace configuration: " + fsError.message();
        return false;
    }
    return true;
}

}  // namespace psxreborn
