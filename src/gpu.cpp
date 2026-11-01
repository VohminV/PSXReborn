#include "psxreborn/gpu.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace psxreborn {

namespace {

constexpr uint32_t kDisplayAreaRightDefault = 0x01FF03FFu;
constexpr uint32_t kBlendModeAverage = 0;   // B/2 + F/2
constexpr uint32_t kBlendModeAdditive = 1;  // B + F
constexpr uint32_t kBlendModeSubtractive = 2;
constexpr uint32_t kBlendModeQuarter = 3;   // B + F/4

int32_t edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

uint32_t saturate5(uint32_t value) { return value > 0x1Fu ? 0x1Fu : value; }

}  // namespace

Gpu::Gpu() : vram_(static_cast<size_t>(kVramWidth) * kVramHeight, 0) {
    reset();
}

void Gpu::reset() {
    std::fill(vram_.begin(), vram_.end(), uint16_t{0});
    resetState();
}

void Gpu::resetState() {
    params_ = {};
    command_ = 0;
    expected_ = 0;
    received_ = 0;
    transfer_ = Transfer::None;
    transferX_ = 0;
    transferY_ = 0;
    transferWidth_ = 0;
    transferHeight_ = 0;
    transferIndex_ = 0;
    transferRemaining_ = 0;
    readLatch_ = 0;
    drawMode_ = 0;
    textureWindow_ = 0;
    drawAreaLeft_ = 0;
    drawAreaRight_ = kDisplayAreaRightDefault;
    drawOffset_ = 0;
    maskBits_ = 0;
    dmaDirection_ = 0;
    displayMode_ = 0;
    displayStart_ = 0;
    horizontalRange_ = 0;
    verticalRange_ = 0;
    displayDisabled_ = false;
    irq_ = false;
    scanlineCycles_ = 0;
    field_ = false;
}

void Gpu::tick(uint32_t cycles) {
    scanlineCycles_ += cycles;
    constexpr uint32_t kScanlineCycles = 33868800u / (60u * 263u);
    while (scanlineCycles_ >= kScanlineCycles) {
        scanlineCycles_ -= kScanlineCycles;
        field_ = !field_;
    }
}

bool Gpu::inRange(uint32_t address, uint32_t base, uint32_t size) {
    return address >= base && address < base + size;
}

void Gpu::setFault(MemoryFault* fault, MemoryFault value) {
    if (fault != nullptr) {
        *fault = value;
    }
}

uint16_t Gpu::colorTo555(uint32_t color) {
    const uint32_t red = (color >> 3) & 0x1Fu;
    const uint32_t green = ((color >> 8) >> 3) & 0x1Fu;
    const uint32_t blue = ((color >> 16) >> 3) & 0x1Fu;
    return static_cast<uint16_t>(red | (green << 5) | (blue << 10));
}

Gpu::ShadeColor Gpu::shadeColorFromWord(uint32_t word) {
    return ShadeColor{word & 0xFFu, (word >> 8) & 0xFFu, (word >> 16) & 0xFFu};
}

uint16_t Gpu::blendChannels(uint16_t back, uint16_t front, uint32_t blendMode) {
    // 5-bit channel math per psx-spx: B/2+F/2, B+F, B-F, B+F/4; saturates/clamps.
    // The texel STP flag (bit15 of the front pixel) passes through to the
    // framebuffer write bit per the GP0(E6h) rules.
    uint16_t result = front & 0x8000u;
    for (uint32_t shift = 0; shift < 15; shift += 5) {
        const uint32_t backChannel = (static_cast<uint32_t>(back) >> shift) & 0x1Fu;
        const uint32_t frontChannel = (static_cast<uint32_t>(front) >> shift) & 0x1Fu;
        uint32_t blended = 0;
        switch (blendMode) {
            case kBlendModeAdditive:
                blended = backChannel + frontChannel;
                break;
            case kBlendModeSubtractive:
                blended = backChannel - frontChannel;
                break;
            case kBlendModeQuarter:
                blended = backChannel + (frontChannel >> 2);
                break;
            case kBlendModeAverage:
            default:
                blended = (backChannel + frontChannel) >> 1;
                break;
        }
        result = static_cast<uint16_t>(result | (saturate5(blended) << shift));
    }
    return result;
}

