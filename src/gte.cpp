#include "psxreborn/gte.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace psxreborn {

namespace {

constexpr uint32_t kMac1Positive = 1u << 30;
constexpr uint32_t kMac2Positive = 1u << 29;
constexpr uint32_t kMac3Positive = 1u << 28;
constexpr uint32_t kMac1Negative = 1u << 27;
constexpr uint32_t kMac2Negative = 1u << 26;
constexpr uint32_t kMac3Negative = 1u << 25;
constexpr uint32_t kIr1Saturated = 1u << 24;
constexpr uint32_t kIr2Saturated = 1u << 23;
constexpr uint32_t kIr3Saturated = 1u << 22;
constexpr uint32_t kColorRSaturated = 1u << 21;
constexpr uint32_t kColorGSaturated = 1u << 20;
constexpr uint32_t kColorBSaturated = 1u << 19;
constexpr uint32_t kSizeSaturated = 1u << 18;
constexpr uint32_t kDivideOverflow = 1u << 17;
constexpr uint32_t kMac0Positive = 1u << 16;
constexpr uint32_t kMac0Negative = 1u << 15;
constexpr uint32_t kSxSaturated = 1u << 14;
constexpr uint32_t kSySaturated = 1u << 13;
constexpr uint32_t kIr0Saturated = 1u << 12;

constexpr int64_t kMac44Maximum = 0x7FFFFFFFFFFll;
constexpr int64_t kMac44Minimum = -0x80000000000ll;

int16_t lowSigned16(uint32_t value) {
    return static_cast<int16_t>(value & 0xFFFFu);
}

int16_t highSigned16(uint32_t value) {
    return static_cast<int16_t>((value >> 16) & 0xFFFFu);
}

uint32_t signExtend16(uint32_t value) {
    return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value & 0xFFFFu)));
}

int32_t clampColor(int64_t value, uint32_t flag, uint32_t& flags) {
    if (value > 0xFF) {
        flags |= flag;
        return 0xFF;
    }
    if (value < 0) {
        flags |= flag;
        return 0;
    }
    return static_cast<int32_t>(value);
}

}  // namespace

void Gte::reset() {
    data_.fill(0);
    control_.fill(0);
    mac_.fill(0);
}

int16_t Gte::matrixElement(int matrix, int row, int column) const {
    const int index = row * 3 + column;
    if (matrix == 3) {
        switch (index) {
            case 0:
                return static_cast<int16_t>(-static_cast<int32_t>(data_[6] & 0xFFu) << 4);
            case 1:
                return static_cast<int16_t>(static_cast<int32_t>(data_[6] & 0xFFu) << 4);
            case 2:
                return static_cast<int16_t>(data_[8] & 0xFFFFu);
            case 3:
            case 4:
            case 5:
                return lowSigned16(control_[1]);
            default:
                return lowSigned16(control_[2]);
        }
    }
    const int base = matrix == 0 ? 0 : matrix == 1 ? 8 : 16;
    const uint32_t packed = control_[static_cast<size_t>(base + index / 2)];
    return (index & 1) != 0 ? highSigned16(packed) : lowSigned16(packed);
}

int16_t Gte::vertexComponent(int vector, int component) const {
    if (vector == 3) {
        return static_cast<int16_t>(data_[static_cast<size_t>(9 + component)] & 0xFFFFu);
    }
    if (component == 2) {
        return static_cast<int16_t>(data_[static_cast<size_t>(vector * 2 + 1)] & 0xFFFFu);
    }
    const uint32_t packed = data_[static_cast<size_t>(vector * 2)];
    return component == 0 ? lowSigned16(packed) : highSigned16(packed);
}

int32_t Gte::controlVector(int vector, int component) const {
    if (vector == 3) {
        return 0;
    }
    const size_t base = vector == 0 ? 5u : vector == 1 ? 13u : 21u;
    return static_cast<int32_t>(control_[base + static_cast<size_t>(component)]);
}

