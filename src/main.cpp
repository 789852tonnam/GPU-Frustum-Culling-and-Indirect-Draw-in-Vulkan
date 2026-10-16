#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace {

struct DeviceSelection {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex = 0;
};

struct SwapchainState {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
};

struct FrameState {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    // Presentation can outlive the submission fence: use one semaphore per image.
    std::vector<VkSemaphore> renderFinished;
};

enum class FrameResult { Presented, Retry, OutOfDate, Failed };

// Handle special statuses (INCOMPLETE, SUBOPTIMAL, TIMEOUT, ...) before calling.
bool checkVulkan(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    std::cerr << operation << " failed. VkResult: " << result << '\n';
    return false;
}

const char* deviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU (software implementation)";
    default: return "Other";
    }
}

bool printQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface,
                        std::optional<uint32_t>& unifiedFamily) {
    unifiedFamily.reset();
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
    families.resize(familyCount);

    std::cout << "  Queue families: " << familyCount << '\n';
    for (uint32_t index = 0; index < familyCount; ++index) {
        const VkQueueFamilyProperties& family = families[index];
        const bool graphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute = (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        // Graphics and compute queues also support transfer operations.
        const bool transfer = (family.queueFlags &
            (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != 0;
        const bool sparse = (family.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) != 0;
        const bool videoDecode = (family.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0;
        const bool videoEncode = (family.queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) != 0;

        // Presentation support is specific to this family and this surface.
        VkBool32 present = VK_FALSE;
        const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
        if (!checkVulkan(result, "Query presentation support")) {
            std::cerr << "Queue family index: " << index << '\n';
            return false;
        }

        if (!unifiedFamily && family.queueCount > 0 && graphics && compute && present == VK_TRUE) {
            unifiedFamily = index;
        }

        std::cout << "    Family " << index
                  << " | Queues: " << family.queueCount
                  << " | Graphics: " << (graphics ? "Yes" : "No")
                  << " | Compute: " << (compute ? "Yes" : "No")
                  << " | Present: " << (present == VK_TRUE ? "Yes" : "No") << '\n'
                  << "      Transfer: " << (transfer ? "Yes" : "No")
                  << " | Sparse binding: " << (sparse ? "Yes" : "No")
                  << " | Video decode: " << (videoDecode ? "Yes" : "No")
                  << " | Video encode: " << (videoEncode ? "Yes" : "No") << '\n';
    }
    return true;
}

// Return false for query errors. Unsupported capabilities use supported = false.
bool checkSwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface, bool& supported) {
    supported = false;
    uint32_t extensionCount = 0;
    std::vector<VkExtensionProperties> extensions;
    VkResult result;
    do {
        result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        if (!checkVulkan(result, "Count device extensions")) {
            return false;
        }
        extensions.resize(extensionCount);
        result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    } while (result == VK_INCOMPLETE);
    if (!checkVulkan(result, "Read device extensions")) {
        return false;
    }
    extensions.resize(extensionCount);

    bool hasSwapchain = false;
    for (const VkExtensionProperties& extension : extensions) {
        if (std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
            hasSwapchain = true;
            break;
        }
    }
    if (!hasSwapchain) {
        std::cout << "  Skip: VK_KHR_swapchain is unavailable.\n\n";
        return true;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);
    if (!checkVulkan(result, "Read surface capabilities")) {
        return false;
    }
    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (!checkVulkan(result, "Count surface formats")) {
        return false;
    }
    uint32_t presentModeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (!checkVulkan(result, "Count present modes")) {
        return false;
    }
    std::cout << "  Swapchain: Yes | Surface formats: " << formatCount
              << " | Present modes: " << presentModeCount << '\n';
    if (formatCount == 0 || presentModeCount == 0) {
        std::cout << "  Skip: surface formats or present modes are unavailable.\n\n";
        return true;
    }
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        std::cout << "  Skip: surface images cannot be used as color attachments.\n\n";
        return true;
    }
    supported = true;
    return true;
}

int devicePriority(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 3;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 1;
    default: return 0;
    }
}

bool selectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, DeviceSelection& selection) {
    selection = {};
    uint32_t deviceCount = 0;
    std::vector<VkPhysicalDevice> devices;
    VkResult result;

    // Query the count, then retrieve the handles. Retry if Vulkan reports an incomplete list.
    do {
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (!checkVulkan(result, "Count Vulkan devices")) {
            return false;
        }
        if (deviceCount == 0) {
            std::cerr << "No Vulkan physical devices found.\n";
            return false;
        }

        devices.resize(deviceCount);
        result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    } while (result == VK_INCOMPLETE);

    if (!checkVulkan(result, "Enumerate Vulkan devices")) {
        return false;
    }
    devices.resize(deviceCount);
    if (devices.empty()) {
        std::cerr << "No Vulkan physical devices found.\n";
        return false;
    }

    std::cout << "Vulkan physical devices: " << deviceCount << '\n';
    int bestPriority = -1;
    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        std::cout << "  Name: " << properties.deviceName
                  << "\n  Type: " << deviceTypeName(properties.deviceType)
                  << "\n  Vulkan API: " << VK_API_VERSION_MAJOR(properties.apiVersion)
                  << '.' << VK_API_VERSION_MINOR(properties.apiVersion)
                  << '.' << VK_API_VERSION_PATCH(properties.apiVersion) << '\n';
        std::optional<uint32_t> unifiedFamily;
        if (!printQueueFamilies(device, surface, unifiedFamily)) {
            return false;
        }
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            std::cout << "  Skip: CPU software rendering is excluded from GPU selection.\n\n";
            continue;
        }
        if (VK_API_VERSION_VARIANT(properties.apiVersion) != 0 ||
            properties.apiVersion < VK_API_VERSION_1_3) {
            std::cout << "  Skip: Vulkan 1.3 or newer is required.\n\n";
            continue;
        }
        if (!unifiedFamily) {
            std::cout << "  Skip: no single family supports graphics + compute + present.\n\n";
            continue;
        }

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(device, &features);
        std::cout << "  Dynamic rendering: " << (features13.dynamicRendering ? "Yes" : "No")
                  << " | Synchronization2: " << (features13.synchronization2 ? "Yes" : "No") << '\n';
        if (!features13.dynamicRendering || !features13.synchronization2) {
            std::cout << "  Skip: dynamicRendering and synchronization2 are required.\n\n";
            continue;
        }

        bool swapchainSupported = false;
        if (!checkSwapchainSupport(device, surface, swapchainSupported)) {
            return false;
        }
        if (!swapchainSupported) {
            continue;
        }

        const int priority = devicePriority(properties.deviceType);
        std::cout << "  Eligible | Unified family: " << *unifiedFamily
                  << " | Device-type priority: " << priority << "\n\n";
        if (priority > bestPriority) {
            selection.device = device;
            selection.queueFamilyIndex = *unifiedFamily;
            bestPriority = priority;
        }
    }
    if (selection.device == VK_NULL_HANDLE) {
        std::cerr << "No suitable GPU found for the Vulkan 1.3 skeleton.\n";
        return false;
    }
    return true;
}

