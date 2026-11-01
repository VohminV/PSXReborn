#include "psxreborn/overlay.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace psxreborn {

namespace {

using Glyph = std::array<uint8_t, 5>;

Glyph glyphFor(char character) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(character)))) {
        case 'A': return {0b010, 0b101, 0b111, 0b101, 0b101};
        case 'B': return {0b110, 0b101, 0b110, 0b101, 0b110};
        case 'C': return {0b011, 0b100, 0b100, 0b100, 0b011};
        case 'D': return {0b110, 0b101, 0b101, 0b101, 0b110};
        case 'E': return {0b111, 0b100, 0b110, 0b100, 0b111};
        case 'F': return {0b111, 0b100, 0b110, 0b100, 0b100};
        case 'G': return {0b011, 0b100, 0b101, 0b101, 0b011};
        case 'H': return {0b101, 0b101, 0b111, 0b101, 0b101};
        case 'I': return {0b111, 0b010, 0b010, 0b010, 0b111};
        case 'J': return {0b001, 0b001, 0b001, 0b101, 0b010};
        case 'K': return {0b101, 0b101, 0b110, 0b101, 0b101};
        case 'L': return {0b100, 0b100, 0b100, 0b100, 0b111};
        case 'M': return {0b101, 0b111, 0b111, 0b101, 0b101};
        case 'N': return {0b101, 0b111, 0b101, 0b101, 0b101};
        case 'O': return {0b010, 0b101, 0b101, 0b101, 0b010};
        case 'P': return {0b110, 0b101, 0b110, 0b100, 0b100};
        case 'Q': return {0b010, 0b101, 0b101, 0b111, 0b011};
        case 'R': return {0b110, 0b101, 0b110, 0b101, 0b101};
        case 'S': return {0b011, 0b100, 0b010, 0b001, 0b110};
        case 'T': return {0b111, 0b010, 0b010, 0b010, 0b010};
        case 'U': return {0b101, 0b101, 0b101, 0b101, 0b111};
        case 'V': return {0b101, 0b101, 0b101, 0b101, 0b010};
        case 'W': return {0b101, 0b101, 0b111, 0b111, 0b101};
        case 'X': return {0b101, 0b101, 0b010, 0b101, 0b101};
        case 'Y': return {0b101, 0b101, 0b010, 0b010, 0b010};
        case 'Z': return {0b111, 0b001, 0b010, 0b100, 0b111};
        case '0': return {0b111, 0b101, 0b101, 0b101, 0b111};
        case '1': return {0b010, 0b110, 0b010, 0b010, 0b111};
        case '2': return {0b110, 0b001, 0b010, 0b100, 0b111};
        case '3': return {0b110, 0b001, 0b010, 0b001, 0b110};
        case '4': return {0b101, 0b101, 0b111, 0b001, 0b001};
        case '5': return {0b111, 0b100, 0b110, 0b001, 0b110};
        case '6': return {0b011, 0b100, 0b111, 0b101, 0b111};
        case '7': return {0b111, 0b001, 0b010, 0b010, 0b010};
        case '8': return {0b111, 0b101, 0b111, 0b101, 0b111};
        case '9': return {0b111, 0b101, 0b111, 0b001, 0b110};
        case '.': return {0b000, 0b000, 0b000, 0b000, 0b010};
        case ',': return {0b000, 0b000, 0b000, 0b010, 0b100};
        case ':': return {0b000, 0b010, 0b000, 0b010, 0b000};
        case ';': return {0b000, 0b010, 0b000, 0b010, 0b100};
        case '-': return {0b000, 0b000, 0b111, 0b000, 0b000};
        case '_': return {0b000, 0b000, 0b000, 0b000, 0b111};
        case '/': return {0b001, 0b001, 0b010, 0b100, 0b100};
        case '\\': return {0b100, 0b100, 0b010, 0b001, 0b001};
        case '(': return {0b001, 0b010, 0b010, 0b010, 0b001};
        case ')': return {0b100, 0b010, 0b010, 0b010, 0b100};
        case '[': return {0b011, 0b010, 0b010, 0b010, 0b011};
        case ']': return {0b110, 0b010, 0b010, 0b010, 0b110};
        case '+': return {0b000, 0b010, 0b111, 0b010, 0b000};
        case '*': return {0b000, 0b101, 0b010, 0b101, 0b000};
        case '=': return {0b000, 0b111, 0b000, 0b111, 0b000};
        case '<': return {0b001, 0b010, 0b100, 0b010, 0b001};
        case '>': return {0b100, 0b010, 0b001, 0b010, 0b100};
        case '%': return {0b101, 0b001, 0b010, 0b100, 0b101};
        case '#': return {0b101, 0b111, 0b101, 0b111, 0b101};
        case '!': return {0b010, 0b010, 0b010, 0b000, 0b010};
        case '?': return {0b110, 0b001, 0b010, 0b000, 0b010};
        case '\'': return {0b010, 0b010, 0b000, 0b000, 0b000};
        case '"': return {0b101, 0b101, 0b000, 0b000, 0b000};
        case '@': return {0b010, 0b101, 0b111, 0b100, 0b011};
        case '&': return {0b010, 0b101, 0b010, 0b101, 0b011};
        case '|': return {0b010, 0b010, 0b010, 0b010, 0b010};
        case '^': return {0b010, 0b101, 0b000, 0b000, 0b000};
        case '~': return {0b000, 0b101, 0b111, 0b101, 0b000};
        case ' ': return {0, 0, 0, 0, 0};
        default: return {0b110, 0b001, 0b010, 0b000, 0b010};
    }
}

