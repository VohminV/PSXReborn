#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#if defined(PSXREBORN_HAS_VULKAN)
#include <vulkan/vulkan.h>
#endif

namespace psxreborn {

enum class ShaderId {
    PresentVertex,
    PresentFragment,
    Easu,
    Rcas,
};

struct SpirvBlob {
    const uint32_t* code = nullptr;
    uint32_t sizeBytes = 0;
};

const char* shaderIdName(ShaderId id) noexcept;
SpirvBlob embeddedShader(ShaderId id) noexcept;

bool validateSpirv(const SpirvBlob& blob, std::string* error = nullptr);

#if defined(PSXREBORN_HAS_VULKAN)
class PresentSpecializationInfo {
public:
    explicit PresentSpecializationInfo(bool is4k = false, bool enableOverlay = true);

    PresentSpecializationInfo(const PresentSpecializationInfo&) = delete;
    PresentSpecializationInfo& operator=(const PresentSpecializationInfo&) = delete;
    PresentSpecializationInfo(PresentSpecializationInfo&&) = delete;
    PresentSpecializationInfo& operator=(PresentSpecializationInfo&&) = delete;

    const VkSpecializationInfo* info() const noexcept { return &info_; }
    bool is4k() const noexcept { return values_.is4k != VK_FALSE; }
    bool overlayEnabled() const noexcept { return values_.enableOverlay != VK_FALSE; }

private:
    struct Values {
        VkBool32 is4k;
        VkBool32 enableOverlay;
    };

    Values values_{};
    std::array<VkSpecializationMapEntry, 2> entries_{};
    VkSpecializationInfo info_{};
};

VkShaderModule createShaderModule(VkDevice device, const SpirvBlob& blob,
                                  std::string* error = nullptr);
VkShaderModule createShaderModule(VkDevice device, ShaderId id,
                                  std::string* error = nullptr);
#endif

}  // namespace psxreborn
