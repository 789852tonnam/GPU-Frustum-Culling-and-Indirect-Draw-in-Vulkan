#define GLFW_INCLUDE_NONE
#include "device.hpp"
#include "frame.hpp"
#include "swapchain.hpp"
#include "vulkan_utils.hpp"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

// Keep receiving window events while rendering is paused. A minimized window
// may keep a nonzero framebuffer size on X11, so check ICONIFIED as well.
bool waitForDrawableWindow(GLFWwindow* window, double closeAt = 0.0) {
    bool paused = false;
    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            (closeAt > 0.0 && glfwGetTime() >= closeAt)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return false;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0 && !glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            if (paused) {
                std::cout << "Window drawable again; resuming.\n";
            }
            return true;
        }
        if (!paused) {
            std::cout << "Rendering paused: window minimized or framebuffer has zero size.\n";
            paused = true;
        }
        glfwWaitEventsTimeout(0.05);
    }
    return false;
}

bool recreateSwapchain(const DeviceSelection& selection, VkDevice device,
                       VkSurfaceKHR surface, GLFWwindow* window,
                       SwapchainState& swapchain, FrameState& frame) {
    // Resize is infrequent: finish old work, then rebuild dependent objects.
    // Rebuilding FrameState also updates its per-image semaphore count and
    // starts with fresh synchronization objects after an out-of-date result.
    if (!checkVulkan(vkDeviceWaitIdle(device), "Wait for device before swapchain recreation")) {
        return false;
    }
    destroyFrameState(device, frame);
    destroySwapchain(device, swapchain);
    if (!createSwapchain(selection.device, device, surface, window, swapchain)) {
        return false;
    }
    return createFrameState(device, selection.queueFamilyIndex, swapchain.images.size(), frame);
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
    GLFWwindow* window = glfwCreateWindow(960, 540, "GPU Culling - S2.9 Resize", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Callbacks only request a rebuild; Vulkan work stays in the main loop.
    bool framebufferResized = false;
    glfwSetWindowUserPointer(window, &framebufferResized);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* resizedWindow, int, int) {
        auto* resized = static_cast<bool*>(glfwGetWindowUserPointer(resizedWindow));
        *resized = true;
    });
    glfwSetWindowIconifyCallback(window, [](GLFWwindow* changedWindow, int) {
        auto* resized = static_cast<bool*>(glfwGetWindowUserPointer(changedWindow));
        *resized = true;
    });

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

    // The window may have been minimized while device setup was running.
    glfwPollEvents();
    if (!waitForDrawableWindow(window)) {
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
        return smokeTest ? EXIT_FAILURE : EXIT_SUCCESS;
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
    framebufferResized = false;
    uint64_t presentedFrames = 0;
    uint32_t swapchainRecreations = 0;
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
        if (!waitForDrawableWindow(window, smokeTest ? startTime + 0.5 : 0.0)) {
            break;
        }
        if (framebufferResized) {
            success = recreateSwapchain(selection, device, surface, window, swapchain, frame);
            if (success) {
                framebufferResized = false;
                ++swapchainRecreations;
                std::cout << "Swapchain recreation completed.\n";
            }
            continue;
        }
        const FrameResult frameResult = drawFrame(device, queue, swapchain, frame);
        if (frameResult == FrameResult::OutOfDate) {
            std::cout << "Swapchain is out of date; requesting recreation.\n";
            framebufferResized = true;
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

    // Wait at shutdown (and during recreation), not after every submitted frame.
    if (!checkVulkan(vkDeviceWaitIdle(device), "Wait for device at shutdown")) {
        success = false;
    }
    std::cout << "Frames submitted for presentation: " << presentedFrames << '\n';
    std::cout << "Swapchain recreations: " << swapchainRecreations << '\n';
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
