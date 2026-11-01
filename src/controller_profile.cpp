#include "psxreborn/controller_profile.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace psxreborn {

namespace {

constexpr std::array<uint16_t, kPs1ButtonCount> kPs1Bits{{
    kPadCross, kPadCircle, kPadSquare, kPadTriangle,
    kPadUp, kPadDown, kPadLeft, kPadRight,
    kPadStart, kPadSelect, kPadL1, kPadR1,
    kPadL2, kPadR2, kPadL3, kPadR3,
}};

enum class Ps1Name : std::size_t {
    Cross = 0,
    Circle,
    Square,
    Triangle,
    Up,
    Down,
    Left,
    Right,
    Start,
    Select,
    L1,
    R1,
    L2,
    R2,
    L3,
    R3,
};

const std::array<const char*, kPs1ButtonCount> kPs1Names{{
    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE",
    "UP", "DOWN", "LEFT", "RIGHT",
    "START", "SELECT", "L1", "R1",
    "L2", "R2", "L3", "R3",
}};

const std::array<const char*, kPs1ButtonCount> kPs1Tokens{{
    "cross", "circle", "square", "triangle",
    "up", "down", "left", "right",
    "start", "select", "l1", "r1",
    "l2", "r2", "l3", "r3",
}};

// Editor cycle order: face buttons, shoulders then sticks, dpad, triggers,
// system buttons, extras. Values mirror the host button ids above.
const std::vector<HostControl> kHostButtonChoices{{
    kHostBtnA, kHostBtnB, kHostBtnX, kHostBtnY,
    kHostBtnLeftShoulder, kHostBtnRightShoulder,
    kHostBtnLeftStick, kHostBtnRightStick,
    kHostBtnDpadUp, kHostBtnDpadDown, kHostBtnDpadLeft, kHostBtnDpadRight,
    kHostBtnStart, kHostBtnBack,
    kHostBtnMisc1, kHostBtnPaddle1, kHostBtnPaddle2, kHostBtnPaddle3,
    kHostBtnPaddle4, kHostBtnTouchpad,
}};

const std::vector<HostControl> kHostAxes{{
    kHostAxisLeftX, kHostAxisLeftY, kHostAxisRightX, kHostAxisRightY,
    kHostAxisTriggerLeft, kHostAxisTriggerRight,
}};

struct ControlName {
    HostControl control;
    const char* upper;
    const char* lower;
};

const std::array<ControlName, kHostBtnTouchpad + 1 + kHostAxisCount> kControlNames{{
    {kHostBtnA, "A", "a"},
    {kHostBtnB, "B", "b"},
    {kHostBtnX, "X", "x"},
    {kHostBtnY, "Y", "y"},
    {kHostBtnBack, "BACK", "back"},
    {kHostBtnGuide, "GUIDE", "guide"},
    {kHostBtnStart, "START", "start"},
    {kHostBtnLeftStick, "LSTICK", "lstick"},
    {kHostBtnRightStick, "RSTICK", "rstick"},
    {kHostBtnLeftShoulder, "LB", "left_shoulder"},
    {kHostBtnRightShoulder, "RB", "right_shoulder"},
    {kHostBtnDpadUp, "DPAD UP", "dpad_up"},
    {kHostBtnDpadDown, "DPAD DOWN", "dpad_down"},
    {kHostBtnDpadLeft, "DPAD LEFT", "dpad_left"},
    {kHostBtnDpadRight, "DPAD RIGHT", "dpad_right"},
    {kHostBtnMisc1, "MISC1", "misc1"},
    {kHostBtnPaddle1, "PADDLE1", "paddle1"},
    {kHostBtnPaddle2, "PADDLE2", "paddle2"},
    {kHostBtnPaddle3, "PADDLE3", "paddle3"},
    {kHostBtnPaddle4, "PADDLE4", "paddle4"},
    {kHostBtnTouchpad, "TOUCHPAD", "touchpad"},
    {kHostAxisLeftX, "AXIS LEFT X", "axis_left_x"},
    {kHostAxisLeftY, "AXIS LEFT Y", "axis_left_y"},
    {kHostAxisRightX, "AXIS RIGHT X", "axis_right_x"},
    {kHostAxisRightY, "AXIS RIGHT Y", "axis_right_y"},
    {kHostAxisTriggerLeft, "AXIS TRIGGER LEFT", "axis_trigger_left"},
    {kHostAxisTriggerRight, "AXIS TRIGGER RIGHT", "axis_trigger_right"},
}};

// Minimal JSON scanner for the flat profile document: strings with escapes,
// integers, and nested one-level objects. It exists to avoid pulling a JSON
// library into the core for a small fixed-shape file.
class JsonParser {
public:
    JsonParser(const std::string& text, std::string& error)
        : text_(text), error_(error) {}