uint16_t Gpu::modulateTexel(uint16_t texel, const ShadeColor& color) {
    // (texel.rgb * color.rgb) / 128 with 5-bit texel and 8-bit vertex color,
    // each channel saturated to 0x1F (psx-spx Modulation section, v2 GPU).
    const uint32_t red = (((texel & 0x1Fu) * color.red) >> 7);
    const uint32_t green = ((((texel >> 5) & 0x1Fu) * color.green) >> 7);
    const uint32_t blue = ((((texel >> 10) & 0x1Fu) * color.blue) >> 7);
    return static_cast<uint16_t>(saturate5(red) | (saturate5(green) << 5) |
                                 (saturate5(blue) << 10));
}

bool Gpu::composePixel(uint16_t texel, const ShadeColor& color, bool textured, bool raw,
                       uint16_t& output, bool& stpFlag) {
    // Texel color 0000h is fully transparent and is not drawn at all.
    if (textured) {
        if (texel == 0) {
            return false;
        }
        stpFlag = (texel & 0x8000u) != 0;
        if (raw) {
            output = texel;
        } else {
            output = static_cast<uint16_t>(modulateTexel(texel, color) |
                                           (stpFlag ? 0x8000u : 0u));
        }
    } else {
        const uint32_t packed = (color.blue << 16) | (color.green << 8) | color.red;
        output = colorTo555(packed);
        stpFlag = false;
    }
    return true;
}

uint32_t Gpu::commandLength(uint32_t word) {
    const uint32_t opcode = word >> 24;
    if (opcode == 0x02u) {
        return 3;
    }
    if (opcode >= 0x20u && opcode <= 0x3Fu) {
        const uint32_t vertices = (word & (1u << 27)) != 0 ? 4u : 3u;
        const uint32_t gouraud = (word & (1u << 28)) != 0 ? 1u : 0u;
        const uint32_t textured = (word & (1u << 26)) != 0 ? 1u : 0u;
        return 1u + vertices * (1u + textured) + (gouraud != 0 ? vertices - 1u : 0u);
    }
    if (opcode >= 0x40u && opcode <= 0x5Fu) {
        const uint32_t gouraud = (word & (1u << 28)) != 0 ? 1u : 0u;
        return 1u + gouraud + 1u;
    }
    if (opcode >= 0x60u && opcode <= 0x7Fu) {
        const uint32_t textured = (word & (1u << 26)) != 0 ? 1u : 0u;
        const uint32_t variableSize = ((word >> 27) & 3u) == 0 ? 1u : 0u;
        return 2u + textured + variableSize;
    }
    if (opcode >= 0x80u && opcode <= 0x9Fu) {
        return 4;
    }
    if (opcode >= 0xA0u && opcode <= 0xDFu) {
        return 3;
    }
    return 1;
}

int32_t Gpu::signExtend11(uint32_t value) {
    const uint32_t masked = value & 0x7FFu;
    return static_cast<int32_t>(masked << 21) >> 21;
}

uint32_t Gpu::status() const {
    uint32_t value = drawMode_ & 0x7FFu;
    value |= (displayMode_ & 3u) << 17;
    if ((displayMode_ & 0x04u) != 0) {
        value |= 1u << 19;
    }
    if ((displayMode_ & 0x08u) != 0) {
        value |= 1u << 20;
    }
    if ((displayMode_ & 0x10u) != 0) {
        value |= 1u << 21;
    }
    if ((displayMode_ & 0x20u) != 0) {
        value |= 1u << 22;
    }
    if ((displayMode_ & 0x40u) != 0) {
        value |= 1u << 16;
    }
    if ((displayMode_ & 0x80u) != 0) {
        value |= 1u << 14;
    }
    if (displayDisabled_) {
        value |= 1u << 23;
    }
    if (irq_) {
        value |= 1u << 24;
    }
    if (dmaDirection_ == 1 || dmaDirection_ == 2) {
        value |= 1u << 25;
    } else if (dmaDirection_ == 3 && transferRemaining_ != 0) {
        value |= 1u << 25;
    }
    value |= 1u << 26;
    if (transfer_ == Transfer::VramToCpu && transferRemaining_ != 0) {
        value |= 1u << 27;
    }
    value |= 1u << 28;
    value |= (dmaDirection_ & 3u) << 29;
    if (field_) {
        value |= 1u << 31;
    }
    return value;
}

