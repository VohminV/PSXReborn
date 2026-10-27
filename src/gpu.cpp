#include "psxreborn/gpu.hpp"

#include <algorithm>
#include <cstdlib>

namespace psxreborn {

namespace {

constexpr uint32_t kDisplayAreaRightDefault = 0x01FF03FFu;

int32_t edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

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
    if (opcode == 0x02u) {
        executeFill();
    } else if (opcode >= 0x20u && opcode <= 0x3Fu) {
        executePolygon();
    } else if (opcode >= 0x60u && opcode <= 0x7Fu) {
        executeRectangle();
    } else if (opcode >= 0x80u && opcode <= 0x9Fu) {
        executeCopy();
    } else if (opcode >= 0xA0u && opcode <= 0xBFu) {
        executeCpuToVram();
    } else if (opcode >= 0xC0u && opcode <= 0xDFu) {
        executeVramToCpu();
    } else if (opcode == 0xE1u) {
        drawMode_ = params_[0] & 0xFFFu;
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
    const int32_t x = static_cast<int32_t>(params_[1] & 0x3FFu) & ~15;
    const int32_t y = static_cast<int32_t>((params_[1] >> 16) & 0x1FFu);
    const uint32_t width = (params_[2] & 0x3FFu) + 1u;
    const uint32_t height = ((params_[2] >> 16) & 0x1FFu) + 1u;
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

void Gpu::drawPixel(int32_t x, int32_t y, uint16_t color) {
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
    if ((maskBits_ & 1u) != 0) {
        color = static_cast<uint16_t>(color | 0x8000u);
    }
    setVram(static_cast<uint32_t>(x), static_cast<uint32_t>(y), color);
}

void Gpu::executeRectangle() {
    const bool textured = (command_ & (1u << 26)) != 0;
    const uint32_t sizeCode = (command_ >> 27) & 3u;
    const int32_t vertexX = signExtend11(params_[1]) + signExtend11(drawOffset_);
    const int32_t vertexY = signExtend11(params_[1] >> 16) + signExtend11(drawOffset_ >> 16);
    uint32_t width = 1;
    uint32_t height = 1;
    if (sizeCode == 0) {
        uint32_t sizeIndex = textured ? 3 : 2;
        width = (params_[sizeIndex] & 0x3FFu) + 1u;
        height = ((params_[sizeIndex] >> 16) & 0x1FFu) + 1u;
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

    const uint16_t color = colorTo555(params_[0]);
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            uint16_t pixel = color;
            if (textured) {
                pixel = sampleTexture(baseU + static_cast<int32_t>(column), baseV + static_cast<int32_t>(row), clut);
                if (pixel == 0) {
                    continue;
                }
            }
            drawPixel(vertexX + static_cast<int32_t>(column), vertexY + static_cast<int32_t>(row), pixel);
        }
    }
}

void Gpu::executePolygon() {
    const bool textured = (command_ & (1u << 26)) != 0;
    const uint32_t vertexCount = (command_ & (1u << 27)) != 0 ? 4u : 3u;
    const bool gouraud = (command_ & (1u << 28)) != 0;
    std::array<int32_t, 4> xs{};
    std::array<int32_t, 4> ys{};
    std::array<int32_t, 4> us{};
    std::array<int32_t, 4> vs{};
    uint32_t clut = 0;
    uint32_t index = 1;
    for (uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        if (gouraud && vertex != 0) {
            ++index;
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
            }
        }
    }
    const uint16_t color = colorTo555(params_[0]);
    drawTriangle(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], color, textured,
                 us[0], vs[0], us[1], vs[1], us[2], vs[2]);
    if (vertexCount == 4) {
        drawTriangle(xs[1], ys[1], xs[2], ys[2], xs[3], ys[3], color, textured,
                     us[1], vs[1], us[2], vs[2], us[3], vs[3]);
    }
    (void)clut;
}