int16_t Gte::ir(int component) const {
    return static_cast<int16_t>(data_[static_cast<size_t>(9 + component)] & 0xFFFFu);
}

int16_t Gte::ir0() const {
    return static_cast<int16_t>(data_[8] & 0xFFFFu);
}

void Gte::setIr(int component, int64_t value, bool lm) {
    const int64_t minimum = lm ? 0 : -0x8000;
    const uint32_t flag = component == 0 ? kIr1Saturated : component == 1 ? kIr2Saturated : kIr3Saturated;
    if (value > 0x7FFF) {
        setFlag(flag);
        value = 0x7FFF;
    } else if (value < minimum) {
        setFlag(flag);
        value = minimum;
    }
    data_[static_cast<size_t>(9 + component)] =
        static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
}

void Gte::setMac(int component, int64_t rawValue, bool sf) {
    if (rawValue > kMac44Maximum) {
        setFlag(component == 1 ? kMac1Positive : component == 2 ? kMac2Positive : kMac3Positive);
    } else if (rawValue < kMac44Minimum) {
        setFlag(component == 1 ? kMac1Negative : component == 2 ? kMac2Negative : kMac3Negative);
    }
    const int64_t value = sf ? (rawValue >> 12) : rawValue;
    mac_[static_cast<size_t>(component)] = value;
    data_[static_cast<size_t>(24 + component)] = static_cast<uint32_t>(value);
}

void Gte::setMac0(int64_t rawValue) {
    if (rawValue > 0x7FFFFFFF) {
        setFlag(kMac0Positive);
    } else if (rawValue < -0x80000000ll) {
        setFlag(kMac0Negative);
    }
    mac_[0] = rawValue;
    data_[24] = static_cast<uint32_t>(rawValue);
}

void Gte::finishFlags() {
    if ((control_[31] & 0x7F87E000u) != 0) {
        control_[31] |= 0x80000000u;
    }
}

void Gte::pushZ(uint16_t value) {
    data_[16] = data_[17];
    data_[17] = data_[18];
    data_[18] = data_[19];
    data_[19] = value;
}

void Gte::pushSxy(int32_t x, int32_t y) {
    data_[12] = data_[13];
    data_[13] = data_[14];
    data_[14] = (static_cast<uint32_t>(y) & 0xFFFFu) << 16 | (static_cast<uint32_t>(x) & 0xFFFFu);
    data_[15] = data_[14];
}

void Gte::pushColor() {
    data_[20] = data_[21];
    data_[21] = data_[22];
    const int32_t r = clampColor(mac_[1] >> 4, kColorRSaturated, control_[31]);
    const int32_t g = clampColor(mac_[2] >> 4, kColorGSaturated, control_[31]);
    const int32_t b = clampColor(mac_[3] >> 4, kColorBSaturated, control_[31]);
    const uint32_t code = (data_[6] >> 24) & 0xFFu;
    data_[22] = (code << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) |
                static_cast<uint32_t>(r);
}

uint32_t Gte::readData(uint8_t index) const {
    const uint8_t reg = index & 0x1Fu;
    switch (reg) {
        case 1:
        case 3:
        case 5:
        case 9:
        case 10:
        case 11:
            return signExtend16(data_[reg]);
        case 24:
        case 25:
        case 26:
        case 27:
            return static_cast<uint32_t>(mac_[reg - 24]);
        case 28:
        case 29: {
            const auto channel = [](int16_t value) -> uint32_t {
                const int32_t clamped = std::clamp<int32_t>(value, 0, 0x0F80);
                return static_cast<uint32_t>(clamped / 0x80);
            };
            return channel(ir(0)) | (channel(ir(1)) << 5) | (channel(ir(2)) << 10);
        }
        case 31: {
            const uint32_t value = data_[30];
            if (value == 0) {
                return 32;
            }
            if ((value & 0x80000000u) != 0) {
                return static_cast<uint32_t>(std::countl_one(value));
            }
            return static_cast<uint32_t>(std::countl_zero(value));
        }
        default:
            return data_[reg];
    }
}

