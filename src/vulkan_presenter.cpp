#include "psxreborn/vulkan_presenter.hpp"

#if defined(PSXREBORN_HAS_VULKAN)

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "psxreborn/vulkan_shader.hpp"

namespace psxreborn {

namespace {

constexpr uint32_t kVramWidth = 1024;
constexpr uint32_t kVramHeight = 512;

struct PresentPushConstants {
    float sourceRect[4];
    float destinationRect[4];
    uint32_t flags[4];
};

static_assert(sizeof(PresentPushConstants) == 48, "push constant layout mismatch");

bool fail(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
    return false;
}

std::string vkResultText(const char* operation, VkResult result) {
    return std::string(operation) + " failed with VkResult " +
           std::to_string(static_cast<int>(result));
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits,
                        VkMemoryPropertyFlags properties, bool* found) {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
    for (uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((typeBits & (1u << index)) != 0 &&
            (memory.memoryTypes[index].propertyFlags & properties) == properties) {
            if (found != nullptr) {
                *found = true;
            }
            return index;
        }
    }
    if (found != nullptr) {
        *found = false;
    }
    return 0;
}

bool createBuffer(VkPhysicalDevice physicalDevice, VkDevice device, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer,
                  VkDeviceMemory& memory, void** mapped, std::string* error) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return fail(error, "vkCreateBuffer failed");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    bool found = false;
    const uint32_t memoryType =
        findMemoryType(physicalDevice, requirements.memoryTypeBits, properties, &found);
    if (!found) {
        return fail(error, "no suitable memory type for buffer");
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        return fail(error, "vkAllocateMemory failed for buffer");
    }
    if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
        return fail(error, "vkBindBufferMemory failed");
    }
    if (mapped != nullptr) {
        if (vkMapMemory(device, memory, 0, size, 0, mapped) != VK_SUCCESS) {
            return fail(error, "vkMapMemory failed");
        }
    }
    return true;
}

bool createImage(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t width,
                 uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImage& image,
                 VkDeviceMemory& memory, std::string* error) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        return fail(error, "vkCreateImage failed");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    bool found = false;
    const uint32_t memoryType = findMemoryType(
        physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &found);
    if (!found) {
        return fail(error, "no suitable memory type for image");
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        return fail(error, "vkAllocateMemory failed for image");
    }
    if (vkBindImageMemory(device, image, memory, 0) != VK_SUCCESS) {
        return fail(error, "vkBindImageMemory failed");
    }
    return true;
}

bool createImageView(VkDevice device, VkImage image, VkFormat format, VkImageView& view,
                     std::string* error) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        return fail(error, "vkCreateImageView failed");
    }
    return true;
}

void imageBarrier(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout,
                  VkImageLayout newLayout, VkPipelineStageFlags sourceStage,
                  VkAccessFlags sourceAccess, VkPipelineStageFlags destinationStage,
                  VkAccessFlags destinationAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

uint32_t color555To888(uint16_t pixel) {
    const uint8_t red = static_cast<uint8_t>((pixel & 0x1Fu) << 3);
    const uint8_t green = static_cast<uint8_t>(((pixel >> 5) & 0x1Fu) << 3);
    const uint8_t blue = static_cast<uint8_t>(((pixel >> 10) & 0x1Fu) << 3);
    return 0xFF000000u | (static_cast<uint32_t>(blue) << 16) |
           (static_cast<uint32_t>(green) << 8) | red;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
            format.format == VK_FORMAT_B8G8R8A8_UNORM) {
            return format;
        }
    }
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                           : formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (!vsync) {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    VkExtent2D extent{};
    extent.width = static_cast<uint32_t>(std::max(width, 1));
    extent.height = static_cast<uint32_t>(std::max(height, 1));
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
    return extent;
}

}  // namespace

struct VulkanPresenter::Impl {
    SDL_Window* window = nullptr;
    bool vsync = true;
    bool nearest = false;
    bool swapchainVsync = true;
    bool descriptorsDirty = true;
    bool overlayUploaded = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    std::string deviceName;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> framebuffers;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;