uint32_t Gpu::displayWidth() const {
    if ((displayMode_ & 0x40u) != 0) {
        return 368u;
    }
    switch (displayMode_ & 3u) {
        case 0:
            return 256u;
        case 1:
            return 320u;
        case 2:
            return 512u;
        case 3:
            return 640u;
        default:
            return 320u;
    }
}

uint32_t Gpu::displayHeight() const {
    return (displayMode_ & 0x04u) != 0 ? 480u : 240u;
}

bool Gpu::read32(uint32_t address, uint32_t& value, MemoryFault* fault) {
    if (address == kGp0Address) {
        readGp0(value);
        setFault(fault, MemoryFault::None);
        return true;
    }
    if (address == kGp1Address) {
        value = status();
        setFault(fault, MemoryFault::None);
        return true;
    }
    setFault(fault, MemoryFault::Unsupported);
    return false;
}

bool Gpu::write32(uint32_t address, uint32_t value, MemoryFault* fault) {
    if (address == kGp0Address) {
        writeGp0(value);
        setFault(fault, MemoryFault::None);
        return true;
    }
    if (address == kGp1Address) {
        executeGp1(value);
        setFault(fault, MemoryFault::None);
        return true;
    }
    setFault(fault, MemoryFault::Unsupported);
    return false;
}

void Gpu::beginCommand(uint32_t word) {
    command_ = word;
    params_[0] = word;
    received_ = 1;
    expected_ = commandLength(word);
    if (expected_ <= 1) {
        executeCommand();
        received_ = 0;
        expected_ = 0;
    }
}

bool Gpu::writeGp0(uint32_t word) {
    if (transfer_ == Transfer::CpuToVram) {
        writeTransferWord(word);
        return true;
    }
    if (received_ == 0) {
        beginCommand(word);
        return true;
    }
    if (received_ < params_.size()) {
        params_[received_] = word;
    }
    ++received_;
    if (received_ >= expected_) {
        executeCommand();
        received_ = 0;
        expected_ = 0;
    }
    return true;
}

bool Gpu::readGp0(uint32_t& word) {
    if (transfer_ == Transfer::VramToCpu) {
        if (transferRemaining_ == 0) {
            transfer_ = Transfer::None;
            word = 0xFFFFFFFFu;
            return true;
        }
        const uint32_t total = transferWidth_ * transferHeight_;
        const uint32_t firstX = transferX_ + (transferIndex_ % transferWidth_);
        const uint32_t firstY = transferY_ + (transferIndex_ / transferWidth_);
        word = vram(firstX, firstY);
        uint32_t next = transferIndex_ + 1u;
        if (next < total) {
            const uint32_t secondX = transferX_ + (next % transferWidth_);
            const uint32_t secondY = transferY_ + (next / transferWidth_);
            word |= static_cast<uint32_t>(vram(secondX, secondY)) << 16;
        }
        transferIndex_ += 2u;
        transferRemaining_ = transferIndex_ >= total ? 0u : total - transferIndex_;
        if (transferRemaining_ == 0) {
            transfer_ = Transfer::None;
        }
        return true;
    }
    word = readLatch_;
    return true;
}

