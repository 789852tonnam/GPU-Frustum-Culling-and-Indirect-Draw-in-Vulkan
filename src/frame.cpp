#include "frame.hpp"
#include "vulkan_utils.hpp"

#include <iostream>

/*
 * Frame cleanup: release synchronization objects and the command pool after GPU work has finished.
 * Destroying the pool also releases the command buffer allocated from it.
 * Reset the state so this path handles both complete resources and partial creation failures.
 */
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

/*
 * Frame setup: allocate reusable command recording and synchronization resources.
 * A fence protects CPU reuse of the one frame in flight; semaphores coordinate image acquisition and presentation.
 * Presentation may outlive that fence, so each swapchain image gets its own render-finished semaphore.
 */
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
                      FrameState& frame, VkPipeline pipeline, VkPipelineLayout layout,
                      VkPipeline spherePipeline, const MeshDraw& floor, const MeshDraw& spheres,
                      const DepthState& depth,
                      const glm::mat4& viewProjection) {
    /*
     * Reuse the frame safely and acquire a destination image.
     * Wait until the previous submission has finished before resetting or recording commands again.
     * Acquisition can request a retry or swapchain rebuild; return before submitting in those cases.
     */
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

    /*
     * Begin recording commands for this frame.
     * Reset the pool only after its previous GPU use has completed, then start a fresh command buffer.
     * The calls below record work; actual GPU execution begins after queue submission later in this function.
     */
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

    /*
     * Prepare colour and depth images for attachment access.
     * Discard their old contents because rendering clears them, while barriers order the relevant accesses.
     * The colour image comes from the swapchain; the depth image is reused by this single-frame renderer.
     */
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

    VkImageMemoryBarrier2 toDepth{};
    toDepth.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toDepth.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toDepth.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toDepth.dstStageMask = toDepth.srcStageMask;
    toDepth.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // One frame is in flight; discard the previous depth contents and clear again.
    toDepth.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    toDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepth.image = depth.image;
    toDepth.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    toDepth.subresourceRange.levelCount = 1;
    toDepth.subresourceRange.layerCount = 1;
    const VkImageMemoryBarrier2 attachments[]{toColor, toDepth};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 2;
    dependency.pImageMemoryBarriers = attachments;
    vkCmdPipelineBarrier2(frame.commandBuffer, &dependency);

    /*
     * Rendering scope: choose the attachments, clear values and image area.
     * Beginning dynamic rendering clears the colour background and resets depth to its far value.
     * Draw commands inside this scope write into these same attachments.
     */
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain.views[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.02f, 0.06f, 0.15f, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depth.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = swapchain.extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);

    /*
     * Draw the floor using its pipeline, mesh and camera transform.
     * Set viewport and scissor from the current swapchain dimensions instead of baking them into the pipeline.
     * The floor also writes depth so later opaque geometry is tested against it correctly.
     */
    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    // Positive viewport height pairs with the Y flip in the camera projection.
    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain.extent.width);
    viewport.height = static_cast<float>(swapchain.extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = swapchain.extent;
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(frame.commandBuffer, 0, 1, &floor.vertexBuffer, &vertexOffset);
    vkCmdBindIndexBuffer(frame.commandBuffer, floor.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(frame.commandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(viewProjection), &viewProjection);
    vkCmdDrawIndexed(frame.commandBuffer, floor.indexCount, 1, 0, 0, 0);

    /*
     * Draw all spheres with one shared mesh and a separate per-instance position stream.
     * The sphere vertex shader combines local vertices with each instance position.
     * This is a direct instanced draw of every object; frustum culling and indirect commands come later.
     */
    vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, spherePipeline);
    const VkBuffer sphereBuffers[]{spheres.vertexBuffer, spheres.instanceBuffer};
    const VkDeviceSize sphereOffsets[]{0, 0};
    vkCmdBindVertexBuffers(frame.commandBuffer, 0, 2, sphereBuffers, sphereOffsets);
    vkCmdBindIndexBuffer(frame.commandBuffer, spheres.indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    // Both pipeline layouts have the same camera push-constant range.
    vkCmdDrawIndexed(frame.commandBuffer, spheres.indexCount, spheres.instanceCount, 0, 0, 0);

    vkCmdEndRendering(frame.commandBuffer);

    /*
     * Finish recording and prepare the colour image for presentation.
     * Order colour writes before the presentation layout transition, then end the command buffer.
     * Depth stays off-screen and will be cleared again next frame; only the colour image is presented.
     */
    VkImageMemoryBarrier2 toPresent = toColor;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = VK_ACCESS_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(frame.commandBuffer, &dependency);

    if (!checkVulkan(vkEndCommandBuffer(frame.commandBuffer), "End command buffer")) {
        return FrameResult::Failed;
    }

    /*
     * Submit recorded work with synchronization between acquisition, drawing and presentation.
     * The submission waits for its colour image and signals the semaphore associated with that image.
     * Reset the fence only immediately before submission, so an earlier return cannot leave an unsignaled idle fence.
     */
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
        !checkVulkan(vkQueueSubmit2(queue, 1, &submitInfo, frame.inFlight), "Submit scene frame")) {
        return FrameResult::Failed;
    }

    /*
     * Present the completed colour image after its render-finished semaphore is signaled.
     * Report an outdated swapchain to the main loop so it can rebuild window-dependent resources.
     * A successful return means this frame was queued for presentation, not that every object was visible.
     */
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
    if (result != VK_SUBOPTIMAL_KHR && !checkVulkan(result, "Present scene frame")) {
        return FrameResult::Failed;
    }
    return FrameResult::Presented;
}