    VkImage vramImage = VK_NULL_HANDLE;
    VkDeviceMemory vramMemory = VK_NULL_HANDLE;
    VkImageView vramView = VK_NULL_HANDLE;
    VkBuffer vramStaging = VK_NULL_HANDLE;
    VkDeviceMemory vramStagingMemory = VK_NULL_HANDLE;
    void* vramStagingMapped = nullptr;
    VkImageLayout vramLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage overlayImage = VK_NULL_HANDLE;
    VkDeviceMemory overlayMemory = VK_NULL_HANDLE;
    VkImageView overlayView = VK_NULL_HANDLE;
    VkBuffer overlayStaging = VK_NULL_HANDLE;
    VkDeviceMemory overlayStagingMemory = VK_NULL_HANDLE;
    void* overlayStagingMapped = nullptr;
    VkImageLayout overlayLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t overlayWidth = 0;
    uint32_t overlayHeight = 0;

    VkSampler samplerLinear = VK_NULL_HANDLE;
    VkSampler samplerNearest = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;

    bool active() const { return device != VK_NULL_HANDLE && swapchain != VK_NULL_HANDLE; }

    bool initialize(SDL_Window* sdlWindow, bool enableVsync, bool nearestFilter,
                    std::string* error) {
        window = sdlWindow;
        vsync = enableVsync;
        nearest = nearestFilter;

        if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
            return fail(error, std::string("SDL_Vulkan_LoadLibrary failed: ") + SDL_GetError());
        }