bool createLogicalDevice(const DeviceSelection& selection, VkDevice& device, VkQueue& queue) {
    device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = selection.queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    // Enable only the features already checked during GPU selection.
    VkPhysicalDeviceVulkan13Features enabledFeatures{};
    enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enabledFeatures.dynamicRendering = VK_TRUE;
    enabledFeatures.synchronization2 = VK_TRUE;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enabledFeatures;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = extensions;

    const VkResult result = vkCreateDevice(selection.device, &deviceInfo, nullptr, &device);
    if (!checkVulkan(result, "Create logical device")) {
        return false;
    }

    // Queue index 0 is the first (and only) queue requested from this family.
    vkGetDeviceQueue(device, selection.queueFamilyIndex, 0, &queue);
    std::cout << "Logical device created; dynamicRendering, synchronization2, and VK_KHR_swapchain enabled.\n"
              << "Queue acquired: family " << selection.queueFamilyIndex << ", queue index 0.\n";
    return true;
}

void destroySwapchain(VkDevice device, SwapchainState& swapchain) {
    for (const VkImageView view : swapchain.views) {
        vkDestroyImageView(device, view, nullptr);
    }
    if (swapchain.handle != VK_NULL_HANDLE) {
        // The swapchain owns its images; do not call vkDestroyImage on them.
        vkDestroySwapchainKHR(device, swapchain.handle, nullptr);
    }
    swapchain = {};
}