void Gpu::executeCommand() {
    const uint32_t opcode = command_ >> 24;
    ++stats_.gp0Commands;
    if (std::getenv("PSXREBORN_GPU_TRACE") != nullptr) {
        std::fprintf(stderr, "GP0 %08X", command_);
        for (uint32_t index = 1; index < expected_ && index < params_.size(); ++index) {
            std::fprintf(stderr, " %08X", params_[index]);
        }
        std::fprintf(stderr, " mode=%03X tw=%05X area=%04X,%04X off=%04X\n", drawMode_,
                     textureWindow_, drawAreaLeft_, drawAreaRight_, drawOffset_);
    }
    if (opcode == 0x02u) {
        ++stats_.fills;
        executeFill();
    } else if (opcode >= 0x20u && opcode <= 0x3Fu) {
        ++stats_.polygons;
        executePolygon();
    } else if (opcode >= 0x60u && opcode <= 0x7Fu) {
        ++stats_.rectangles;
        executeRectangle();
    } else if (opcode >= 0x80u && opcode <= 0x9Fu) {
        ++stats_.copies;
        executeCopy();
    } else if (opcode >= 0xA0u && opcode <= 0xBFu) {
        ++stats_.cpuToVram;
        executeCpuToVram();
    } else if (opcode >= 0xC0u && opcode <= 0xDFu) {
        ++stats_.vramToCpu;
        executeVramToCpu();
    } else if (opcode == 0xE1u) {
        drawMode_ = params_[0] & 0x3FFFu;
    } else if (opcode == 0xE2u) {
        textureWindow_ = params_[0] & 0xFFFFFu;
    } else if (opcode == 0xE3u) {
        drawAreaLeft_ = (params_[0] & 0x3FFu) | (((params_[0] >> 10) & 0x3FFu) << 16);
    } else if (opcode == 0xE4u) {
        drawAreaRight_ = (params_[0] & 0x3FFu) | (((params_[0] >> 10) & 0x3FFu) << 16);
    } else if (opcode == 0xE5u) {
        drawOffset_ = (params_[0] & 0x7FFu) | (((params_[0] >> 11) & 0x7FFu) << 16);
    } else if (opcode == 0xE6u) {
        maskBits_ = params_[0] & 3u;
    } else if (opcode == 0x1Fu) {
        irq_ = true;
    }
}

void Gpu::executeFill() {
    const uint16_t color = colorTo555(params_[0]);
    const int32_t x = static_cast<int32_t>(params_[1] & 0x3F0u);
    const int32_t y = static_cast<int32_t>((params_[1] >> 16) & 0x1FFu);
    const uint32_t width = ((params_[2] & 0x3FFu) + 0xFu) & ~0xFu;
    const uint32_t height = (params_[2] >> 16) & 0x1FFu;
    if (width == 0 || height == 0) {
        return;
    }
    for (uint32_t row = 0; row < height; ++row) {
        const uint32_t targetY = static_cast<uint32_t>(y) + row;
        if (targetY >= kVramHeight) {
            break;
        }
        for (uint32_t column = 0; column < width; ++column) {
            const uint32_t targetX = static_cast<uint32_t>(x) + column;
            if (targetX >= kVramWidth) {
                break;
            }
            setVram(targetX, targetY, color);
        }
    }
}

void Gpu::drawPixel(int32_t x, int32_t y, uint16_t color, bool blend, uint32_t blendMode) {
    const int32_t left = static_cast<int32_t>(drawAreaLeft_ & 0x3FFu);
    const int32_t top = static_cast<int32_t>((drawAreaLeft_ >> 16) & 0x3FFu);
    const int32_t right = static_cast<int32_t>(drawAreaRight_ & 0x3FFu);
    const int32_t bottom = static_cast<int32_t>((drawAreaRight_ >> 16) & 0x3FFu);
    if (x < left || x > right || y < top || y > bottom) {
        return;
    }
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= kVramWidth || static_cast<uint32_t>(y) >= kVramHeight) {
        return;
    }
    if ((maskBits_ & 2u) != 0) {
        const uint16_t existing = vram(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        if ((existing & 0x8000u) != 0) {
            return;
        }
    }
    if (blend) {
        color = blendChannels(vram(static_cast<uint32_t>(x), static_cast<uint32_t>(y)), color,
                              blendMode);
    }
    if ((maskBits_ & 1u) != 0) {
        color = static_cast<uint16_t>(color | 0x8000u);
    }
    ++stats_.pixelsDrawn;
    setVram(static_cast<uint32_t>(x), static_cast<uint32_t>(y), color);
}