std::string fitText(const OverlayCanvas& canvas, const std::string& text, int scale,
                    int availableWidth) {
    if (availableWidth <= 0) {
        return {};
    }
    if (canvas.textWidth(text, scale) <= availableWidth) {
        return text;
    }
    std::string result = text;
    while (!result.empty() &&
           canvas.textWidth(result + "..", scale) > availableWidth) {
        result.pop_back();
    }
    if (result.empty()) {
        return {};
    }
    return result + "..";
}

}  // namespace

void OverlayCanvas::resize(int newWidth, int newHeight) {
    if (newWidth < 0) {
        newWidth = 0;
    }
    if (newHeight < 0) {
        newHeight = 0;
    }
    if (width == newWidth && height == newHeight && pixels.size() ==
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return;
    }
    width = newWidth;
    height = newHeight;
    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
}

void OverlayCanvas::clear(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        pixels[index] = red;
        pixels[index + 1] = green;
        pixels[index + 2] = blue;
        pixels[index + 3] = alpha;
    }
}

void OverlayCanvas::fillRect(int x, int y, int w, int h, uint8_t red, uint8_t green,
                             uint8_t blue, uint8_t alpha) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(width, x + w);
    const int y1 = std::min(height, y + h);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const std::size_t offset =
                (static_cast<std::size_t>(py) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(px)) * 4u;
            if (alpha == 255 || pixels[offset + 3] == 0) {
                pixels[offset] = red;
                pixels[offset + 1] = green;
                pixels[offset + 2] = blue;
                pixels[offset + 3] = alpha;
                continue;
            }
            const uint32_t sourceAlpha = alpha;
            const uint32_t destinationAlpha = pixels[offset + 3];
            const uint32_t inverse = 255u - sourceAlpha;
            const uint32_t outputAlpha = sourceAlpha + destinationAlpha * inverse / 255u;
            if (outputAlpha == 0) {
                continue;
            }
            const auto blend = [&](uint8_t source, uint8_t destination) {
                return static_cast<uint8_t>(
                    (source * sourceAlpha + destination * destinationAlpha * inverse / 255u) /
                    outputAlpha);
            };
            pixels[offset] = blend(red, pixels[offset]);
            pixels[offset + 1] = blend(green, pixels[offset + 1]);
            pixels[offset + 2] = blend(blue, pixels[offset + 2]);
            pixels[offset + 3] = static_cast<uint8_t>(outputAlpha);
        }
    }
}

int OverlayCanvas::textWidth(const std::string& text, int scale) const {
    if (text.empty() || scale <= 0) {
        return 0;
    }
    int longest = 0;
    int current = 0;
    for (char character : text) {
        if (character == '\n') {
            longest = std::max(longest, current);
            current = 0;
            continue;
        }
        current += 4;
    }
    longest = std::max(longest, current);
    return std::max(0, (longest - 1) * scale);
}

int OverlayCanvas::lineHeight(int scale) const {
    return 7 * std::max(1, scale);
}

void OverlayCanvas::drawText(int x, int y, const std::string& text, int scale, uint8_t red,
                             uint8_t green, uint8_t blue, uint8_t alpha) {
    if (scale <= 0) {
        return;
    }
    int cursorX = x;
    int cursorY = y;
    const int advance = 4 * scale;
    for (char character : text) {
        if (character == '\n') {
            cursorX = x;
            cursorY += lineHeight(scale);
            continue;
        }
        const Glyph glyph = glyphFor(character);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 3; ++column) {
                if ((glyph[static_cast<std::size_t>(row)] &
                     (1u << static_cast<unsigned>(2 - column))) == 0) {
                    continue;
                }
                fillRect(cursorX + column * scale, cursorY + row * scale, scale, scale,
                         red, green, blue, alpha);
            }
        }
        cursorX += advance;
    }
}

