# GPU Culling in Vulkan

## Current milestone: S2.9 - Resize and minimize recovery

A small C++20 application that creates a 960 x 540 GLFW window, processes events,
and creates a Vulkan instance requesting API 1.3. The instance enables the window
system extensions supplied by GLFW and is destroyed when the application exits.
After instance creation, it creates a `VkSurfaceKHR` for the GLFW window using
`glfwCreateWindowSurface`, then lists the available Vulkan physical devices with
their names, device types, and supported Vulkan API versions. Software devices
are labeled as CPU implementations. For each device it prints every queue
family's index, queue count, graphics/compute capabilities, and presentation
support for this particular surface. A second line reports transfer, sparse
binding, video decode, and video encode support. Transfer is also reported for
graphics/compute families, where Vulkan implicitly supports transfer operations
even if the explicit transfer flag is absent. These are capability reports;
video codec/feature support is not inspected and no video work is performed.
It then selects an eligible physical device and queue-family index, printing the
chosen GPU and family. From `DeviceSelection` it creates one logical device with
`dynamicRendering`, `synchronization2`, and `VK_KHR_swapchain` enabled. It requests
one queue at priority 1.0 from the selected graphics/compute/present family, then
retrieves queue index 0 with `vkGetDeviceQueue`.
It then creates a swapchain, retrieves its images, and creates one 2D color image
view per image, stored in `SwapchainState`. A single-frame rendering loop acquires
an image, clears it dark blue using dynamic rendering, and submits it for FIFO
presentation. No graphics pipeline, shaders, or draw calls are needed for a clear.
On exit, it waits for the device, releases frame resources, then destroys the image
views, swapchain, logical device (which owns the queue), surface, instance, and
native window.
Query errors also release the surface, instance, and GLFW resources before
returning failure; unsupported presentation is printed as `No`, not an error.
Press Escape or use the window's close button to exit when the window is visible.
The event loop polls input between frames; FIFO presentation supplies pacing.

The selection policy requires:

- Vulkan API variant 0, version 1.3 or newer.
- A non-CPU device; software CPU implementations are still listed but skipped.
- One queue family with at least one queue and graphics + compute + presentation
  support for this window surface. Split graphics/present families are deliberately
  outside this first skeleton's policy, even though Vulkan can support them.
- `dynamicRendering` and `synchronization2` support, queried with
  `VkPhysicalDeviceFeatures2` and `VkPhysicalDeviceVulkan13Features`.
- `VK_KHR_swapchain`, at least one surface format and present mode, and support
  for `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` on swapchain images.

Among eligible devices, preference is discrete > integrated > virtual > other.
Devices with equal priority keep enumeration order; the first eligible family
within each device is used. This is a simple policy, not a performance measurement.
Unsupported requirements print a reason and skip that device. Query errors abort
inspection with cleanup; no eligible GPU produces a failure exit. A CPU-only
system therefore fails intentionally rather than silently selecting software rendering.

The swapchain uses:

- FIFO presentation, guaranteed by Vulkan for ordinary window surfaces.
- BGRA8 sRGB with the sRGB nonlinear color space when available; otherwise the
  first advertised surface format with optimal-tiling color-attachment support.
- The required current surface extent, or the actual GLFW framebuffer pixel size
  clamped to the supported extent range when the surface leaves the size to us.
- One image above the minimum when allowed, respecting a finite maximum and the
  special unlimited `maxImageCount == 0` case. The actual image count is queried.
- Exclusive sharing for the unified queue family, one image layer, the current
  surface transform, and opaque composite alpha when supported (otherwise the
  first supported alpha mode).

The main loop handles framebuffer resize and iconify events. It pauses rendering
while the window is iconified or the framebuffer has zero width/height, including
before initial swapchain creation. The pause continues to process window events
and Escape/close requests; the smoke-test deadline also applies during an in-loop
pause. On restore or resize it waits for old GPU work, destroys frame resources
and the old swapchain/views, then recreates them using current surface properties.
Rebuilding FrameState keeps presentation semaphores matched to the new image count
and gives the new swapchain fresh synchronization objects. Image-query or image-view
creation failure destroys any views already created and the swapchain before
device teardown.
Swapchain images are owned by the swapchain and are not separately destroyed.