void Gpu::executeRectangle() {
    const bool textured = (command_ & (1u << 26)) != 0;
    const bool raw = (command_ & (1u << 24)) != 0;
    const bool semiTrans = (command_ & (1u << 25)) != 0;
    const uint32_t sizeCode = (command_ >> 27) & 3u;
    const int32_t vertexX = signExtend11(params_[1]) + signExtend11(drawOffset_);
    const int32_t vertexY = signExtend11(params_[1] >> 16) + signExtend11(drawOffset_ >> 16);
    uint32_t width = 1;
    uint32_t height = 1;
    if (sizeCode == 0) {
        const uint32_t sizeIndex = textured ? 3 : 2;
        width = params_[sizeIndex] & 0x3FFu;
        height = (params_[sizeIndex] >> 16) & 0x1FFu;
        if (width == 0 || height == 0) {
            return;
        }
    } else if (sizeCode == 2) {
        width = 8;
        height = 8;
    } else if (sizeCode == 3) {
        width = 16;
        height = 16;
    }

    uint32_t clut = 0;
    int32_t baseU = 0;
    int32_t baseV = 0;
    if (textured) {
        const uint32_t uv = params_[2];
        baseU = static_cast<int32_t>(uv & 0xFFu);
        baseV = static_cast<int32_t>((uv >> 8) & 0xFFu);
        clut = (uv >> 16) & 0xFFFFu;
    }

    const ShadeColor color = shadeColorFromWord(params_[0]);
    const uint32_t blendMode = (drawMode_ >> 5) & 3u;
    const bool flipX = (drawMode_ & (1u << 12)) != 0;
    const bool flipY = (drawMode_ & (1u << 13)) != 0;
    for (uint32_t row = 0; row < height; ++row) {
        const int32_t v = flipY ? baseV - static_cast<int32_t>(row)
                                : baseV + static_cast<int32_t>(row);
        for (uint32_t column = 0; column < width; ++column) {
            const int32_t u = flipX ? baseU - static_cast<int32_t>(column)
                                    : baseU + static_cast<int32_t>(column);
            uint16_t texel = 0;
            if (textured) {
                texel = sampleTexture(u, v, drawMode_, clut);
            }
            uint16_t pixel = 0;
            bool stpFlag = false;
            if (!composePixel(texel, color, textured, raw, pixel, stpFlag)) {
                continue;
            }
            if (semiTrans && (!textured || stpFlag)) {
                drawPixel(vertexX + static_cast<int32_t>(column),
                          vertexY + static_cast<int32_t>(row), pixel, true, blendMode);
            } else {
                drawPixel(vertexX + static_cast<int32_t>(column),
                          vertexY + static_cast<int32_t>(row), pixel, false, blendMode);
            }
        }
    }
}

