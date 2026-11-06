#pragma once

#include "swapchain.hpp"
#include "depth.hpp"

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <vector>

/*
 * Frame resources: keep command recording and GPU/presentation synchronization together.
 * Only one frame is submitted at a time, but presentation has a completion semaphore per image.
 * The caller waits for outstanding work before destroying or rebuilding this state.
 */
struct FrameState {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    // Presentation can outlive the submission fence: use one semaphore per image.
    std::vector<VkSemaphore> renderFinished;
};

enum class FrameResult { Presented, Retry, OutOfDate, Failed };

/*
 * Draw description: identify the mesh buffers and how many indices/instances to submit.
 * Spheres also provide a position stream that advances once per instance; the floor uses no such stream.
 * These are resource references, not owners: main allocates memory and releases the referenced buffers.
 */
struct MeshDraw {
    // Draw metadata: these handles refer to resources owned and released by main.
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    // Optional per-instance positions; the floor leaves this null.
    VkBuffer instanceBuffer = VK_NULL_HANDLE;
    uint32_t instanceCount = 1;
};

/*
 * Frame operations: create reusable resources, draw one frame, and release the resources when finished.
 * Drawing reports whether a frame was presented, should be retried, needs a new swapchain, or failed.
 * The main loop owns those decisions and supplies live pipelines, mesh buffers and attachments.
 */
void destroyFrameState(VkDevice device, FrameState& frame);
bool createFrameState(VkDevice device, uint32_t queueFamilyIndex,
                      std::size_t imageCount, FrameState& frame);
FrameResult drawFrame(VkDevice device, VkQueue queue, const SwapchainState& swapchain,
                      FrameState& frame, VkPipeline pipeline, VkPipelineLayout layout,
                      VkPipeline spherePipeline, const MeshDraw& floor, const MeshDraw& spheres,
                      const DepthState& depth,
                      const glm::mat4& viewProjection);