uint32_t Gte::readControl(uint8_t index) const {
    const uint8_t reg = index & 0x1Fu;
    switch (reg) {
        case 26:
            return signExtend16(control_[26]);
        case 27:
            return signExtend16(control_[27]);
        case 29:
        case 30:
            return signExtend16(control_[reg]);
        default:
            return control_[reg];
    }
}

void Gte::writeData(uint8_t index, uint32_t value) {
    const uint8_t reg = index & 0x1Fu;
    switch (reg) {
        case 15:
            pushSxy(lowSigned16(value), highSigned16(value));
            return;
        case 24:
        case 25:
        case 26:
        case 27:
            mac_[reg - 24] = static_cast<int64_t>(static_cast<int32_t>(value));
            data_[reg] = value;
            return;
        case 28: {
            data_[28] = value & 0x7FFFu;
            const int16_t red = static_cast<int16_t>(static_cast<int16_t>((value & 0x1Fu) << 7));
            const int16_t green = static_cast<int16_t>(static_cast<int16_t>(((value >> 5) & 0x1Fu) << 7));
            const int16_t blue = static_cast<int16_t>(static_cast<int16_t>(((value >> 10) & 0x1Fu) << 7));
            data_[9] = static_cast<uint32_t>(static_cast<int32_t>(red));
            data_[10] = static_cast<uint32_t>(static_cast<int32_t>(green));
            data_[11] = static_cast<uint32_t>(static_cast<int32_t>(blue));
            return;
        }
        case 30:
            data_[30] = value;
            return;
        case 31:
            return;
        default:
            data_[reg] = value;
            return;
    }
}

void Gte::writeControl(uint8_t index, uint32_t value) {
    const uint8_t reg = index & 0x1Fu;
    if (reg == 31) {
        control_[31] = value & 0x7FFFF000u;
        return;
    }
    control_[reg] = value;
}

void Gte::transform(int matrix, int vector, int translation, bool sf, bool lm, int64_t& rawMac3) {
    int64_t raw[3] = {};
    if (translation == 2) {
        for (int row = 0; row < 3; ++row) {
            raw[row] = static_cast<int64_t>(matrixElement(matrix, row, 1)) * vertexComponent(vector, 1) +
                       static_cast<int64_t>(matrixElement(matrix, row, 2)) * vertexComponent(vector, 2);
        }
        int64_t ignored[3] = {};
        for (int row = 0; row < 3; ++row) {
            ignored[row] = (static_cast<int64_t>(controlVector(translation, row)) << 12) +
                           static_cast<int64_t>(matrixElement(matrix, row, 0)) * vertexComponent(vector, 0);
        }
        setMac(1, ignored[0], sf);
        setMac(2, ignored[1], sf);
        setMac(3, ignored[2], sf);
        (void)ir(0);
    } else {
        for (int row = 0; row < 3; ++row) {
            raw[row] = (static_cast<int64_t>(controlVector(translation, row)) << 12) +
                       static_cast<int64_t>(matrixElement(matrix, row, 0)) * vertexComponent(vector, 0) +
                       static_cast<int64_t>(matrixElement(matrix, row, 1)) * vertexComponent(vector, 1) +
                       static_cast<int64_t>(matrixElement(matrix, row, 2)) * vertexComponent(vector, 2);
        }
    }
    if (translation == 2) {
        for (int row = 0; row < 3; ++row) {
            setMac(row + 1, raw[row], sf);
        }
    } else {
        setMac(1, raw[0], sf);
        setMac(2, raw[1], sf);
        setMac(3, raw[2], sf);
    }
    rawMac3 = raw[2];
    setIr(0, mac_[1], lm);
    setIr(1, mac_[2], lm);
    setIr(2, mac_[3], lm);
}