        unsigned extensionCount = 0;
        if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr)) {
            return fail(error, std::string("SDL_Vulkan_GetInstanceExtensions failed: ") +
                                    SDL_GetError());
        }
        std::vector<const char*> extensions(extensionCount);
        if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data())) {
            return fail(error, std::string("SDL_Vulkan_GetInstanceExtensions failed: ") +
                                    SDL_GetError());
        }

        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "PSXReborn";
        application.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        application.pEngineName = "PSXReborn";
        application.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        application.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &application;
        instanceInfo.enabledExtensionCount = extensionCount;
        instanceInfo.ppEnabledExtensionNames = extensions.data();
        const VkResult instanceResult = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if (instanceResult != VK_SUCCESS) {
            return fail(error, vkResultText("vkCreateInstance", instanceResult));
        }

        if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
            return fail(error, std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }

        if (!pickPhysicalDevice(error)) {
            return false;
        }
        if (!createDevice(error)) {
            return false;
        }
        if (!createSwapchain(error)) {
            return false;
        }
        if (!createRenderPass(error)) {
            return false;
        }
        if (!createDescriptorLayout(error)) {
            return false;
        }
        if (!createShaderModules(error)) {
            return false;
        }
        if (!createPipeline(error)) {
            return false;
        }
        if (!createFramebuffers(error)) {
            return false;
        }
        if (!createTextures(error)) {
            return false;
        }
        if (!createSamplers(error)) {
            return false;
        }
        if (!createDescriptors(error)) {
            return false;
        }
        if (!createCommandsAndSync(error)) {
            return false;
        }
        return true;
    }

    bool pickPhysicalDevice(std::string* error) {
        uint32_t count = 0;
        if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS || count == 0) {
            return fail(error, "no Vulkan physical devices");
        }
        std::vector<VkPhysicalDevice> devices(count);
        if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) != VK_SUCCESS) {
            return fail(error, "vkEnumeratePhysicalDevices failed");
        }

        int bestScore = -1;
        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);

            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

            int graphics = -1;
            int present = -1;
            for (uint32_t index = 0; index < familyCount; ++index) {
                if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && graphics < 0) {
                    graphics = static_cast<int>(index);
                }
                VkBool32 supported = VK_FALSE;
                if (vkGetPhysicalDeviceSurfaceSupportKHR(candidate, index, surface, &supported) ==
                        VK_SUCCESS &&
                    supported == VK_TRUE && present < 0) {
                    present = static_cast<int>(index);
                }
            }
            if (graphics < 0 || present < 0) {
                continue;
            }

            uint32_t extensionCount = 0;
            vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount,
                                                 deviceExtensions.data());
            bool swapchainSupported = false;
            for (const VkExtensionProperties& extension : deviceExtensions) {
                if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                    swapchainSupported = true;
                    break;
                }
            }
            if (!swapchainSupported) {
                continue;
            }

            int score = 0;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                score = 3;
            } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                score = 2;
            } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) {
                score = 1;
            }
            if (score > bestScore) {
                bestScore = score;
                physicalDevice = candidate;
                graphicsFamily = static_cast<uint32_t>(graphics);
                presentFamily = static_cast<uint32_t>(present);
                deviceName = properties.deviceName;
            }
        }

        if (physicalDevice == VK_NULL_HANDLE) {
            return fail(error, "no Vulkan device with graphics, present, and swapchain support");
        }
        return true;
    }

    bool createDevice(std::string* error) {
        const float priority = 1.0f;
        std::array<uint32_t, 2> families{graphicsFamily, presentFamily};
        uint32_t uniqueCount = 0;
        for (uint32_t family : families) {
            bool seen = false;
            for (uint32_t index = 0; index < uniqueCount; ++index) {
                seen = seen || families[index] == family;
            }
            if (!seen) {
                families[uniqueCount++] = family;
            }
        }

        std::array<VkDeviceQueueCreateInfo, 2> queueInfos{};
        for (uint32_t index = 0; index < uniqueCount; ++index) {
            queueInfos[index].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfos[index].queueFamilyIndex = families[index];
            queueInfos[index].queueCount = 1;
            queueInfos[index].pQueuePriorities = &priority;
        }

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = uniqueCount;
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = extensions;
        const VkResult result = vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
        if (result != VK_SUCCESS) {
            return fail(error, vkResultText("vkCreateDevice", result));
        }
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamily, 0, &presentQueue);
        return true;
    }

    bool createSwapchain(std::string* error) {
        VkSurfaceCapabilitiesKHR capabilities{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities) !=
            VK_SUCCESS) {
            return fail(error, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        }

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());

        const VkSurfaceFormatKHR format = chooseSurfaceFormat(formats);
        const VkPresentModeKHR presentMode = choosePresentMode(modes, vsync);
        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        const VkExtent2D newExtent = chooseExtent(capabilities, drawableWidth, drawableHeight);

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
            imageCount = capabilities.maxImageCount;
        }

        std::array<uint32_t, 2> familyIndices{graphicsFamily, presentFamily};
        const bool exclusive = graphicsFamily == presentFamily;

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = format.format;
        swapchainInfo.imageColorSpace = format.colorSpace;
        swapchainInfo.imageExtent = newExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = exclusive ? VK_SHARING_MODE_EXCLUSIVE
                                                   : VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = exclusive ? 0u : 2u;
        swapchainInfo.pQueueFamilyIndices = exclusive ? nullptr : familyIndices.data();
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = presentMode;
        swapchainInfo.clipped = VK_TRUE;

        const VkResult result =
            vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain);
        if (result != VK_SUCCESS) {
            return fail(error, vkResultText("vkCreateSwapchainKHR", result));
        }

        swapchainFormat = format.format;
        extent = newExtent;
        swapchainVsync = vsync;

        if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr) != VK_SUCCESS) {
            return fail(error, "vkGetSwapchainImagesKHR failed");
        }
        swapchainImages.resize(imageCount);
        if (vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()) !=
            VK_SUCCESS) {
            return fail(error, "vkGetSwapchainImagesKHR failed");
        }

        swapchainViews.resize(swapchainImages.size(), VK_NULL_HANDLE);
        for (std::size_t index = 0; index < swapchainImages.size(); ++index) {
            if (!createImageView(device, swapchainImages[index], swapchainFormat,
                                 swapchainViews[index], error)) {
                return false;
            }
        }
        return true;
    }

    bool createRenderPass(std::string* error) {
        VkAttachmentDescription color{};
        color.format = swapchainFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &color;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        const VkResult result =
            vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
        if (result != VK_SUCCESS) {
            return fail(error, vkResultText("vkCreateRenderPass", result));
        }
        return true;
    }

    bool createDescriptorLayout(std::string* error) {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        for (uint32_t index = 0; index < bindings.size(); ++index) {
            bindings[index].binding = index;
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        const VkResult result =
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorLayout);
        if (result != VK_SUCCESS) {
            return fail(error, vkResultText("vkCreateDescriptorSetLayout", result));
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(PresentPushConstants);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
            VK_SUCCESS) {
            return fail(error, "vkCreatePipelineLayout failed");
        }
        return true;
    }

    bool createShaderModules(std::string* error) {
        vertexModule = createShaderModule(device, ShaderId::PresentVertex, error);
        if (vertexModule == VK_NULL_HANDLE) {
            return false;
        }
        fragmentModule = createShaderModule(device, ShaderId::PresentFragment, error);
        return fragmentModule != VK_NULL_HANDLE;
    }

    bool createPipeline(std::string* error) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";

        PresentSpecializationInfo specialization(extent.width >= 3840u || extent.height >= 2160u,
                                                 true);
        stages[1].pSpecializationInfo = specialization.info();

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.depthClampEnable = VK_FALSE;
        raster.rasterizerDiscardEnable = VK_FALSE;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.depthBiasEnable = VK_FALSE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisample.sampleShadingEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = VK_FALSE;
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.logicOpEnable = VK_FALSE;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        const VkResult result =
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &pipeline);
        if (result != VK_SUCCESS) {
            return fail(error, vkResultText("vkCreateGraphicsPipelines", result));
        }
        return true;
    }

    bool createFramebuffers(std::string* error) {
        framebuffers.resize(swapchainViews.size(), VK_NULL_HANDLE);
        for (std::size_t index = 0; index < swapchainViews.size(); ++index) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &swapchainViews[index];
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;
            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[index]) !=
                VK_SUCCESS) {
                return fail(error, "vkCreateFramebuffer failed");
            }
        }
        return true;
    }

    bool createTextures(std::string* error) {
        if (!createImage(physicalDevice, device, kVramWidth, kVramHeight,
                         VK_FORMAT_B8G8R8A8_UNORM,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         vramImage, vramMemory, error)) {
            return false;
        }
        if (!createImageView(device, vramImage, VK_FORMAT_B8G8R8A8_UNORM, vramView, error)) {
            return false;
        }
        if (!createBuffer(physicalDevice, device, kVramWidth * kVramHeight * sizeof(uint32_t),
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vramStaging, vramStagingMemory, &vramStagingMapped, error)) {
            return false;
        }
        std::memset(vramStagingMapped, 0, kVramWidth * kVramHeight * sizeof(uint32_t));
        return createOverlay(extent.width, extent.height, error);
    }

    bool createOverlay(uint32_t width, uint32_t height, std::string* error) {
        if (width == 0 || height == 0) {
            return true;
        }
        if (!createImage(physicalDevice, device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         overlayImage, overlayMemory, error)) {
            return false;
        }
        if (!createImageView(device, overlayImage, VK_FORMAT_R8G8B8A8_UNORM, overlayView, error)) {
            return false;
        }
        if (!createBuffer(physicalDevice, device,
                          static_cast<VkDeviceSize>(width) * height * sizeof(uint32_t),
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          overlayStaging, overlayStagingMemory, &overlayStagingMapped, error)) {
            return false;
        }
        std::memset(overlayStagingMapped, 0,
                    static_cast<std::size_t>(width) * height * sizeof(uint32_t));
        overlayWidth = width;
        overlayHeight = height;
        overlayLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        overlayUploaded = false;
        descriptorsDirty = true;
        return true;
    }

    void destroyOverlay() {
        if (overlayStagingMapped != nullptr) {
            vkUnmapMemory(device, overlayStagingMemory);
            overlayStagingMapped = nullptr;
        }
        if (overlayStaging != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, overlayStaging, nullptr);
            overlayStaging = VK_NULL_HANDLE;
        }
        if (overlayStagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, overlayStagingMemory, nullptr);
            overlayStagingMemory = VK_NULL_HANDLE;
        }
        if (overlayView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, overlayView, nullptr);
            overlayView = VK_NULL_HANDLE;
        }
        if (overlayImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, overlayImage, nullptr);
            overlayImage = VK_NULL_HANDLE;
        }
        if (overlayMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, overlayMemory, nullptr);
            overlayMemory = VK_NULL_HANDLE;
        }
        overlayWidth = 0;
        overlayHeight = 0;
        overlayLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        overlayUploaded = false;
    }

    bool ensureOverlay(uint32_t width, uint32_t height, std::string* error) {
        if (width == overlayWidth && height == overlayHeight) {
            return true;
        }
        destroyOverlay();
        return createOverlay(width, height, error);
    }

    bool createSamplers(std::string* error) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &samplerNearest) != VK_SUCCESS) {
            return fail(error, "vkCreateSampler failed for nearest filter");
        }

        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        if (vkCreateSampler(device, &samplerInfo, nullptr, &samplerLinear) != VK_SUCCESS) {
            return fail(error, "vkCreateSampler failed for linear filter");
        }
        return true;
    }

    bool createDescriptors(std::string* error) {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        size.descriptorCount = 2;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            return fail(error, "vkCreateDescriptorPool failed");
        }

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &descriptorLayout;
        if (vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet) != VK_SUCCESS) {
            return fail(error, "vkAllocateDescriptorSets failed");
        }
        updateDescriptors();
        return true;
    }

    void updateDescriptors() {
        if (descriptorSet == VK_NULL_HANDLE || vramView == VK_NULL_HANDLE ||
            overlayView == VK_NULL_HANDLE) {
            return;
        }
        VkDescriptorImageInfo gameInfo{};
        gameInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        gameInfo.imageView = vramView;
        gameInfo.sampler = nearest ? samplerNearest : samplerLinear;

        VkDescriptorImageInfo overlayInfo{};
        overlayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        overlayInfo.imageView = overlayView;
        overlayInfo.sampler = nearest ? samplerNearest : samplerLinear;

        VkWriteDescriptorSet writes[2]{};
        for (uint32_t index = 0; index < 2; ++index) {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptorSet;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[index].pImageInfo = index == 0 ? &gameInfo : &overlayInfo;
        }
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        descriptorsDirty = false;
    }

    bool createCommandsAndSync(std::string* error) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            return fail(error, "vkCreateCommandPool failed");
        }

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
            return fail(error, "vkAllocateCommandBuffers failed");
        }

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished) != VK_SUCCESS) {
            return fail(error, "vkCreateSemaphore failed");
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(device, &fenceInfo, nullptr, &inFlight) != VK_SUCCESS) {
            return fail(error, "vkCreateFence failed");
        }
        return true;
    }

    bool recreateSwapchain(std::string* error) {
        vkDeviceWaitIdle(device);
        destroySwapchainDependent();

        if (!createSwapchain(error)) {
            return false;
        }
        if (!createRenderPass(error)) {
            return false;
        }
        if (!createPipeline(error)) {
            return false;
        }
        if (!createFramebuffers(error)) {
            return false;
        }
        descriptorsDirty = true;
        return true;
    }

    void destroySwapchainDependent() {
        for (VkFramebuffer framebuffer : framebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        framebuffers.clear();
        for (VkImageView view : swapchainViews) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
            }
        }
        swapchainViews.clear();
        swapchainImages.clear();
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }

    void uploadImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout& layout,
                     VkBuffer staging, uint32_t width, uint32_t height) {
        const VkPipelineStageFlags sourceStage =
            layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                                : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        const VkAccessFlags sourceAccess =
            layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT;

        imageBarrier(commandBuffer, image, layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     sourceStage, sourceAccess, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(commandBuffer, staging, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        imageBarrier(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    bool present(const uint16_t* vram, uint32_t srcX, uint32_t srcY, uint32_t srcW,
                 uint32_t srcH, bool displayEnabled, bool overlayEnabled,
                 const uint8_t* overlayPixels, int newOverlayWidth, int newOverlayHeight,
                 bool integerScale, bool fourByThree, std::string* error) {
        if (!active()) {
            return fail(error, "Vulkan presenter is not initialized");
        }

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
        if (drawableWidth <= 0 || drawableHeight <= 0) {
            return true;
        }
        if (static_cast<uint32_t>(drawableWidth) != extent.width ||
            static_cast<uint32_t>(drawableHeight) != extent.height || vsync != swapchainVsync) {
            if (!recreateSwapchain(error)) {
                return false;
            }
        }

        const VkResult waitResult = vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
        if (waitResult != VK_SUCCESS) {
            return fail(error, vkResultText("vkWaitForFences", waitResult));
        }

        uint32_t imageIndex = 0;
        const VkResult acquireResult = vkAcquireNextImageKHR(
            device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            return recreateSwapchain(error);
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            return fail(error, vkResultText("vkAcquireNextImageKHR", acquireResult));
        }

        if (vkResetFences(device, 1, &inFlight) != VK_SUCCESS) {
            return fail(error, "vkResetFences failed");
        }

        if (overlayEnabled && overlayPixels != nullptr && newOverlayWidth > 0 &&
            newOverlayHeight > 0) {
            if (!ensureOverlay(static_cast<uint32_t>(newOverlayWidth),
                               static_cast<uint32_t>(newOverlayHeight), error)) {
                return false;
            }
        } else if (!overlayUploaded) {
            if (!ensureOverlay(extent.width, extent.height, error)) {
                return false;
            }
        }

        if (vram != nullptr && vramStagingMapped != nullptr) {
            uint32_t* destination = static_cast<uint32_t*>(vramStagingMapped);
            for (uint32_t index = 0; index < kVramWidth * kVramHeight; ++index) {
                destination[index] = color555To888(vram[index]);
            }
        }

        const bool drawOverlay = overlayEnabled && overlayPixels != nullptr &&
                                 newOverlayWidth > 0 && newOverlayHeight > 0;
        if (drawOverlay) {
            std::memcpy(overlayStagingMapped, overlayPixels,
                        static_cast<std::size_t>(newOverlayWidth) *
                            static_cast<std::size_t>(newOverlayHeight) * sizeof(uint32_t));
        } else if (!overlayUploaded) {
            std::memset(overlayStagingMapped, 0,
                        static_cast<std::size_t>(overlayWidth) * overlayHeight *
                            sizeof(uint32_t));
        }

        if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS) {
            return fail(error, "vkResetCommandBuffer failed");
        }
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            return fail(error, "vkBeginCommandBuffer failed");
        }

        uploadImage(commandBuffer, vramImage, vramLayout, vramStaging, kVramWidth, kVramHeight);
        if (drawOverlay || !overlayUploaded) {
            uploadImage(commandBuffer, overlayImage, overlayLayout, overlayStaging, overlayWidth,
                        overlayHeight);
            overlayUploaded = true;
        }

        if (descriptorsDirty) {
            updateDescriptors();
        }

        VkClearValue clear{};
        clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea = {{0, 0}, extent};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                                1, &descriptorSet, 0, nullptr);

        int destinationX = 0;
        int destinationY = 0;
        int destinationWidth = static_cast<int>(extent.width);
        int destinationHeight = static_cast<int>(extent.height);
        if (fourByThree) {
            if (destinationWidth * 3 > destinationHeight * 4) {
                destinationWidth = destinationHeight * 4 / 3;
            } else {
                destinationHeight = destinationWidth * 3 / 4;
            }
            destinationX = (static_cast<int>(extent.width) - destinationWidth) / 2;
            destinationY = (static_cast<int>(extent.height) - destinationHeight) / 2;
        }
        if (integerScale && srcW > 0 && srcH > 0) {
            const int scaleX = static_cast<int>(extent.width / srcW);
            const int scaleY = static_cast<int>(extent.height / srcH);
            const int scale = std::min(scaleX, scaleY);
            if (scale >= 1) {
                destinationWidth = static_cast<int>(srcW) * scale;
                destinationHeight = static_cast<int>(srcH) * scale;
                destinationX = (static_cast<int>(extent.width) - destinationWidth) / 2;
                destinationY = (static_cast<int>(extent.height) - destinationHeight) / 2;
            }
        }
        if (!displayEnabled || srcW == 0 || srcH == 0) {
            destinationWidth = 0;
            destinationHeight = 0;
        }

        PresentPushConstants push{};
        push.sourceRect[0] = static_cast<float>(srcX) / static_cast<float>(kVramWidth);
        push.sourceRect[1] = static_cast<float>(srcY) / static_cast<float>(kVramHeight);
        push.sourceRect[2] = static_cast<float>(srcX + srcW) / static_cast<float>(kVramWidth);
        push.sourceRect[3] = static_cast<float>(srcY + srcH) / static_cast<float>(kVramHeight);
        push.destinationRect[0] = static_cast<float>(destinationX);
        push.destinationRect[1] = static_cast<float>(destinationY);
        push.destinationRect[2] = static_cast<float>(destinationWidth);
        push.destinationRect[3] = static_cast<float>(destinationHeight);
        push.flags[0] = drawOverlay ? 1u : 0u;
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            return fail(error, "vkEndCommandBuffer failed");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished;
        const VkResult submitResult =
            vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlight);
        if (submitResult != VK_SUCCESS) {
            return fail(error, vkResultText("vkQueueSubmit", submitResult));
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        const VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
            acquireResult == VK_SUBOPTIMAL_KHR) {
            return recreateSwapchain(error);
        }
        if (presentResult != VK_SUCCESS) {
            return fail(error, vkResultText("vkQueuePresentKHR", presentResult));
        }
        return true;
    }

    void destroy() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }
        if (device != VK_NULL_HANDLE) {
            destroyOverlay();
        }
        if (vramStagingMapped != nullptr) {
            vkUnmapMemory(device, vramStagingMemory);
            vramStagingMapped = nullptr;
        }
        if (vramStaging != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vramStaging, nullptr);
            vramStaging = VK_NULL_HANDLE;
        }
        if (vramStagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vramStagingMemory, nullptr);
            vramStagingMemory = VK_NULL_HANDLE;
        }
        if (vramView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, vramView, nullptr);
            vramView = VK_NULL_HANDLE;
        }
        if (vramImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, vramImage, nullptr);
            vramImage = VK_NULL_HANDLE;
        }
        if (vramMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vramMemory, nullptr);
            vramMemory = VK_NULL_HANDLE;
        }
        if (device != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
            descriptorSet = VK_NULL_HANDLE;
            vkDestroySampler(device, samplerLinear, nullptr);
            samplerLinear = VK_NULL_HANDLE;
            vkDestroySampler(device, samplerNearest, nullptr);
            samplerNearest = VK_NULL_HANDLE;
            vkDestroyFence(device, inFlight, nullptr);
            inFlight = VK_NULL_HANDLE;
            vkDestroySemaphore(device, renderFinished, nullptr);
            renderFinished = VK_NULL_HANDLE;
            vkDestroySemaphore(device, imageAvailable, nullptr);
            imageAvailable = VK_NULL_HANDLE;
            if (commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, commandPool, nullptr);
                commandPool = VK_NULL_HANDLE;
                commandBuffer = VK_NULL_HANDLE;
            }
            destroySwapchainDependent();
            vkDestroyShaderModule(device, fragmentModule, nullptr);
            fragmentModule = VK_NULL_HANDLE;
            vkDestroyShaderModule(device, vertexModule, nullptr);
            vertexModule = VK_NULL_HANDLE;
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
            descriptorLayout = VK_NULL_HANDLE;
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
            instance = VK_NULL_HANDLE;
        }
        SDL_Vulkan_UnloadLibrary();
        window = nullptr;
    }
};