void renderMenuOverlay(OverlayCanvas& canvas, const Menu& menu, const MenuSettings& settings,
                       const MenuStatus& status) {
    if (canvas.width <= 0 || canvas.height <= 0) {
        return;
    }
    canvas.clear(0, 0, 0, 0);
    const int scale = std::clamp(std::min(canvas.width / 420, canvas.height / 300), 2, 8);
    const int margin = 2 * scale;
    const int panelX = margin;
    const int panelY = margin;
    const int panelW = canvas.width - margin * 2;
    const int panelH = canvas.height - margin * 2;
    canvas.fillRect(panelX, panelY, panelW, panelH, 13, 16, 22, 236);

    const int headerH = 9 * scale;
    canvas.fillRect(panelX, panelY, panelW, headerH, 28, 42, 62, 255);
    canvas.drawText(panelX + 2 * scale, panelY + 2 * scale, "PSXREBORN HOME", scale, 235,
                    240, 250, 255);
    const std::string rightTitle = status.gameId.empty() ? "NO GAME" : status.gameId;
    const int rightTitleWidth = canvas.textWidth(rightTitle, scale);
    canvas.drawText(panelX + panelW - rightTitleWidth - 2 * scale, panelY + 2 * scale,
                    rightTitle, scale, 170, 200, 230, 255);

    const int leftW = std::max(150, panelW / 4);
    const int contentX = panelX + leftW + 2 * scale;
    const int contentW = panelW - leftW - 4 * scale;
    const int contentRight = contentX + contentW;

    int tabY = panelY + headerH + 2 * scale;
    const int tabStep = canvas.lineHeight(scale) + 2 * scale;
    for (std::size_t index = 0; index < kMenuTabCount; ++index) {
        const bool selected = static_cast<std::size_t>(menu.tab()) == index;
        if (selected) {
            canvas.fillRect(panelX, tabY - scale, leftW, tabStep, 46, 110, 176, 255);
        }
        canvas.drawText(panelX + 2 * scale, tabY,
                        menuTabName(static_cast<MenuTab>(index)), scale,
                        selected ? 245 : 175, selected ? 250 : 185, selected ? 255 : 200, 255);
        tabY += tabStep;
    }

    const std::vector<MenuItem> list = menu.items(settings, status);
    const int footerH = 8 * scale;
    const int messageH = status.message.empty() ? 0 : 8 * scale;
    const int itemsTop = panelY + headerH + 2 * scale;
    const int itemsBottom = panelY + panelH - footerH - messageH - scale;
    const int rowStep = canvas.lineHeight(scale) + scale;
    const int visibleRows = std::max(1, (itemsBottom - itemsTop) / rowStep);
    int firstRow = 0;
    if (static_cast<int>(menu.selected()) >= visibleRows) {
        firstRow = static_cast<int>(menu.selected()) - visibleRows + 1;
    }
    if (firstRow < 0) {
        firstRow = 0;
    }
    for (int row = 0; row < visibleRows; ++row) {
        const int index = firstRow + row;
        if (index >= static_cast<int>(list.size())) {
            break;
        }
        const MenuItem& item = list[static_cast<std::size_t>(index)];
        const int y = itemsTop + row * rowStep;
        const bool selected = static_cast<std::size_t>(index) == menu.selected();
        if (selected) {
            canvas.fillRect(contentX - scale, y - scale, contentW + 2 * scale, rowStep,
                            36, 76, 118, 255);
        }
        const uint8_t labelR = item.enabled ? (selected ? 245 : 215) : 120;
        const uint8_t labelG = item.enabled ? (selected ? 250 : 220) : 125;
        const uint8_t labelB = item.enabled ? (selected ? 255 : 230) : 135;
        const int labelX = contentX + 2 * scale;
        const int valueReserve = contentW / 2;
        if (selected) {
            canvas.drawText(contentX, y, ">", scale, 120, 220, 255, 255);
        }
        canvas.drawText(labelX, y,
                        fitText(canvas, item.label, scale, contentW - valueReserve - 2 * scale),
                        scale, labelR, labelG, labelB, 255);
        const int valueWidth = canvas.textWidth(item.value, scale);
        const int valueX = std::max(contentX + contentW / 2,
                                    contentRight - valueWidth);
        canvas.drawText(valueX, y,
                        fitText(canvas, item.value, scale, contentRight - valueX), scale,
                        selected ? 255 : 170, selected ? 220 : 190, selected ? 140 : 205, 255);
    }

    if (!status.message.empty()) {
        const int messageY = itemsBottom + scale;
        canvas.fillRect(contentX - scale, messageY - scale, contentW + 2 * scale, messageH,
                        70, 54, 20, 255);
        canvas.drawText(contentX, messageY,
                        fitText(canvas, status.message, scale, contentW), scale, 255, 220, 150,
                        255);
    }

    const int footerY = panelY + panelH - footerH + 2 * scale;
    canvas.fillRect(panelX, panelY + panelH - footerH, panelW, footerH, 22, 27, 35, 255);
    canvas.drawText(panelX + 2 * scale, footerY,
                    "UP/DOWN MOVE  LEFT/RIGHT CHANGE  ENTER ACTIVATE  TAB/Q/E TAB  HOME/ESC CLOSE",
                    scale, 150, 160, 175, 255);
}

}  // namespace psxreborn
