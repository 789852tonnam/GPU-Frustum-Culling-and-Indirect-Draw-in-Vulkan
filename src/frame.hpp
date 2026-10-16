#pragma once

#include "swapchain.hpp"

#include <cstddef>
#include <vector>

struct FrameState {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    // Presentation can outlive the submission fence: use one semaphore per image.
    std::vector<VkSemaphore> renderFinished;
};

enum class FrameResult { Presented, Retry, OutOfDate, Failed };

void destroyFrameState(VkDevice device, FrameState& frame);
bool createFrameState(VkDevice device, uint32_t queueFamilyIndex,
                      std::size_t imageCount, FrameState& frame);
FrameResult drawFrame(VkDevice device, VkQueue queue, const SwapchainState& swapchain,
                      FrameState& frame);
