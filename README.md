# GPU Culling in Vulkan

## Current milestone: S2.6 - Logical device and queue

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
On exit, it destroys the logical device (which owns the queue), then the surface,
instance, and native window. No GPU work is submitted yet, so shutdown needs no
device-idle wait in this milestone.
Query errors also release the surface, instance, and GLFW resources before
returning failure; unsupported presentation is printed as `No`, not an error.
Press Escape or use the window's close button to exit when the window is visible.
The event loop waits between events instead of busy-spinning.

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

This milestone does not create a swapchain, command buffers, or rendered image.
Only the two requested Vulkan 1.3 features are enabled; other queried features
are not enabled wholesale. Indirect-draw/culling features will be checked and
enabled when implementing those later milestones. Surface formats and
present modes are counted here; their actual values and current surface capabilities
must be queried again when constructing or recreating the swapchain.
On Wayland the window may not appear until its first image is presented. Use
Ctrl+C in the terminal to stop a non-visible interactive run. Later milestones
will add the swapchain and a clear frame for the D1 skeleton.

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

To display this early window through X11/XWayland on the Fedora setup:

```sh
XDG_SESSION_TYPE=x11 ./build/gpu_culling
```

This selects X11 for this process when GLFW supports it and an X server is
available; it does not change the desktop session.

Run with a Visual Studio multi-configuration build on Windows:

```powershell
.\build\Debug\gpu_culling.exe
```

For a short startup/shutdown check in a desktop session, append `--smoke-test`.
It creates the same window and exits normally after about half a second.
Successful startup prints `Vulkan instance created (requested API 1.3).` and normal
shutdown prints `Vulkan instance destroyed.` Between them it prints
`Vulkan window surface created.`, the device/queue-family list and selection reasons,
`Selected GPU:` and `Selected queue family:`, logical-device creation/queue
acquisition messages, `Logical device destroyed; its queue is released with it.`, then
`Vulkan window surface destroyed.` A positive presentation-support query is
a capability report, not an actual image-presentation test.
Enumeration errors or an empty list cause cleanup and a failure exit.
The enumeration retries if Vulkan returns `VK_INCOMPLETE`.
Physical-device handles belong to the instance and need no separate destruction.
This is not a rendering or GPU performance test.

On Linux with the Khronos validation layer installed, an instance/surface lifecycle
and device/queue lifecycle check can also be run with:

```sh
XDG_SESSION_TYPE=x11 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/gpu_culling --smoke-test
```

This enables validation for this run through the Vulkan loader. The application
does not yet install a debug messenger of its own.

## Code and attribution

- `src/main.cpp`: window, instance, surface, GPU selection, logical-device/queue
  creation, event loop, and cleanup in one file.
- `CMakeLists.txt`: build target, GLFW dependency, and Vulkan loader linkage.
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

AI assistance was used to draft this initial code and documentation. No external
renderer or research implementation is included at this stage. Verification
results are recorded separately in `../AI.md`, outside this Git repository.