bool createSwapchain(VkPhysicalDevice physicalDevice, VkDevice device,
                     VkSurfaceKHR surface, GLFWwindow* window, SwapchainState& swapchain) {
    VkSurfaceCapabilitiesKHR capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
    if (!checkVulkan(result, "Read swapchain surface capabilities")) {
        return false;
    }

    uint32_t formatCount = 0;
    std::vector<VkSurfaceFormatKHR> formats;
    do {
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        if (!checkVulkan(result, "Count swapchain surface formats")) {
            return false;
        }
        if (formatCount == 0) {
            std::cerr << "Surface formats unavailable.\n";
            return false;
        }
        formats.resize(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    } while (result == VK_INCOMPLETE);
    if (!checkVulkan(result, "Read swapchain surface formats")) {
        return false;
    }
    if (formatCount == 0) {
        std::cerr << "Surface formats unavailable.\n";
        return false;
    }
    formats.resize(formatCount);

    // Prefer BGRA8 sRGB; otherwise use the first color-renderable surface format.
    std::optional<VkSurfaceFormatKHR> surfaceFormat;
    for (const VkSurfaceFormatKHR candidate : formats) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate.format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0) {
            continue;
        }
        if (!surfaceFormat) {
            surfaceFormat = candidate;
        }
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = candidate;
            break;
        }
    }
    if (!surfaceFormat) {
        std::cerr << "No color-renderable surface format found.\n";
        return false;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        std::cerr << "Cannot create swapchain for a zero-size framebuffer.\n";
        return false;
    }
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max()) {
        extent.width = std::clamp(static_cast<uint32_t>(width),
            capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(height),
            capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        std::cerr << "Surface currently has a zero-size extent.\n";
        return false;
    }

    uint32_t imageCount = capabilities.minImageCount;
    if (imageCount < std::numeric_limits<uint32_t>::max() &&
        (capabilities.maxImageCount == 0 || imageCount < capabilities.maxImageCount)) {
        ++imageCount;
    }
    // maxImageCount == 0 means there is no surface-imposed maximum.
    const VkCompositeAlphaFlagBitsKHR alphaModes[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    std::optional<VkCompositeAlphaFlagBitsKHR> compositeAlpha;
    for (const VkCompositeAlphaFlagBitsKHR mode : alphaModes) {
        if ((capabilities.supportedCompositeAlpha & mode) != 0) {
            compositeAlpha = mode;
            break;
        }
    }
    if (!compositeAlpha ||
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        std::cerr << "Surface does not support the requested swapchain usage or alpha mode.\n";
        return false;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat->format;
    swapchainInfo.imageColorSpace = surfaceFormat->colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = *compositeAlpha;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed for ordinary surfaces.
    swapchainInfo.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain.handle);
    if (!checkVulkan(result, "Create swapchain")) {
        return false;
    }
    swapchain.format = surfaceFormat->format;
    swapchain.extent = extent;

    // The implementation may provide more images than the requested minimum.
    do {
        result = vkGetSwapchainImagesKHR(device, swapchain.handle, &imageCount, nullptr);
        if (!checkVulkan(result, "Count swapchain images")) {
            destroySwapchain(device, swapchain);
            return false;
        }
        if (imageCount == 0) {
            std::cerr << "No swapchain images available.\n";
            destroySwapchain(device, swapchain);
            return false;
        }
        swapchain.images.resize(imageCount);
        result = vkGetSwapchainImagesKHR(device, swapchain.handle, &imageCount, swapchain.images.data());
    } while (result == VK_INCOMPLETE);
    if (!checkVulkan(result, "Read swapchain images")) {
        destroySwapchain(device, swapchain);
        return false;
    }
    if (imageCount == 0) {
        std::cerr << "No swapchain images available.\n";
        destroySwapchain(device, swapchain);
        return false;
    }
    swapchain.images.resize(imageCount);
    swapchain.views.reserve(imageCount);
    for (const VkImage image : swapchain.images) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchain.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView view = VK_NULL_HANDLE;
        result = vkCreateImageView(device, &viewInfo, nullptr, &view);
        if (!checkVulkan(result, "Create swapchain image view")) {
            destroySwapchain(device, swapchain);
            return false;
        }
        swapchain.views.push_back(view);
    }
    std::cout << "Swapchain created: " << extent.width << 'x' << extent.height
              << " | Images: " << swapchain.images.size()
              << " | Format enum: " << swapchain.format
              << " | Color-space enum: " << surfaceFormat->colorSpace
              << " | Present mode: FIFO\n"
              << "Swapchain image views created: " << swapchain.views.size() << '\n';
    return true;
}