VulkanPresenter::VulkanPresenter() : impl_(std::make_unique<Impl>()) {}

VulkanPresenter::~VulkanPresenter() {
    if (impl_) {
        impl_->destroy();
    }
}

bool VulkanPresenter::initialize(SDL_Window* window, bool vsync, bool nearestFilter,
                                 std::string* error) {
    return impl_->initialize(window, vsync, nearestFilter, error);
}

bool VulkanPresenter::active() const {
    return impl_->active();
}

int VulkanPresenter::width() const {
    return static_cast<int>(impl_->extent.width);
}

int VulkanPresenter::height() const {
    return static_cast<int>(impl_->extent.height);
}

const std::string& VulkanPresenter::deviceName() const {
    return impl_->deviceName;
}

void VulkanPresenter::setVsync(bool vsync) {
    impl_->vsync = vsync;
}

void VulkanPresenter::setFilter(bool nearestFilter) {
    if (impl_->nearest != nearestFilter) {
        impl_->nearest = nearestFilter;
        impl_->descriptorsDirty = true;
    }
}

bool VulkanPresenter::present(const uint16_t* vram, uint32_t srcX, uint32_t srcY,
                              uint32_t srcW, uint32_t srcH, bool displayEnabled,
                              bool overlayEnabled, const uint8_t* overlayPixels,
                              int overlayWidth, int overlayHeight, bool integerScale,
                              bool fourByThree, std::string* error) {
    return impl_->present(vram, srcX, srcY, srcW, srcH, displayEnabled, overlayEnabled,
                          overlayPixels, overlayWidth, overlayHeight, integerScale,
                          fourByThree, error);
}

}  // namespace psxreborn

#endif