void Gte::colorMatrix(bool sf, bool lm) {
    int64_t ignored = 0;
    transform(2, 3, 1, sf, lm, ignored);
}

void Gte::lightTransform(int vector, bool sf, bool lm) {
    int64_t ignored = 0;
    transform(1, vector, 3, sf, lm, ignored);
}

void Gte::colorApply(bool sf, bool lm) {
    const int64_t red = static_cast<int64_t>(data_[6] & 0xFFu) << 4;
    const int64_t green = static_cast<int64_t>((data_[6] >> 8) & 0xFFu) << 4;
    const int64_t blue = static_cast<int64_t>((data_[6] >> 16) & 0xFFu) << 4;
    setMac(1, red * ir(0), sf);
    setMac(2, green * ir(1), sf);
    setMac(3, blue * ir(2), sf);
    setIr(0, mac_[1], lm);
    setIr(1, mac_[2], lm);
    setIr(2, mac_[3], lm);
}

void Gte::depthCue(int64_t inputR, int64_t inputG, int64_t inputB, bool sf, bool lm) {
    const auto channel = [this, sf, lm](int64_t input, int component, int64_t farColor) {
        int64_t difference = (farColor << 12) - input;
        if (sf) {
            difference >>= 12;
        }
        const uint32_t flag = component == 0 ? kIr1Saturated : component == 1 ? kIr2Saturated : kIr3Saturated;
        if (difference > 0x7FFF) {
            setFlag(flag);
            difference = 0x7FFF;
        } else if (difference < -0x8000) {
            setFlag(flag);
            difference = -0x8000;
        }
        setMac(component + 1, input + static_cast<int64_t>(ir0()) * difference, sf);
        setIr(component, mac_[static_cast<size_t>(component + 1)], lm);
    };
    channel(inputR, 0, static_cast<int32_t>(control_[21]));
    channel(inputG, 1, static_cast<int32_t>(control_[22]));
    channel(inputB, 2, static_cast<int32_t>(control_[23]));
}

void Gte::depthCueColor(bool sf, bool lm) {
    const int64_t red = static_cast<int64_t>(data_[6] & 0xFFu) << 4;
    const int64_t green = static_cast<int64_t>((data_[6] >> 8) & 0xFFu) << 4;
    const int64_t blue = static_cast<int64_t>((data_[6] >> 16) & 0xFFu) << 4;
    depthCue(red * ir(0), green * ir(1), blue * ir(2), sf, lm);
}

uint32_t Gte::divide(uint16_t numerator, uint16_t denominator) {
    if (static_cast<uint32_t>(numerator) < static_cast<uint32_t>(denominator) * 2u) {
        static const std::array<uint8_t, 257> kTable = [] {
            std::array<uint8_t, 257> table{};
            for (size_t index = 0; index < table.size(); ++index) {
                const uint32_t value = 0x40000u / (static_cast<uint32_t>(index) + 0x100u);
                const int32_t result = static_cast<int32_t>((value + 1u) / 2u) - 0x101;
                table[index] = static_cast<uint8_t>(std::max(0, result));
            }
            return table;
        }();

        const uint32_t shift = static_cast<uint32_t>(std::countl_zero(static_cast<uint32_t>(denominator))) - 16u;
        const uint32_t n = static_cast<uint32_t>(numerator) << shift;
        uint32_t d = static_cast<uint32_t>(denominator) << shift;
        const uint32_t u = static_cast<uint32_t>(kTable[(d - 0x7FC0u) >> 7]) + 0x101u;
        d = (0x2000080u - d * u) >> 8;
        d = (0x0000080u + d * u) >> 8;
        const uint64_t result = (static_cast<uint64_t>(n) * d + 0x8000u) >> 16;
        return static_cast<uint32_t>(std::min<uint64_t>(0x1FFFFu, result));
    }
    setFlag(kDivideOverflow);
    return 0x1FFFFu;
}

