#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace psxreborn {

class Config {
public:
    static constexpr unsigned kCurrentVersion = 1;

    Config();

    bool load(const std::string& path, std::string& error);
    bool save(const std::string& path, std::string& error) const;

    void setGameId(const std::string& gameId);
    const std::string& gameId() const { return gameId_; }

    std::string getString(const std::string& key, const std::string& fallback,
                          bool gameOverride = true) const;
    int getInt(const std::string& key, int fallback, bool gameOverride = true) const;
    bool getBool(const std::string& key, bool fallback, bool gameOverride = true) const;
    double getDouble(const std::string& key, double fallback, bool gameOverride = true) const;

    void setString(const std::string& key, const std::string& value, bool gameOverride = false);
    void setInt(const std::string& key, int value, bool gameOverride = false);
    void setBool(const std::string& key, bool value, bool gameOverride = false);
    void setDouble(const std::string& key, double value, bool gameOverride = false);

    bool has(const std::string& key, bool gameOverride = true) const;
    void erase(const std::string& key, bool gameOverride = false);
    void clear();

    unsigned version() const { return version_; }
    void setVersion(unsigned version) { version_ = version; }
    const std::map<std::string, std::string>& values(bool gameOverride = false) const;
    const std::map<std::string, std::string>& gameValues() const { return game_; }

    std::string serialize() const;
    bool parse(const std::string& text, std::string& error);

    static std::string defaultPath();
    static std::string sanitizeGameId(const std::string& id);

private:
    bool validateKey(const std::string& key, std::string& error) const;
    static std::string trim(const std::string& value);

    unsigned version_ = kCurrentVersion;
    std::map<std::string, std::string> global_;
    std::map<std::string, std::string> game_;
    std::string gameId_;
};

}  // namespace psxreborn