void Gpu::executePolygon() {
    const bool textured = (command_ & (1u << 26)) != 0;
    const uint32_t vertexCount = (command_ & (1u << 27)) != 0 ? 4u : 3u;
    const bool gouraud = (command_ & (1u << 28)) != 0;
    const bool raw = (command_ & (1u << 24)) != 0;
    const bool semiTrans = (command_ & (1u << 25)) != 0;
    std::array<int32_t, 4> xs{};
    std::array<int32_t, 4> ys{};
    std::array<int32_t, 4> us{};
    std::array<int32_t, 4> vs{};
    std::array<ShadeColor, 4> colors{};
    uint32_t clut = 0;
    uint32_t texpageMirror = 0;
    uint32_t index = 1;
    for (uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        if (gouraud && vertex != 0) {
            colors[vertex] = shadeColorFromWord(params_[index++]);
        } else {
            colors[vertex] = shadeColorFromWord(params_[0]);
        }
        const uint32_t position = params_[index++];
        xs[vertex] = signExtend11(position) + signExtend11(drawOffset_);
        ys[vertex] = signExtend11(position >> 16) + signExtend11(drawOffset_ >> 16);
        if (textured) {
            const uint32_t uv = params_[index++];
            us[vertex] = static_cast<int32_t>(uv & 0xFFu);
            vs[vertex] = static_cast<int32_t>((uv >> 8) & 0xFFu);
            if (vertex == 0) {
                clut = (uv >> 16) & 0xFFFFu;
            } else if (vertex == 1) {
                texpageMirror = (uv >> 16) & 0x08FFu;
            }
        }
    }
    uint32_t texpage = drawMode_;
    if (textured && vertexCount >= 2) {
        texpage = (texpage & ~0x08FFu) | texpageMirror;
    }
    const uint32_t blendMode = (texpage >> 5) & 3u;
    std::array<int32_t, 3> triXs{};
    std::array<int32_t, 3> triYs{};
    std::array<int32_t, 3> triUs{};
    std::array<int32_t, 3> triVs{};
    std::array<ShadeColor, 3> triColors{};
    for (uint32_t i = 0; i < 3; ++i) {
        triXs[i] = xs[i];
        triYs[i] = ys[i];
        triUs[i] = us[i];
        triVs[i] = vs[i];
        triColors[i] = colors[i];
    }
    drawTriangle(triXs, triYs, triUs, triVs, triColors, textured, gouraud, raw, semiTrans,
                 blendMode, texpage, clut);
    if (vertexCount == 4) {
        for (uint32_t i = 0; i < 3; ++i) {
            triXs[i] = xs[i + 1];
            triYs[i] = ys[i + 1];
            triUs[i] = us[i + 1];
            triVs[i] = vs[i + 1];
            triColors[i] = colors[i + 1];
        }
        drawTriangle(triXs, triYs, triUs, triVs, triColors, textured, gouraud, raw, semiTrans,
                     blendMode, texpage, clut);
    }
}

void Gpu::drawTriangle(const std::array<int32_t, 3>& xs, const std::array<int32_t, 3>& ys,
                       const std::array<int32_t, 3>& us, const std::array<int32_t, 3>& vs,
                       const std::array<ShadeColor, 3>& colors, bool textured, bool gouraud,
                       bool raw, bool semiTrans, uint32_t blendMode, uint32_t texpage,
                       uint32_t clut) {
    const int32_t area = edge(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]);
    if (area == 0) {
        return;
    }
    const int32_t minX = std::min({xs[0], xs[1], xs[2]});
    const int32_t maxX = std::max({xs[0], xs[1], xs[2]});
    const int32_t minY = std::min({ys[0], ys[1], ys[2]});
    const int32_t maxY = std::max({ys[0], ys[1], ys[2]});
    const int32_t sign = area > 0 ? 1 : -1;
    const int32_t ratio = area * sign;
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            const int32_t w0 = edge(xs[1], ys[1], xs[2], ys[2], x, y) * sign;
            const int32_t w1 = edge(xs[2], ys[2], xs[0], ys[0], x, y) * sign;
            const int32_t w2 = edge(xs[0], ys[0], xs[1], ys[1], x, y) * sign;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            ShadeColor pixelColor = colors[0];
            if (gouraud) {
                pixelColor.red =
                    static_cast<uint32_t>((w0 * colors[0].red + w1 * colors[1].red +
                                           w2 * colors[2].red) / ratio);
                pixelColor.green =
                    static_cast<uint32_t>((w0 * colors[0].green + w1 * colors[1].green +
                                           w2 * colors[2].green) / ratio);
                pixelColor.blue =
                    static_cast<uint32_t>((w0 * colors[0].blue + w1 * colors[1].blue +
                                           w2 * colors[2].blue) / ratio);
            }
            uint16_t texel = 0;
            if (textured) {
                const int32_t u = (w0 * us[0] + w1 * us[1] + w2 * us[2]) / ratio;
                const int32_t v = (w0 * vs[0] + w1 * vs[1] + w2 * vs[2]) / ratio;
                texel = sampleTexture(u, v, texpage, clut);
            }
            uint16_t pixel = 0;
            bool stpFlag = false;
            if (!composePixel(texel, pixelColor, textured, raw, pixel, stpFlag)) {
                continue;
            }
            const bool blended = semiTrans && (!textured || stpFlag);
            drawPixel(x, y, pixel, blended, blendMode);
        }
    }
}

