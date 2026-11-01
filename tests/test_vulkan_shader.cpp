#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "psxreborn/vulkan_shader.hpp"

#include "test_framework.hpp"

using namespace psxreborn;

namespace {

const ShaderId kAllShaders[] = {
    ShaderId::PresentVertex,
    ShaderId::PresentFragment,
    ShaderId::Easu,
    ShaderId::Rcas,
};

}  // namespace

TEST_CASE(vulkan_embedded_shaders_are_valid_spirv) {
    for (ShaderId id : kAllShaders) {
        const SpirvBlob blob = embeddedShader(id);
        std::string error;
        CHECK(validateSpirv(blob, &error));
        CHECK(error.empty());
        CHECK_EQ(blob.code[0], 0x07230203u);
        CHECK((blob.sizeBytes % 4u) == 0u);
        CHECK(blob.sizeBytes >= 20u);
        CHECK(shaderIdName(id) != std::string("unknown"));
    }
}

TEST_CASE(vulkan_spirv_validation_rejects_bad_modules) {
    const std::array<uint32_t, 5> valid = {0x07230203u, 0x00010300u, 0, 1, 0};
    std::string error;

    CHECK(!validateSpirv({nullptr, sizeof(valid)}, &error));
    CHECK(!error.empty());

    error.clear();
    CHECK(!validateSpirv({valid.data(), 0}, &error));
    CHECK(!error.empty());

    error.clear();
    CHECK(!validateSpirv({valid.data(), 18}, &error));
    CHECK(!error.empty());

    error.clear();
    CHECK(!validateSpirv({valid.data(), 12}, &error));
    CHECK(!error.empty());

    error.clear();
    const std::array<uint32_t, 5> badMagic = {0xDEADBEEFu, 0, 0, 0, 0};
    CHECK(!validateSpirv({badMagic.data(), sizeof(badMagic)}, &error));
    CHECK(!error.empty());

    CHECK(validateSpirv({valid.data(), sizeof(valid)}, nullptr));
}

#if defined(PSXREBORN_HAS_VULKAN)
TEST_CASE(vulkan_present_specialization_layout) {
    PresentSpecializationInfo defaults;
    const VkSpecializationInfo* defaultInfo = defaults.info();
    CHECK_EQ(defaultInfo->mapEntryCount, 2u);
    CHECK_EQ(defaultInfo->dataSize, sizeof(VkBool32) * 2u);
    CHECK_EQ(defaultInfo->pMapEntries[0].constantID, 0u);
    CHECK_EQ(defaultInfo->pMapEntries[0].size, sizeof(VkBool32));
    CHECK_EQ(defaultInfo->pMapEntries[1].constantID, 1u);
    CHECK_EQ(defaultInfo->pMapEntries[1].size, sizeof(VkBool32));
    CHECK(!defaults.is4k());
    CHECK(defaults.overlayEnabled());

    PresentSpecializationInfo fourK(true, false);
    CHECK(fourK.is4k());
    CHECK(!fourK.overlayEnabled());
}

TEST_CASE(vulkan_shader_modules_create_from_embedded_spirv) {
    if (std::getenv("PSXREBORN_SKIP_VULKAN_TESTS") != nullptr) {
        std::fprintf(stderr, "[SKIP] Vulkan tests disabled by environment\n");
        return;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "PSXReborn shader test";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        std::fprintf(stderr, "[SKIP] Vulkan loader/instance unavailable\n");
        return;
    }

    uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS ||
        deviceCount == 0) {
        std::fprintf(stderr, "[SKIP] no Vulkan physical devices\n");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    std::array<VkPhysicalDevice, 8> devices{};
    if (deviceCount > devices.size()) {
        deviceCount = static_cast<uint32_t>(devices.size());
    }
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "[SKIP] Vulkan device enumeration failed\n");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    for (uint32_t index = 0; index < deviceCount && physicalDevice == VK_NULL_HANDLE; ++index) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &familyCount, nullptr);
        if (familyCount == 0) {
            continue;
        }
        std::array<VkQueueFamilyProperties, 8> families{};
        if (familyCount > families.size()) {
            familyCount = static_cast<uint32_t>(families.size());
        }
        vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &familyCount, families.data());
        if (familyCount != 0) {
            physicalDevice = devices[index];
            queueFamily = 0;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[SKIP] no Vulkan queue family\n");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
        std::fprintf(stderr, "[SKIP] Vulkan logical device creation failed\n");
        vkDestroyInstance(instance, nullptr);
        return;
    }

    for (ShaderId id : kAllShaders) {
        std::string error;
        const VkShaderModule module = createShaderModule(device, id, &error);
        if (module == VK_NULL_HANDLE) {
            std::fprintf(stderr, "[FAIL] %s: %s\n", shaderIdName(id), error.c_str());
        }
        CHECK(module != VK_NULL_HANDLE);
        vkDestroyShaderModule(device, module, nullptr);
    }

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
#endif
