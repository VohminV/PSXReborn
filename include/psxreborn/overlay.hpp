#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "psxreborn/menu.hpp"

namespace psxreborn {

struct OverlayCanvas {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    void resize(int newWidth, int newHeight);
    void clear(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
    void fillRect(int x, int y, int w, int h, uint8_t red, uint8_t green, uint8_t blue,
                  uint8_t alpha);
    void drawText(int x, int y, const std::string& text, int scale, uint8_t red, uint8_t green,
                  uint8_t blue, uint8_t alpha);
    int textWidth(const std::string& text, int scale) const;
    int lineHeight(int scale) const;
};

void renderMenuOverlay(OverlayCanvas& canvas, const Menu& menu, const MenuSettings& settings,
                       const MenuStatus& status);

}  // namespace psxreborn
