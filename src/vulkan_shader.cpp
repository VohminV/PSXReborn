#include "psxreborn/vulkan_shader.hpp"

#include <utility>

#include "spirv_data.h"

namespace psxreborn {

namespace {

constexpr uint32_t kSpirvMagic = 0x07230203u;
constexpr uint32_t kSpirvHeaderBytes = 5u * sizeof(uint32_t);

void setError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

}  // namespace

const char* shaderIdName(ShaderId id) noexcept {
    switch (id) {
        case ShaderId::PresentVertex:
            return "present.vert";
        case ShaderId::PresentFragment:
            return "present.frag";
        case ShaderId::Easu:
            return "easu.comp";
        case ShaderId::Rcas:
            return "rcas.comp";
        default:
            return "unknown";
    }
}

SpirvBlob embeddedShader(ShaderId id) noexcept {
    switch (id) {
        case ShaderId::PresentVertex:
            return {embedded_shaders::kPresentVertSpirv.data(),
                    embedded_shaders::kPresentVertSpirvSizeBytes};
        case ShaderId::PresentFragment:
            return {embedded_shaders::kPresentFragSpirv.data(),
                    embedded_shaders::kPresentFragSpirvSizeBytes};
        case ShaderId::Easu:
            return {embedded_shaders::kEasuCompSpirv.data(),
                    embedded_shaders::kEasuCompSpirvSizeBytes};
        case ShaderId::Rcas:
            return {embedded_shaders::kRcasCompSpirv.data(),
                    embedded_shaders::kRcasCompSpirvSizeBytes};
        default:
            return {};
    }
}

bool validateSpirv(const SpirvBlob& blob, std::string* error) {
    if (blob.code == nullptr) {
        setError(error, "SPIR-V code pointer is null");
        return false;
    }
    if (blob.sizeBytes == 0) {
        setError(error, "SPIR-V module is empty");
        return false;
    }
    if ((blob.sizeBytes % sizeof(uint32_t)) != 0) {
        setError(error, "SPIR-V codeSize is not a multiple of four bytes");
        return false;
    }
    if (blob.sizeBytes < kSpirvHeaderBytes) {
        setError(error, "SPIR-V module is smaller than the five-word header");
        return false;
    }
    if (blob.code[0] != kSpirvMagic) {
        setError(error, "SPIR-V magic word is invalid");
        return false;
    }
    return true;
}

#if defined(PSXREBORN_HAS_VULKAN)

PresentSpecializationInfo::PresentSpecializationInfo(bool is4k, bool enableOverlay) {
    values_.is4k = is4k ? VK_TRUE : VK_FALSE;
    values_.enableOverlay = enableOverlay ? VK_TRUE : VK_FALSE;

    entries_[0] = VkSpecializationMapEntry{
        0u, static_cast<uint32_t>(offsetof(Values, is4k)), sizeof(VkBool32)};
    entries_[1] = VkSpecializationMapEntry{
        1u, static_cast<uint32_t>(offsetof(Values, enableOverlay)), sizeof(VkBool32)};

    info_.mapEntryCount = static_cast<uint32_t>(entries_.size());
    info_.pMapEntries = entries_.data();
    info_.dataSize = sizeof(Values);
    info_.pData = &values_;
}

VkShaderModule createShaderModule(VkDevice device, const SpirvBlob& blob,
                                  std::string* error) {
    if (device == VK_NULL_HANDLE) {
        setError(error, "VkDevice is null");
        return VK_NULL_HANDLE;
    }
    if (!validateSpirv(blob, error)) {
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = blob.sizeBytes;
    info.pCode = blob.code;

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device, &info, nullptr, &module);
    if (result != VK_SUCCESS) {
        setError(error, "vkCreateShaderModule failed with VkResult " +
                            std::to_string(static_cast<int>(result)));
        return VK_NULL_HANDLE;
    }
    return module;
}

VkShaderModule createShaderModule(VkDevice device, ShaderId id, std::string* error) {
    return createShaderModule(device, embeddedShader(id), error);
}

#endif

}  // namespace psxreborn
