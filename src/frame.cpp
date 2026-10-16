#include "frame.hpp"
#include "vulkan_utils.hpp"

#include <iostream>

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
                      std::size_t imageCount, FrameState& frame) {
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
    for (std::size_t index = 0; index < imageCount; ++index) {
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