void destroyFrameState(VkDevice device, FrameState& frame) {
    // The caller waits for submitted GPU work before releasing these objects.
    for (const VkSemaphore semaphore : frame.renderFinished) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    if (frame.imageAvailable != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, frame.imageAvailable, nullptr);
    }
    if (frame.inFlight != VK_NULL_HANDLE) {
        vkDestroyFence(device, frame.inFlight, nullptr);
    }
    if (frame.commandPool != VK_NULL_HANDLE) {
        // Destroying the pool also frees its command buffer.
        vkDestroyCommandPool(device, frame.commandPool, nullptr);
    }
    frame = {};
}

bool createFrameState(VkDevice device, uint32_t queueFamilyIndex,
                      size_t imageCount, FrameState& frame) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    if (!checkVulkan(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool),
                     "Create command pool")) {
        return false;
    }

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = frame.commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    if (!checkVulkan(vkAllocateCommandBuffers(device, &allocateInfo, &frame.commandBuffer),
                     "Allocate command buffer")) {
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // The first frame has no previous work.
    if (!checkVulkan(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight),
                     "Create frame fence")) {
        return false;
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (!checkVulkan(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable),
                     "Create image-available semaphore")) {
        return false;
    }
    frame.renderFinished.reserve(imageCount);
    for (size_t index = 0; index < imageCount; ++index) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (!checkVulkan(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore),
                         "Create render-finished semaphore")) {
            return false;
        }
        frame.renderFinished.push_back(semaphore);
    }
    std::cout << "Frame resources created: one command buffer/fence, one frame in flight.\n";
    return true;
}

