#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace psxreborn {

class Gte {
public:
    static constexpr size_t kDataRegisterCount = 32;
    static constexpr size_t kControlRegisterCount = 32;

    void reset();

    uint32_t readData(uint8_t index) const;
    uint32_t readControl(uint8_t index) const;
    void writeData(uint8_t index, uint32_t value);
    void writeControl(uint8_t index, uint32_t value);

    void execute(uint32_t command);

    uint32_t flags() const { return control_[31]; }
    uint32_t dataRegister(uint8_t index) const { return data_[index & 0x1Fu]; }
    uint32_t controlRegister(uint8_t index) const { return control_[index & 0x1Fu]; }

private:
    int16_t matrixElement(int base, int row, int column) const;
    int16_t vertexComponent(int vector, int component) const;
    int32_t controlVector(int vector, int component) const;
    int16_t ir(int component) const;
    int16_t ir0() const;
    void setIr(int component, int64_t value, bool lm);
    void setMac(int component, int64_t rawValue, bool sf);
    void setMac0(int64_t rawValue);
    void setFlag(uint32_t flag) { control_[31] |= flag; }
    void finishFlags();
    void pushColor();
    void pushZ(uint16_t value);
    void pushSxy(int32_t x, int32_t y);
    void transform(int matrix, int vector, int translation, bool sf, bool lm, int64_t& rawMac3);
    void colorMatrix(bool sf, bool lm);
    void lightTransform(int vector, bool sf, bool lm);
    void colorApply(bool sf, bool lm);
    void depthCue(int64_t inputR, int64_t inputG, int64_t inputB, bool sf, bool lm);
    void depthCueColor(bool sf, bool lm);
    void rtps(int vector, bool last, bool sf, bool lm);

    uint32_t divide(uint16_t numerator, uint16_t denominator);

    std::array<uint32_t, kDataRegisterCount> data_{};
    std::array<uint32_t, kControlRegisterCount> control_{};
    std::array<int64_t, 4> mac_{};
};

}  // namespace psxreborn