    void fail(const std::string& message) {
        if (error_.empty()) {
            std::ostringstream stream;
            stream << "line " << line_ << ": " << message;
            error_ = stream.str();
        }
    }

    bool skipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '\n') {
                ++line_;
                ++pos_;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
                continue;
            }
            break;
        }
        return pos_ < text_.size();
    }

    bool peek(char& c) {
        if (!skipSpace()) {
            fail("unexpected end of input");
            return false;
        }
        c = text_[pos_];
        return true;
    }

    bool expect(char c) {
        char found = 0;
        if (!peek(found)) {
            return false;
        }
        if (found != c) {
            std::ostringstream stream;
            stream << "expected '" << c << "'";
            fail(stream.str());
            return false;
        }
        ++pos_;
        return true;
    }

    bool accept(char c) {
        char found = 0;
        if (!peek(found)) {
            return false;
        }
        if (found != c) {
            return false;
        }
        ++pos_;
        return true;
    }

    bool parseString(std::string& result) {
        char c = 0;
        if (!expect('"')) {
            return false;
        }
        result.clear();
        while (pos_ < text_.size()) {
            c = text_[pos_++];
            if (c == '\n') {
                ++line_;
            }
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    fail("truncated string escape");
                    return false;
                }
                const char escaped = text_[pos_++];
                switch (escaped) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'n': result.push_back('\n'); break;
                    case 't': result.push_back('\t'); break;
                    case 'r': result.push_back('\r'); break;
                    default:
                        fail("unsupported string escape");
                        return false;
                }
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("raw control character in string");
                return false;
            }
            result.push_back(c);
        }
        fail("unterminated string");
        return false;
    }

    bool parseInteger(long long& result) {
        if (!skipSpace()) {
            fail("unexpected end of input");
            return false;
        }
        bool negative = false;
        if (pos_ < text_.size() &&
            (text_[pos_] == '-' || text_[pos_] == '+')) {
            negative = text_[pos_] == '-';
            ++pos_;
        }
        size_t digits = 0;
        result = 0;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                break;
            }
            result = result * 10 + (c - '0');
            ++pos_;
            ++digits;
        }
        if (digits == 0) {
            fail("expected a number");
            return false;
        }
        if (negative) {
            result = -result;
        }
        return true;
    }

private:
    const std::string& text_;
    std::string& error_;
    size_t pos_ = 0;
    int line_ = 1;
};

HostControl controlFromString(const std::string& raw, bool& ok) {
    // Names are case-insensitive treated; spaces and '_' stand in for each
    // other so the serialized form survives UI rounding such as the space in
    // "DPAD UP".
    std::string name;
    for (char c : raw) {
        const char lower = static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
        name.push_back(lower == ' ' ? '_' : lower);
    }
    if (name == "none") {
        ok = true;
        return kHostNone;
    }
    for (const ControlName& entry : kControlNames) {
        std::string candidate;
        for (const char* source : {entry.lower, entry.upper}) {
            candidate.clear();
            for (const char* p = source; *p != '\0'; ++p) {
                const char lower = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(*p)));
                candidate.push_back(lower == ' ' ? '_' : lower);
            }
            if (candidate == name) {
                ok = true;
                return entry.control;
            }
        }
    }
    ok = false;
    return kHostNone;
}

}  // namespace

uint16_t ps1ButtonBit(std::size_t index) {
    return index < kPs1ButtonCount ? kPs1Bits[index] : 0;
}

const char* ps1ButtonName(std::size_t index) {
    return index < kPs1ButtonCount ? kPs1Names[index] : "";
}

const char* ps1ButtonToken(std::size_t index) {
    return index < kPs1ButtonCount ? kPs1Tokens[index] : "";
}