FrameResult drawFrame(VkDevice device, VkQueue queue, const SwapchainState& swapchain,
                      FrameState& frame) {
    if (!checkVulkan(vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
                     "Wait for previous frame")) {
        return FrameResult::Failed;
    }

    uint32_t imageIndex = 0;
    // A finite timeout lets the event loop respond even if no image is available.
    VkResult result = vkAcquireNextImageKHR(device, swapchain.handle, 1'000'000'000,
                                            frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return FrameResult::OutOfDate;
    }
    if (result == VK_TIMEOUT || result == VK_NOT_READY) {
        return FrameResult::Retry;
    }
    if (result != VK_SUBOPTIMAL_KHR && !checkVulkan(result, "Acquire swapchain image")) {
        return FrameResult::Failed;
    }

    // The fence has completed, so the whole pool can be reset and reused safely.
    if (!checkVulkan(vkResetCommandPool(device, frame.commandPool, 0), "Reset command pool")) {
        return FrameResult::Failed;
    }
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!checkVulkan(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "Begin command buffer")) {
        return FrameResult::Failed;
    }

    VkImageMemoryBarrier2 toColor{};
    toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toColor.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    // We clear the whole image, so its previous contents can be discarded every frame.
    toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.image = swapchain.images[imageIndex];
    toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toColor.subresourceRange.levelCount = 1;
    toColor.subresourceRange.layerCount = 1;

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toColor;
    vkCmdPipelineBarrier2(frame.commandBuffer, &dependency);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain.views[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.02f, 0.06f, 0.15f, 1.0f}};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = swapchain.extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);
    vkCmdEndRendering(frame.commandBuffer);

    VkImageMemoryBarrier2 toPresent = toColor;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = VK_ACCESS_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(frame.commandBuffer, &dependency);

    if (!checkVulkan(vkEndCommandBuffer(frame.commandBuffer), "End command buffer")) {
        return FrameResult::Failed;
    }

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = frame.commandBuffer;
    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = frame.renderFinished[imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    // Reset only when a submission is ready; an early acquire return keeps it signaled.
    if (!checkVulkan(vkResetFences(device, 1, &frame.inFlight), "Reset frame fence") ||
        !checkVulkan(vkQueueSubmit2(queue, 1, &submitInfo, frame.inFlight), "Submit clear frame")) {
        return FrameResult::Failed;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinished[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.handle;
    presentInfo.pImageIndices = &imageIndex;
    result = vkQueuePresentKHR(queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return FrameResult::OutOfDate;
    }
    if (result != VK_SUBOPTIMAL_KHR && !checkVulkan(result, "Present clear frame")) {
        return FrameResult::Failed;
    }
    return FrameResult::Presented;
}

} // namespace

int main(int argc, char** argv) {
    const bool smokeTest = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
    if (argc > 1 && !smokeTest) {
        std::cerr << "Usage: gpu_culling [--smoke-test]\n";
        return EXIT_FAILURE;
    }

    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    });

    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    // Vulkan handles rendering; no OpenGL context is needed.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(960, 540, "GPU Culling - S2.8 Clear Frame", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "GLFW window created (960x540). Press Escape or close the window to exit.\n";

    // GLFW chooses the surface extensions for the active window system.
    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (!extensions) {
        std::cerr << "Vulkan loader or required window-system extensions are unavailable.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "GPU Culling";
    applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = extensionCount;
    instanceInfo.ppEnabledExtensionNames = extensions;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (!checkVulkan(result, "Create Vulkan 1.3 instance")) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "Vulkan instance created (requested API 1.3).\n";
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (!checkVulkan(surfaceResult, "Create Vulkan window surface")) {
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    std::cout << "Vulkan window surface created.\n";

    DeviceSelection selection{};
    if (!selectPhysicalDevice(instance, surface, selection)) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    VkPhysicalDeviceProperties selectedProperties{};
    vkGetPhysicalDeviceProperties(selection.device, &selectedProperties);
    std::cout << "Selected GPU: " << selectedProperties.deviceName
              << "\nSelected queue family: " << selection.queueFamilyIndex
              << " (graphics + compute + present)\n"
              << "Preference: discrete > integrated > virtual > other; ties keep the first eligible device.\n";

    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    if (!createLogicalDevice(selection, device, queue)) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    SwapchainState swapchain{};
    if (!createSwapchain(selection.device, device, surface, window, swapchain)) {
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    FrameState frame{};
    bool success = createFrameState(device, selection.queueFamilyIndex, swapchain.images.size(), frame);
    uint64_t presentedFrames = 0;
    const double startTime = glfwGetTime();

    while (success && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            (smokeTest && glfwGetTime() - startTime >= 0.5)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (glfwWindowShouldClose(window)) {
            break;
        }
        const FrameResult frameResult = drawFrame(device, queue, swapchain, frame);
        if (frameResult == FrameResult::OutOfDate) {
            std::cerr << "Swapchain is out of date; recreation will be added in the next milestone.\n";
            success = false;
        } else if (frameResult == FrameResult::Failed) {
            success = false;
        } else if (frameResult == FrameResult::Presented) {
            ++presentedFrames;
            if (presentedFrames == 1) {
                std::cout << "First clear frame submitted for presentation (dark blue).\n";
            }
        }
    }
    if (smokeTest && presentedFrames == 0) {
        std::cerr << "Smoke test did not present any frames.\n";
        success = false;
    }

    // Small skeleton: wait once at shutdown, not after every submitted frame.
    if (!checkVulkan(vkDeviceWaitIdle(device), "Wait for device at shutdown")) {
        success = false;
    }
    std::cout << "Frames submitted for presentation: " << presentedFrames << '\n';
    destroyFrameState(device, frame);
    std::cout << "Frame resources destroyed.\n";
    destroySwapchain(device, swapchain);
    std::cout << "Swapchain image views and swapchain destroyed.\n";
    vkDestroyDevice(device, nullptr);
    std::cout << "Logical device destroyed; its queue is released with it.\n";
    // The surface must be released before its instance and native window.
    vkDestroySurfaceKHR(instance, surface, nullptr);
    std::cout << "Vulkan window surface destroyed.\n";
    vkDestroyInstance(instance, nullptr);
    std::cout << "Vulkan instance destroyed.\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Window closed; GLFW shut down.\n";
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