uint16_t Gpu::sampleTexture(int32_t u, int32_t v, uint32_t texpage, uint32_t clut) const {
    const uint32_t pageX = (texpage & 0xFu) * 64u;
    const uint32_t pageY = ((texpage >> 4) & 1u) * 256u + ((texpage >> 11) & 1u) * 512u;
    const uint32_t depth = (texpage >> 7) & 3u;
    const uint32_t maskX = (textureWindow_ & 0x1Fu) * 8u;
    const uint32_t maskY = ((textureWindow_ >> 5) & 0x1Fu) * 8u;
    const uint32_t offsetX = (textureWindow_ >> 10) & 0x1Fu;
    const uint32_t offsetY = (textureWindow_ >> 15) & 0x1Fu;
    const uint32_t coordU =
        ((static_cast<uint32_t>(u) & 0xFFu) & ~maskX) | ((offsetX & (maskX >> 3)) << 3);
    const uint32_t coordV =
        ((static_cast<uint32_t>(v) & 0xFFu) & ~maskY) | ((offsetY & (maskY >> 3)) << 3);
    const uint32_t texY = pageY + coordV;
    if (texY >= kVramHeight) {
        return 0;
    }
    if (depth == 0) {
        const uint32_t texX = pageX + (coordU >> 2);
        if (texX >= kVramWidth) {
            return 0;
        }
        const uint32_t word = vram(texX, texY);
        const uint32_t paletteIndex = (word >> ((coordU & 3u) * 4u)) & 0xFu;
        const uint32_t clutX = (clut & 0x3Fu) * 16u;
        const uint32_t clutY = (clut >> 6) & 0x1FFu;
        return vram(clutX + paletteIndex, clutY);
    }
    if (depth == 1) {
        const uint32_t texX = pageX + (coordU >> 1);
        if (texX >= kVramWidth) {
            return 0;
        }
        const uint32_t word = vram(texX, texY);
        const uint32_t paletteIndex = (word >> ((coordU & 1u) * 8u)) & 0xFFu;
        const uint32_t clutX = (clut & 0x3Fu) * 16u;
        const uint32_t clutY = (clut >> 6) & 0x1FFu;
        return vram(clutX + paletteIndex, clutY);
    }
    if (pageX + coordU >= kVramWidth) {
        return 0;
    }
    return vram(pageX + coordU, texY);
}

void Gpu::executeCopy() {
    const int32_t sourceX = static_cast<int32_t>(params_[1] & 0x3FFu);
    const int32_t sourceY = static_cast<int32_t>((params_[1] >> 16) & 0x1FFu);
    const int32_t targetX = static_cast<int32_t>(params_[2] & 0x3FFu);
    const int32_t targetY = static_cast<int32_t>((params_[2] >> 16) & 0x1FFu);
    const uint32_t width = ((params_[3] - 1u) & 0x3FFu) + 1u;
    const uint32_t height = (((params_[3] >> 16) - 1u) & 0x1FFu) + 1u;
    std::vector<uint16_t> temporary;
    temporary.reserve(static_cast<size_t>(width) * height);
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            temporary.push_back(vram(static_cast<uint32_t>(sourceX) + column, static_cast<uint32_t>(sourceY) + row));
        }
    }
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            setVram(static_cast<uint32_t>(targetX) + column, static_cast<uint32_t>(targetY) + row,
                    temporary[static_cast<size_t>(row) * width + column]);
        }
    }
}