## Frame loop

General `VkResult` success/error checks share the local `checkVulkan` helper.
Retry/usable statuses such as `VK_INCOMPLETE`, `VK_TIMEOUT`, and
`VK_SUBOPTIMAL_KHR`, and the recreation-needed status `VK_ERROR_OUT_OF_DATE_KHR`,
are handled by the caller first. Successful queries with empty required lists
are checked separately from API errors.

- `FrameState` owns one command pool/primary command buffer, one initially signaled
  submission fence, one image-available semaphore, and one render-finished semaphore
  per swapchain image. Partial initialization is cleaned up by the caller.
- Wait for the previous submission fence before reusing the command pool and the
  image-available semaphore. Acquire with a finite timeout; a timeout retries via
  the event loop. Reset the fence only immediately before submission, so an early
  acquire return does not leave the next frame waiting for nonexistent work.
- Record synchronization2 image barriers into color-attachment layout and then
  presentation layout. Discard previous image contents because the full image is
  cleared each frame. Dynamic rendering uses CLEAR/STORE with linear RGBA
  `(0.02, 0.06, 0.15, 1.0)`.
- Submit using `vkQueueSubmit2`, waiting on image availability at color-attachment
  output. Signal the acquired image's render-finished semaphore after all commands;
  `vkQueuePresentKHR` waits on that semaphore. Buffering these semaphores by image
  avoids reusing one while presentation is still waiting on it.
- `VK_SUBOPTIMAL_KHR` remains usable. `VK_ERROR_OUT_OF_DATE_KHR` requests a
  rebuild in main instead of terminating. Resize/iconify callbacks also request
  a rebuild, even on platforms that do not report OUT_OF_DATE for every resize.
  Callbacks only set a flag; resource work happens between drawFrame calls.
  Query, allocation, device-loss, and other unrecoverable errors still exit with
  code 1 after cleanup. Surface-loss/device-loss recovery is outside this skeleton.
- Shutdown/recreation use the usual unextended-Vulkan `vkDeviceWaitIdle` approach.
  This waits for submitted GPU work; it is not a formal presentation-completion fence.
  Explicit presentation fences require a maintenance extension, outside this step.

Only the two requested Vulkan 1.3 features are enabled; other queried features
are not enabled wholesale. Indirect-draw/culling features will be checked and
enabled when implementing those later milestones. Surface formats and
present modes are counted here; their actual values and current surface capabilities
must be queried again when constructing or recreating the swapchain.
The first presentation supplies window content. Native Wayland behavior still
needs separate verification; local X11/XWayland verification is recorded in the
external AI log. Minimize detection checks GLFW_ICONIFIED as well as framebuffer
size, because some platforms keep a nonzero framebuffer while iconified.

## Build

Requires CMake 3.24+, a C++20 compiler, Vulkan headers and loader (development
package or Vulkan SDK with headers 1.3+), and a graphical desktop session to run.
On Windows, having `vulkaninfo` available does not necessarily mean the development
headers and linker library are installed. CMake must find `Vulkan::Vulkan`.
GLFW 3.4 is used from the system when available; otherwise CMake downloads the
upstream `3.4` tag. That first build needs Git and network access. Building GLFW
from source on Linux also requires its platform development dependencies.

```sh
cmake -S . -B build
cmake --build build --config Debug
```

Run on Linux:

```sh
./build/gpu_culling
```

To select X11/XWayland on the Fedora setup:

```sh
XDG_SESSION_TYPE=x11 ./build/gpu_culling
```

This selects X11 for this process when GLFW supports it and an X server is
available; it does not change the desktop session.

Run with a Visual Studio multi-configuration build on Windows:

```powershell
.\build\Debug\gpu_culling.exe
```

