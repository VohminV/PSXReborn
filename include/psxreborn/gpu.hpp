#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "psxreborn/memory.hpp"

namespace psxreborn {

struct GpuStats {
    uint64_t gp0Commands = 0;
    uint64_t polygons = 0;
    uint64_t rectangles = 0;
    uint64_t fills = 0;
    uint64_t copies = 0;
    uint64_t cpuToVram = 0;
    uint64_t vramToCpu = 0;
    uint64_t gp1Commands = 0;
    uint64_t transferWords = 0;
    uint64_t pixelsDrawn = 0;
};

class Gpu {
public:
    static constexpr uint32_t kGp0Address = 0x1F801810u;
    static constexpr uint32_t kGp1Address = 0x1F801814u;
    static constexpr uint32_t kVramWidth = 1024;
    static constexpr uint32_t kVramHeight = 512;

    Gpu();

    const GpuStats& stats() const { return stats_; }

    void reset();
    void tick(uint32_t cycles);
    bool read32(uint32_t address, uint32_t& value, MemoryFault* fault);
    bool write32(uint32_t address, uint32_t value, MemoryFault* fault);

    uint32_t status() const;
    uint32_t dmaDirection() const { return dmaDirection_; }
    bool writeGp0(uint32_t word);
    bool readGp0(uint32_t& word);
    bool dmaRead(uint32_t& word) { return readGp0(word); }

    uint16_t vram(uint32_t x, uint32_t y) const;
    void setVram(uint32_t x, uint32_t y, uint16_t value);
    const std::vector<uint16_t>& vram() const { return vram_; }

    uint32_t displayStartX() const { return displayStart_ & 0x3FFu; }
    uint32_t displayStartY() const { return (displayStart_ >> 10) & 0x1FFu; }
    uint32_t displayWidth() const;
    uint32_t displayHeight() const;
    uint32_t displayMode() const { return displayMode_; }
    bool displayDisabled() const { return displayDisabled_; }

private:
    enum class Transfer {
        None,
        CpuToVram,
        VramToCpu,
    };

    struct ShadeColor {
        uint32_t red = 0;
        uint32_t green = 0;
        uint32_t blue = 0;
    };

    static bool inRange(uint32_t address, uint32_t base, uint32_t size);
    static uint16_t colorTo555(uint32_t color);
    static uint16_t blendChannels(uint16_t back, uint16_t front, uint32_t blendMode);
    static uint16_t modulateTexel(uint16_t texel, const ShadeColor& color);
    static bool composePixel(uint16_t texel, const ShadeColor& color, bool textured, bool raw,
                             uint16_t& output, bool& stpFlag);
    static uint32_t commandLength(uint32_t word);
    static int32_t signExtend11(uint32_t value);
    static void setFault(MemoryFault* fault, MemoryFault value);
    static ShadeColor shadeColorFromWord(uint32_t word);

    void resetState();
    void beginCommand(uint32_t word);
    void executeCommand();
    void executeFill();
    void executeRectangle();
    void executePolygon();
    void executeCopy();
    void executeCpuToVram();
    void executeVramToCpu();
    void writeTransferWord(uint32_t word);
    void drawPixel(int32_t x, int32_t y, uint16_t color, bool blend, uint32_t blendMode);
    void drawTriangle(const std::array<int32_t, 3>& xs, const std::array<int32_t, 3>& ys,
                      const std::array<int32_t, 3>& us, const std::array<int32_t, 3>& vs,
                      const std::array<ShadeColor, 3>& colors, bool textured, bool gouraud,
                      bool raw, bool semiTrans, uint32_t blendMode, uint32_t texpage,
                      uint32_t clut);
    uint16_t sampleTexture(int32_t u, int32_t v, uint32_t texpage, uint32_t clut) const;
    void executeGp1(uint32_t word);

    std::vector<uint16_t> vram_;
    std::array<uint32_t, 16> params_{};
    uint32_t command_ = 0;
    uint32_t expected_ = 0;
    uint32_t received_ = 0;
    Transfer transfer_ = Transfer::None;
    uint32_t transferX_ = 0;
    uint32_t transferY_ = 0;
    uint32_t transferWidth_ = 0;
    uint32_t transferHeight_ = 0;
    uint32_t transferIndex_ = 0;
    uint32_t transferRemaining_ = 0;
    uint32_t readLatch_ = 0;
    uint32_t drawMode_ = 0;
    uint32_t textureWindow_ = 0;
    uint32_t drawAreaLeft_ = 0;
    uint32_t drawAreaRight_ = 0;
    uint32_t drawOffset_ = 0;
    uint32_t maskBits_ = 0;
    uint32_t dmaDirection_ = 0;
    uint32_t displayMode_ = 0;
    uint32_t displayStart_ = 0;
    uint32_t horizontalRange_ = 0;
    uint32_t verticalRange_ = 0;
    bool displayDisabled_ = false;
    bool irq_ = false;
    uint32_t scanlineCycles_ = 0;
    bool field_ = false;
    GpuStats stats_{};
};

}  // namespace psxreborn