void Gte::rtps(int vector, bool last, bool sf, bool lm) {
    int64_t rawX = (static_cast<int64_t>(control_[5]) << 12) +
                   static_cast<int64_t>(matrixElement(0, 0, 0)) * vertexComponent(vector, 0) +
                   static_cast<int64_t>(matrixElement(0, 0, 1)) * vertexComponent(vector, 1) +
                   static_cast<int64_t>(matrixElement(0, 0, 2)) * vertexComponent(vector, 2);
    int64_t rawY = (static_cast<int64_t>(control_[6]) << 12) +
                   static_cast<int64_t>(matrixElement(0, 1, 0)) * vertexComponent(vector, 0) +
                   static_cast<int64_t>(matrixElement(0, 1, 1)) * vertexComponent(vector, 1) +
                   static_cast<int64_t>(matrixElement(0, 1, 2)) * vertexComponent(vector, 2);
    int64_t rawZ = (static_cast<int64_t>(control_[7]) << 12) +
                   static_cast<int64_t>(matrixElement(0, 2, 0)) * vertexComponent(vector, 0) +
                   static_cast<int64_t>(matrixElement(0, 2, 1)) * vertexComponent(vector, 1) +
                   static_cast<int64_t>(matrixElement(0, 2, 2)) * vertexComponent(vector, 2);

    setMac(1, rawX, sf);
    setMac(2, rawY, sf);
    setMac(3, rawZ, sf);
    setIr(0, mac_[1], lm);
    setIr(1, mac_[2], lm);
    setIr(2, mac_[3], lm);

    const int64_t value12 = rawZ >> 12;
    if (value12 < -0x8000 || value12 > 0x7FFF) {
        setFlag(kIr3Saturated);
    }
    const int64_t storedIr3 = sf ? (rawZ >> 12) : rawZ;
    const int64_t minimumIr3 = lm ? 0 : -0x8000;
    data_[11] = static_cast<uint32_t>(
        static_cast<int32_t>(static_cast<int16_t>(std::clamp<int64_t>(storedIr3, minimumIr3, 0x7FFF))));

    const int64_t sizeRaw = sf ? (rawZ >> 12) : (rawZ >> 12);
    int64_t size = sizeRaw;
    if (size < 0) {
        setFlag(kSizeSaturated);
        size = 0;
    } else if (size > 0xFFFF) {
        setFlag(kSizeSaturated);
        size = 0xFFFF;
    }
    pushZ(static_cast<uint16_t>(size));

    const uint16_t h = static_cast<uint16_t>(control_[26] & 0xFFFFu);
    const uint32_t quotient = divide(h, static_cast<uint16_t>(data_[19] & 0xFFFFu));

    const int64_t xRaw = static_cast<int64_t>(static_cast<int32_t>(control_[24])) +
                         static_cast<int64_t>(ir(0)) * quotient;
    const int64_t yRaw = static_cast<int64_t>(static_cast<int32_t>(control_[25])) +
                         static_cast<int64_t>(ir(1)) * quotient;
    setMac0(xRaw);
    int64_t x = xRaw >> 16;
    int64_t y = yRaw >> 16;
    if (x < -0x400) {
        setFlag(kSxSaturated);
        x = -0x400;
    } else if (x > 0x3FF) {
        setFlag(kSxSaturated);
        x = 0x3FF;
    }
    if (y < -0x400) {
        setFlag(kSySaturated);
        y = -0x400;
    } else if (y > 0x3FF) {
        setFlag(kSySaturated);
        y = 0x3FF;
    }
    pushSxy(static_cast<int32_t>(x), static_cast<int32_t>(y));

    if (last) {
        const int64_t depthRaw = static_cast<int64_t>(static_cast<int32_t>(control_[28])) +
                                 static_cast<int64_t>(static_cast<int32_t>(control_[27])) * quotient;
        setMac0(depthRaw);
        const int64_t depth = depthRaw >> 12;
        if (depth < 0 || depth > 0x1000) {
            setFlag(kIr0Saturated);
        }
        data_[8] = static_cast<uint32_t>(static_cast<int32_t>(std::clamp<int64_t>(depth, 0, 0x1000)));
    }
}