const char* hostControlName(HostControl control) {
    for (const ControlName& entry : kControlNames) {
        if (entry.control == control) {
            return entry.upper;
        }
    }
    return control == kHostNone ? "NONE" : "?";
}

bool hostControlFromName(const std::string& name, HostControl& result) {
    bool ok = false;
    result = controlFromString(name, ok);
    return ok;
}

const std::vector<HostControl>& hostControlChoices() {
    static const std::vector<HostControl> choices = [] {
        std::vector<HostControl> all = kHostButtonChoices;
        all.insert(all.end(), kHostAxes.begin(), kHostAxes.end());
        return all;
    }();
    return choices;
}

bool hostControlIsAxis(HostControl control) {
    return (control & kHostAxisBase) != 0;
}

HostControl hostControlAxisIndex(HostControl control) {
    return hostControlIsAxis(control) ? control - kHostAxisBase : 0;
}

ControllerMapping ControllerMapping::defaults() {
    ControllerMapping mapping;
    mapping.host[static_cast<std::size_t>(Ps1Name::Cross)] = kHostBtnA;
    mapping.host[static_cast<std::size_t>(Ps1Name::Circle)] = kHostBtnB;
    mapping.host[static_cast<std::size_t>(Ps1Name::Square)] = kHostBtnX;
    mapping.host[static_cast<std::size_t>(Ps1Name::Triangle)] = kHostBtnY;
    mapping.host[static_cast<std::size_t>(Ps1Name::Up)] = kHostBtnDpadUp;
    mapping.host[static_cast<std::size_t>(Ps1Name::Down)] = kHostBtnDpadDown;
    mapping.host[static_cast<std::size_t>(Ps1Name::Left)] = kHostBtnDpadLeft;
    mapping.host[static_cast<std::size_t>(Ps1Name::Right)] = kHostBtnDpadRight;
    mapping.host[static_cast<std::size_t>(Ps1Name::Start)] = kHostBtnStart;
    mapping.host[static_cast<std::size_t>(Ps1Name::Select)] = kHostBtnBack;
    mapping.host[static_cast<std::size_t>(Ps1Name::L1)] = kHostBtnLeftShoulder;
    mapping.host[static_cast<std::size_t>(Ps1Name::R1)] = kHostBtnRightShoulder;
    mapping.host[static_cast<std::size_t>(Ps1Name::L2)] = kHostAxisTriggerLeft;
    mapping.host[static_cast<std::size_t>(Ps1Name::R2)] = kHostAxisTriggerRight;
    mapping.host[static_cast<std::size_t>(Ps1Name::L3)] = kHostBtnLeftStick;
    mapping.host[static_cast<std::size_t>(Ps1Name::R3)] = kHostBtnRightStick;
    mapping.deadzone = kDefaultDeadzone;
    mapping.triggerThreshold = kDefaultTriggerThreshold;
    return mapping;
}

std::vector<MappingConflict> mappingConflicts(const ControllerMapping& mapping) {
    std::vector<MappingConflict> conflicts;
    if (mapping.host.size() != kPs1ButtonCount) {
        return conflicts;
    }
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        const HostControl control = mapping.host[slot];
        if (control == kHostNone) {
            continue;
        }
        bool found = false;
        for (MappingConflict& conflict : conflicts) {
            if (conflict.control == control) {
                conflict.slots.push_back(slot);
                found = true;
                break;
            }
        }
        if (!found) {
            for (std::size_t other = 0; other < slot; ++other) {
                if (mapping.host[other] == control) {
                    MappingConflict conflict;
                    conflict.control = control;
                    conflict.slots.push_back(other);
                    conflict.slots.push_back(slot);
                    conflicts.push_back(std::move(conflict));
                    break;
                }
            }
        }
    }
    return conflicts;
}

namespace {

bool hostControlPressed(const ControllerSnapshot& snapshot,
                        const ControllerMapping& mapping, HostControl control) {
    if (control == kHostNone) {
        return false;
    }
    if (control < kHostAxisBase) {
        return snapshot.buttonPressed(control);
    }
    const int32_t axis = snapshot.axes[static_cast<std::size_t>(
        hostControlAxisIndex(control))];
    if (control == kHostAxisTriggerLeft || control == kHostAxisTriggerRight) {
        return axis >= mapping.triggerThreshold;
    }
    return axis >= 8192;
}

}  // namespace

