#pragma once

#include <cstdint>
#include <memory>
#include <string>

#if defined(PSXREBORN_HAS_VULKAN)
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

namespace psxreborn {

class VulkanPresenter {
public:
    VulkanPresenter();
    ~VulkanPresenter();

    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;
    VulkanPresenter(VulkanPresenter&&) = delete;
    VulkanPresenter& operator=(VulkanPresenter&&) = delete;

    bool initialize(SDL_Window* window, bool vsync, bool nearestFilter,
                    std::string* error = nullptr);
    bool active() const;
    int width() const;
    int height() const;
    const std::string& deviceName() const;

    void setVsync(bool vsync);
    void setFilter(bool nearestFilter);

    bool present(const uint16_t* vram, uint32_t srcX, uint32_t srcY, uint32_t srcW,
                 uint32_t srcH, bool displayEnabled, bool overlayEnabled,
                 const uint8_t* overlayPixels, int overlayWidth, int overlayHeight,
                 bool integerScale, bool fourByThree, std::string* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace psxreborn

#endif