void Gte::execute(uint32_t command) {
    control_[31] = 0;
    const uint8_t opcode = static_cast<uint8_t>(command & 0x3Fu);
    const bool sf = ((command >> 19) & 1u) != 0;
    const bool lm = ((command >> 10) & 1u) != 0;
    const int matrix = static_cast<int>((command >> 17) & 3u);
    const int vector = static_cast<int>((command >> 15) & 3u);
    const int translation = static_cast<int>((command >> 13) & 3u);
    int64_t rawMac3 = 0;

    switch (opcode) {
        case 0x01:
            rtps(0, true, sf, lm);
            break;
        case 0x06: {
            const int64_t sx0 = static_cast<int16_t>(data_[12] & 0xFFFFu);
            const int64_t sy0 = static_cast<int16_t>((data_[12] >> 16) & 0xFFFFu);
            const int64_t sx1 = static_cast<int16_t>(data_[13] & 0xFFFFu);
            const int64_t sy1 = static_cast<int16_t>((data_[13] >> 16) & 0xFFFFu);
            const int64_t sx2 = static_cast<int16_t>(data_[14] & 0xFFFFu);
            const int64_t sy2 = static_cast<int16_t>((data_[14] >> 16) & 0xFFFFu);
            setMac0(sx0 * sy1 + sx1 * sy2 + sx2 * sy0 - sx0 * sy2 - sx1 * sy0 - sx2 * sy1);
            break;
        }
        case 0x0C: {
            setMac(1, static_cast<int64_t>(matrixElement(0, 2, 2)) * ir(2) -
                           static_cast<int64_t>(matrixElement(0, 1, 1)) * ir(1), sf);
            setMac(2, static_cast<int64_t>(matrixElement(0, 0, 0)) * ir(0) -
                           static_cast<int64_t>(matrixElement(0, 2, 2)) * ir(2), sf);
            setMac(3, static_cast<int64_t>(matrixElement(0, 1, 1)) * ir(1) -
                           static_cast<int64_t>(matrixElement(0, 0, 0)) * ir(0), sf);
            setIr(0, mac_[1], lm);
            setIr(1, mac_[2], lm);
            setIr(2, mac_[3], lm);
            break;
        }
        case 0x10:
            depthCue(static_cast<int64_t>(data_[6] & 0xFFu) << 16,
                     static_cast<int64_t>((data_[6] >> 8) & 0xFFu) << 16,
                     static_cast<int64_t>((data_[6] >> 16) & 0xFFu) << 16, sf, lm);
            pushColor();
            break;
        case 0x11:
            depthCue(static_cast<int64_t>(ir(0)) << 12, static_cast<int64_t>(ir(1)) << 12,
                     static_cast<int64_t>(ir(2)) << 12, sf, lm);
            pushColor();
            break;
        case 0x12:
            transform(matrix, vector, translation, sf, lm, rawMac3);
            break;
        case 0x13:
            lightTransform(0, sf, lm);
            colorMatrix(sf, lm);
            depthCueColor(sf, lm);
            pushColor();
            break;
        case 0x14:
            colorMatrix(sf, lm);
            depthCueColor(sf, lm);
            pushColor();
            break;
        case 0x16:
            for (int index = 0; index < 3; ++index) {
                lightTransform(index, sf, lm);
                colorMatrix(sf, lm);
                depthCueColor(sf, lm);
                pushColor();
            }
            break;
        case 0x1B:
            lightTransform(0, sf, lm);
            colorMatrix(sf, lm);
            colorApply(sf, lm);
            pushColor();
            break;
        case 0x1C:
            colorMatrix(sf, lm);
            colorApply(sf, lm);
            pushColor();
            break;
        case 0x1E:
            lightTransform(0, sf, lm);
            colorMatrix(sf, lm);
            pushColor();
            break;
        case 0x20:
            for (int index = 0; index < 3; ++index) {
                lightTransform(index, sf, lm);
                colorMatrix(sf, lm);
                pushColor();
            }
            break;
        case 0x28:
            setMac(1, static_cast<int64_t>(ir(0)) * ir(0), sf);
            setMac(2, static_cast<int64_t>(ir(1)) * ir(1), sf);
            setMac(3, static_cast<int64_t>(ir(2)) * ir(2), sf);
            setIr(0, mac_[1], lm);
            setIr(1, mac_[2], lm);
            setIr(2, mac_[3], lm);
            break;
        case 0x29:
            depthCueColor(sf, lm);
            pushColor();
            break;
        case 0x2A:
            for (int index = 0; index < 3; ++index) {
                depthCue(static_cast<int64_t>(data_[20] & 0xFFu) << 16,
                         static_cast<int64_t>((data_[20] >> 8) & 0xFFu) << 16,
                         static_cast<int64_t>((data_[20] >> 16) & 0xFFu) << 16, sf, lm);
                pushColor();
            }
            break;
        case 0x2D:
        case 0x2E: {
            const bool quad = opcode == 0x2E;
            const int64_t scale = static_cast<int16_t>(control_[quad ? 30 : 29] & 0xFFFFu);
            const int64_t sum = quad ? static_cast<int64_t>(data_[16] & 0xFFFFu) + (data_[17] & 0xFFFFu) +
                                          (data_[18] & 0xFFFFu) + (data_[19] & 0xFFFFu)
                                    : static_cast<int64_t>(data_[17] & 0xFFFFu) + (data_[18] & 0xFFFFu) +
                                          (data_[19] & 0xFFFFu);
            const int64_t raw = scale * sum;
            setMac0(raw);
            int64_t otz = raw >> 12;
            if (otz < 0) {
                setFlag(kSizeSaturated);
                otz = 0;
            } else if (otz > 0xFFFF) {
                setFlag(kSizeSaturated);
                otz = 0xFFFF;
            }
            data_[7] = static_cast<uint32_t>(otz);
            break;
        }
        case 0x30:
            rtps(0, false, sf, lm);
            rtps(1, false, sf, lm);
            rtps(2, true, sf, lm);
            break;
        case 0x3D:
            setMac(1, static_cast<int64_t>(ir0()) * ir(0), sf);
            setMac(2, static_cast<int64_t>(ir0()) * ir(1), sf);
            setMac(3, static_cast<int64_t>(ir0()) * ir(2), sf);
            setIr(0, mac_[1], lm);
            setIr(1, mac_[2], lm);
            setIr(2, mac_[3], lm);
            pushColor();
            break;
        case 0x3E:
            setMac(1, (sf ? (mac_[1] << 12) : mac_[1]) + static_cast<int64_t>(ir0()) * ir(0), sf);
            setMac(2, (sf ? (mac_[2] << 12) : mac_[2]) + static_cast<int64_t>(ir0()) * ir(1), sf);
            setMac(3, (sf ? (mac_[3] << 12) : mac_[3]) + static_cast<int64_t>(ir0()) * ir(2), sf);
            setIr(0, mac_[1], lm);
            setIr(1, mac_[2], lm);
            setIr(2, mac_[3], lm);
            pushColor();
            break;
        case 0x3F:
            for (int index = 0; index < 3; ++index) {
                lightTransform(index, sf, lm);
                colorMatrix(sf, lm);
                colorApply(sf, lm);
                pushColor();
            }
            break;
        default:
            break;
    }
    finishFlags();
}

}  // namespace psxreborn