void applyControllerSnapshot(PadState& pad, const ControllerSnapshot& snapshot,
                             const ControllerMapping& mapping) {
    pad.clearController();
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        if (hostControlPressed(snapshot, mapping, mapping.host[slot])) {
            pad.setControllerButton(ps1ButtonBit(slot), true);
        }
    }
    pad.setLeftStick(snapshot.axes[0], snapshot.axes[1], mapping.deadzone);
    pad.setRightStick(snapshot.axes[2], snapshot.axes[3]);
}

void ControllerProfile::applyMissingFields() {
    if (mapping.deadzone < 0) {
        mapping.deadzone = ControllerMapping::kDefaultDeadzone;
    }
    if (mapping.deadzone > 32767) {
        mapping.deadzone = 32767;
    }
    if (mapping.triggerThreshold < 1) {
        mapping.triggerThreshold = ControllerMapping::kDefaultTriggerThreshold;
    }
    if (mapping.triggerThreshold > 32000) {
        mapping.triggerThreshold = 32000;
    }
    for (HostControl& control : mapping.host) {
        if (!hostControlIsAxis(control) && control >= 32 && control != kHostNone) {
            control = kHostNone;
        }
    }
}

std::string ControllerProfile::serialize() const {
    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"format_version\": " << formatVersion << ",\n";
    stream << "  \"guid\": \"" << guid << "\",\n";
    stream << "  \"device_name\": \"" << deviceName << "\",\n";
    stream << "  \"mapping\": {\n";
    for (std::size_t slot = 0; slot < kPs1ButtonCount; ++slot) {
        stream << "    \"" << ps1ButtonToken(slot) << "\": \"";
        if (mapping.host[slot] == kHostNone) {
            stream << "none";
        } else {
            stream << hostControlName(mapping.host[slot]);
        }
        stream << "\"";
        stream << (slot + 1 < kPs1ButtonCount ? "," : "");
        stream << "\n";
    }
    stream << "  },\n";
    stream << "  \"deadzone\": " << mapping.deadzone << ",\n";
    stream << "  \"trigger_threshold\": " << mapping.triggerThreshold << "\n";
    stream << "}\n";
    return stream.str();
}