void Gpu::drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       uint16_t color, bool textured, int32_t u0, int32_t v0, int32_t u1, int32_t v1,
                       int32_t u2, int32_t v2) {
    const int32_t area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;
    }
    const int32_t minX = std::min({x0, x1, x2});
    const int32_t maxX = std::max({x0, x1, x2});
    const int32_t minY = std::min({y0, y1, y2});
    const int32_t maxY = std::max({y0, y1, y2});
    const int32_t sign = area > 0 ? 1 : -1;
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            const int32_t w0 = edge(x1, y1, x2, y2, x, y) * sign;
            const int32_t w1 = edge(x2, y2, x0, y0, x, y) * sign;
            const int32_t w2 = edge(x0, y0, x1, y1, x, y) * sign;
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            uint16_t pixel = color;
            if (textured) {
                const int32_t u = (w0 * u0 + w1 * u1 + w2 * u2) / (area * sign);
                const int32_t v = (w0 * v0 + w1 * v1 + w2 * v2) / (area * sign);
                pixel = sampleTexture(u, v, 0);
                if (pixel == 0) {
                    continue;
                }
            }
            drawPixel(x, y, pixel);
        }
    }
}

uint16_t Gpu::sampleTexture(int32_t u, int32_t v, uint32_t clut) const {
    const uint32_t texturePage = drawMode_ & 0x7FFu;
    const uint32_t pageX = (texturePage & 0xFu) * 64u;
    const uint32_t pageY = ((texturePage >> 4) & 1u) * 256u + ((texturePage >> 11) & 1u) * 512u;
    const uint32_t depth = (texturePage >> 7) & 3u;
    const uint32_t texX = pageX + (static_cast<uint32_t>(u) & 0xFFu);
    const uint32_t texY = pageY + (static_cast<uint32_t>(v) & 0xFFu);
    if (texX >= kVramWidth || texY >= kVramHeight) {
        return 0;
    }
    if (depth == 0) {
        const uint32_t word = vram(texX & ~3u, texY);
        const uint32_t paletteIndex = (word >> ((texX & 3u) * 4u)) & 0xFu;
        const uint32_t clutX = (clut & 0x3Fu) * 16u;
        const uint32_t clutY = (clut >> 6) & 0x1FFu;
        return vram(clutX + paletteIndex, clutY);
    }
    if (depth == 1) {
        const uint32_t word = vram(texX & ~1u, texY);
        const uint32_t paletteIndex = (word >> ((texX & 1u) * 8u)) & 0xFFu;
        const uint32_t clutX = (clut & 0x3Fu) * 16u;
        const uint32_t clutY = (clut >> 6) & 0x1FFu;
        return vram(clutX + paletteIndex, clutY);
    }
    return vram(texX, texY);
}

void Gpu::executeCopy() {
    const int32_t sourceX = static_cast<int32_t>(params_[1] & 0x3FFu);
    const int32_t sourceY = static_cast<int32_t>((params_[1] >> 16) & 0x1FFu);
    const int32_t targetX = static_cast<int32_t>(params_[2] & 0x3FFu);
    const int32_t targetY = static_cast<int32_t>((params_[2] >> 16) & 0x1FFu);
    const uint32_t width = (params_[3] & 0x3FFu) + 1u;
    const uint32_t height = ((params_[3] >> 16) & 0x1FFu) + 1u;
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
    transferWidth_ = params_[2] & 0x3FFu;
    transferHeight_ = (params_[2] >> 16) & 0x1FFu;
    transferWidth_ += 1u;
    transferHeight_ += 1u;
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
    transferWidth_ = (params_[2] & 0x3FFu) + 1u;
    transferHeight_ = ((params_[2] >> 16) & 0x1FFu) + 1u;
    transferIndex_ = 0;
    transferRemaining_ = transferWidth_ * transferHeight_;
}

void Gpu::executeGp1(uint32_t word) {
    const uint32_t opcode = word >> 24;
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
        displayMode_ = word & 0x3Fu;
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