For a short rendering/shutdown check in a desktop session, append `--smoke-test`.
It runs the same frame loop for about half a second and requires at least one
successful presentation request before returning success. This is not proof of
physical display visibility or a GPU performance benchmark.
Successful startup prints `Vulkan instance created (requested API 1.3).` and normal
shutdown prints `Vulkan instance destroyed.` Between them it prints
`Vulkan window surface created.`, the device/queue-family list and selection reasons,
`Selected GPU:` and `Selected queue family:`, logical-device creation/queue
acquisition messages, swapchain size/image count/format/color-space enums/FIFO
mode and image-view count, frame-resource creation, first-frame presentation
submission and total presented-request count, then frame/image-view/swapchain destruction,
`Logical device destroyed; its queue is released with it.`, then
`Vulkan window surface destroyed.` A positive presentation-support query is
a capability report, not an actual image-presentation test.
Enumeration errors or an empty list cause cleanup and a failure exit.
The enumeration retries if Vulkan returns `VK_INCOMPLETE`.
Physical-device handles belong to the instance and need no separate destruction.
No culling or scene geometry is implemented yet.

On Linux with the Khronos validation layer installed, an instance/surface lifecycle
and frame synchronization/lifecycle check can also be run with:

```sh
XDG_SESSION_TYPE=x11 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/gpu_culling --smoke-test
```

This enables validation for this run through the Vulkan loader. The application
does not yet install a debug messenger of its own.

## Code and attribution

Source is divided by responsibility:

- `src/main.cpp`: window, instance and surface creation; startup sequence, event
  loop, calls to drawFrame, and shutdown order. The new waitForDrawableWindow and
  recreateSwapchain helpers and resize/iconify callbacks stay here for the code
  walkthrough; resize logic has not been moved into another module.
- `src/device.hpp` / `src/device.cpp`: DeviceSelection, physical-device selection
  and logical-device/queue creation. Device inspection helpers are private to
  device.cpp in an anonymous namespace.
- `src/swapchain.hpp` / `src/swapchain.cpp`: SwapchainState, swapchain/image-view
  creation, and their cleanup. The header forward-declares GLFWwindow; only the
  implementation needs the GLFW header.
- `src/frame.hpp` / `src/frame.cpp`: FrameState, FrameResult, frame-resource
  creation/cleanup, command recording, submission, and presentation.
- `src/vulkan_utils.hpp`: shared inline checkVulkan helper. Inline allows the
  header's definition to be included in multiple translation units.
- Headers expose shared structs and function declarations; .cpp files hold the
  implementations. To follow execution, start in main.cpp and open the module
  corresponding to the function call being read.
- `CMakeLists.txt`: all four .cpp files in one executable, GLFW dependency, and
  Vulkan loader linkage. No new dependency is needed for resize support.

References and attribution:

- GLFW: <https://github.com/glfw/glfw/tree/3.4>, zlib/libpng license.
- API reference: <https://www.glfw.org/docs/latest/vulkan_guide.html>.
- Vulkan instance reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateInstance.html>.
- Device enumeration reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkEnumeratePhysicalDevices.html>.
- Device properties reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceProperties.html>.
- Surface creation reference: <https://www.glfw.org/docs/3.4/group__vulkan.html>.
- Surface destruction reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySurfaceKHR.html>.
- Queue family reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceQueueFamilyProperties.html>.
- Presentation query reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceSupportKHR.html>.
- Queue capability flags: <https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFlagBits.html>.
- Feature query reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceFeatures2.html>.
- Vulkan 1.3 feature reference: <https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan13Features.html>.
- Device extensions: <https://docs.vulkan.org/refpages/latest/refpages/source/vkEnumerateDeviceExtensionProperties.html>.
- Surface capabilities: <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html>.
- Logical-device creation: <https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateDevice.html>.
- Queue creation information: <https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceQueueCreateInfo.html>.
- Queue retrieval: <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceQueue.html>.
- Swapchain creation: <https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html>.
- Swapchain images: <https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainImagesKHR.html>.
- Image views: <https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html>.
- FIFO presentation: <https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html>.
- Dynamic rendering: <https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBeginRendering.html>.
- Synchronization examples: <https://docs.vulkan.org/guide/latest/synchronization_examples.html>.
- Presentation semaphore reuse and shutdown limits: <https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html>.
- Swapchain recreation and minimization: <https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/04_Swap_chain_recreation.html>.

AI assistance was used to draft this initial code and documentation. No external
renderer or research implementation is included at this stage. Verification
results are recorded separately in `../AI.md`, outside this Git repository.