bool ControllerProfile::parse(const std::string& json, std::string& error) {
    JsonParser parser(json, error);
    if (!parser.expect('{')) {
        return false;
    }
    ControllerMapping parsedMapping = ControllerMapping::defaults();
    bool haveClose = false;
    unsigned parsedVersion = kFormatVersion;
    std::string parsedGuid;
    std::string parsedName;
    int parsedDeadzone = ControllerMapping::kDefaultDeadzone;
    int parsedThreshold = ControllerMapping::kDefaultTriggerThreshold;
    bool haveDeadzone = false;
    bool haveThreshold = false;

    if (!parser.skipSpace()) {
        parser.fail("empty profile document");
        return false;
    }
    if (parser.accept('}')) {
        haveClose = true;
    }
    while (!haveClose) {
        std::string key;
        if (!parser.parseString(key)) {
            return false;
        }
        if (!parser.expect(':')) {
            return false;
        }
        if (key == "format_version") {
            long long value = 0;
            if (!parser.parseInteger(value)) {
                return false;
            }
            if (value < 0 || value > 100) {
                parser.fail("unsupported format version");
                return false;
            }
            parsedVersion = static_cast<unsigned>(value);
        } else if (key == "guid") {
            if (!parser.parseString(parsedGuid)) {
                return false;
            }
        } else if (key == "device_name") {
            if (!parser.parseString(parsedName)) {
                return false;
            }
        } else if (key == "deadzone") {
            long long value = 0;
            if (!parser.parseInteger(value)) {
                return false;
            }
            if (value < 0 || value > 32767) {
                parser.fail("deadzone out of range");
                return false;
            }
            parsedDeadzone = static_cast<int>(value);
            haveDeadzone = true;
        } else if (key == "trigger_threshold") {
            long long value = 0;
            if (!parser.parseInteger(value)) {
                return false;
            }
            if (value < 1 || value > 32000) {
                parser.fail("trigger threshold out of range");
                return false;
            }
            parsedThreshold = static_cast<int>(value);
            haveThreshold = true;
        } else if (key == "mapping") {
            if (!parser.expect('{')) {
                return false;
            }
            if (parser.accept('}')) {
                // Empty mapping object keeps hosts unassigned.
                continue;
            }
            while (true) {
                std::string buttonToken;
                if (!parser.parseString(buttonToken)) {
                    return false;
                }
                if (!parser.expect(':')) {
                    return false;
                }
                std::string controlToken;
                if (!parser.parseString(controlToken)) {
                    return false;
                }
                std::size_t slot = kPs1ButtonCount;
                for (std::size_t candidate = 0; candidate < kPs1ButtonCount; ++candidate) {
                    if (buttonToken == ps1ButtonToken(candidate)) {
                        slot = candidate;
                        break;
                    }
                }
                if (slot == kPs1ButtonCount) {
                    parser.fail("unknown PS1 button in mapping: " + buttonToken);
                    return false;
                }
                bool controlOk = false;
                const HostControl control = controlFromString(controlToken, controlOk);
                if (!controlOk) {
                    parser.fail("unknown host control: " + controlToken);
                    return false;
                }
                if (control != kHostNone && control >= 32 && !hostControlIsAxis(control)) {
                    parser.fail("invalid host control: " + controlToken);
                    return false;
                }
                parsedMapping.host[slot] = control;
                if (parser.accept(',')) {
                    continue;
                }
                if (parser.accept('}')) {
                    break;
                }
                parser.fail("expected ',' or '}' in mapping object");
                return false;
            }
        } else {
            parser.fail("unknown profile key: " + key);
            return false;
        }
        if (parser.accept(',')) {
            continue;
        }
        if (parser.accept('}')) {
            haveClose = true;
            break;
        }
        parser.fail("expected ',' or '}' in profile document");
        return false;
    }
    if (parsedVersion > kFormatVersion) {
        error = "profile format version " + std::to_string(parsedVersion) +
                " is newer than supported version " + std::to_string(kFormatVersion);
        return false;
    }

    formatVersion = parsedVersion;
    guid = parsedGuid;
    deviceName = parsedName;
    mapping = parsedMapping;
    if (haveDeadzone) {
        mapping.deadzone = parsedDeadzone;
    }
    if (haveThreshold) {
        mapping.triggerThreshold = parsedThreshold;
    }
    applyMissingFields();
    return true;
}

std::string ControllerProfile::profilesDirectory() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base;
    if (xdg != nullptr && *xdg != '\0') {
        base = std::filesystem::path(xdg);
    } else if (const char* home = std::getenv("HOME");
               home != nullptr && *home != '\0') {
        base = std::filesystem::path(home) / ".config";
    } else {
        base = std::filesystem::current_path();
    }
    return (base / "psxreborn" / "profiles").string();
}

std::string ControllerProfile::sanitizeGuid(const std::string& guid) {
    std::string result;
    for (char character : guid) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (std::isalnum(code) != 0 || character == '-' || character == '_') {
            result.push_back(static_cast<char>(code));
        } else {
            result.push_back('_');
        }
    }
    return result.empty() ? "default" : result;
}

std::string ControllerProfile::pathForGuid(const std::string& guid) {
    const std::filesystem::path directory(profilesDirectory());
    return (directory / (sanitizeGuid(guid) + ".json")).string();
}

bool ControllerProfile::save(const std::string& path, std::string& error) const {
    namespace fs = std::filesystem;
    const fs::path target(path);
    std::error_code fsError;
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), fsError);
        if (fsError) {
            error = "cannot create profile directory: " + fsError.message();
            return false;
        }
    }
    const fs::path temporary = target.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "cannot write profile " + temporary.string();
            return false;
        }
        const std::string text = serialize();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file) {
            error = "cannot write profile " + temporary.string();
            return false;
        }
    }
    fs::rename(temporary, target, fsError);
    if (fsError) {
        fs::remove(target, fsError);
        fsError.clear();
        fs::rename(temporary, target, fsError);
    }
    if (fsError) {
        error = "cannot replace profile: " + fsError.message();
        return false;
    }
    return true;
}

bool ControllerProfile::load(const std::string& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open profile " + path;
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return parse(contents.str(), error);
}

}  // namespace psxreborn