void Gpu::executeCpuToVram() {
    transfer_ = Transfer::CpuToVram;
    transferX_ = params_[1] & 0x3FFu;
    transferY_ = (params_[1] >> 16) & 0x1FFu;
    transferWidth_ = ((params_[2] - 1u) & 0x3FFu) + 1u;
    transferHeight_ = (((params_[2] >> 16) - 1u) & 0x1FFu) + 1u;
    transferIndex_ = 0;
    transferRemaining_ = transferWidth_ * transferHeight_;
}

void Gpu::writeTransferWord(uint32_t word) {
    const uint32_t total = transferWidth_ * transferHeight_;
    const uint32_t firstX = transferX_ + (transferIndex_ % transferWidth_);
    const uint32_t firstY = transferY_ + (transferIndex_ / transferWidth_);
    setVram(firstX, firstY, static_cast<uint16_t>(word & 0xFFFFu));
    const uint32_t second = transferIndex_ + 1u;
    if (second < total) {
        const uint32_t secondX = transferX_ + (second % transferWidth_);
        const uint32_t secondY = transferY_ + (second / transferWidth_);
        setVram(secondX, secondY, static_cast<uint16_t>(word >> 16));
    }
    ++stats_.transferWords;
    transferIndex_ += 2u;
    if (transferIndex_ >= total) {
        transfer_ = Transfer::None;
        transferIndex_ = 0;
    }
}

void Gpu::executeVramToCpu() {
    transfer_ = Transfer::VramToCpu;
    transferX_ = params_[1] & 0x3FFu;
    transferY_ = (params_[1] >> 16) & 0x1FFu;
    transferWidth_ = ((params_[2] - 1u) & 0x3FFu) + 1u;
    transferHeight_ = (((params_[2] >> 16) - 1u) & 0x1FFu) + 1u;
    transferIndex_ = 0;
    transferRemaining_ = transferWidth_ * transferHeight_;
}

void Gpu::executeGp1(uint32_t word) {
    const uint32_t opcode = word >> 24;
    ++stats_.gp1Commands;
    if (opcode == 0x00u) {
        resetState();
    } else if (opcode == 0x01u) {
        received_ = 0;
        expected_ = 0;
        transfer_ = Transfer::None;
    } else if (opcode == 0x02u) {
        irq_ = false;
    } else if (opcode == 0x03u) {
        displayDisabled_ = (word & 1u) != 0;
    } else if (opcode == 0x04u) {
        dmaDirection_ = word & 3u;
    } else if (opcode == 0x05u) {
        displayStart_ = word & 0x0007FFFFu;
    } else if (opcode == 0x06u) {
        horizontalRange_ = word & 0x00FFFFFFu;
    } else if (opcode == 0x07u) {
        verticalRange_ = word & 0x00FFFFFFu;
    } else if (opcode == 0x08u) {
        displayMode_ = word & 0xFFu;
    } else if (opcode == 0x10u) {
        const uint32_t info = word & 0x0Fu;
        if (info == 0) {
            readLatch_ = drawMode_ & 0x7FFu;
        } else if (info == 2) {
            readLatch_ = textureWindow_;
        } else if (info == 3) {
            readLatch_ = drawAreaLeft_;
        } else if (info == 4) {
            readLatch_ = drawAreaRight_;
        } else if (info == 5) {
            readLatch_ = drawOffset_;
        } else if (info == 7) {
            readLatch_ = 2;
        } else {
            readLatch_ = 0;
        }
    }
}

uint16_t Gpu::vram(uint32_t x, uint32_t y) const {
    if (x >= kVramWidth || y >= kVramHeight) {
        return 0;
    }
    return vram_[static_cast<size_t>(y) * kVramWidth + x];
}

void Gpu::setVram(uint32_t x, uint32_t y, uint16_t value) {
    if (x >= kVramWidth || y >= kVramHeight) {
        return;
    }
    vram_[static_cast<size_t>(y) * kVramWidth + x] = value;
}

}  // namespace psxreborn
