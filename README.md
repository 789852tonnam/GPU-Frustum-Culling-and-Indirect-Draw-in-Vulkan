# GPU Culling in Vulkan

## Current milestone: S2.4 - Queue families and presentation support

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
It does not select a device or request queues.
On exit, it destroys the surface before destroying the instance and native window.
Query errors also release the surface, instance, and GLFW resources before
returning failure; unsupported presentation is printed as `No`, not an error.
Press Escape or use the window's close button to exit when the window is visible.
The event loop waits between events instead of busy-spinning.

This milestone does not select a GPU or create a logical device, swapchain,
or rendered image. Reported API versions and queue capabilities do not verify
the individual device features, swapchain extension, surface formats, or present
modes that later device selection and swapchain creation will require.
On Wayland the window may not appear until its first image is presented. Use
Ctrl+C in the terminal to stop a non-visible interactive run. Later milestones
will add device selection, the swapchain, and a clear frame for the D1 skeleton.

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
`Vulkan window surface created.`, the device/queue-family list, then
`Vulkan window surface destroyed.` A positive presentation-support query is
a capability report, not an actual image-presentation test.
Enumeration errors or an empty list cause cleanup and a failure exit.
The enumeration retries if Vulkan returns `VK_INCOMPLETE`.
Physical-device handles belong to the instance and need no separate destruction.
This is not a rendering or GPU performance test.

On Linux with the Khronos validation layer installed, an instance/surface lifecycle
and device/queue inspection check can also be run with:

```sh
XDG_SESSION_TYPE=x11 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/gpu_culling --smoke-test
```

This enables validation for this run through the Vulkan loader. The application
does not yet install a debug messenger of its own.

## Code and attribution

- `src/main.cpp`: window, instance, surface, device/queue inspection, event loop, and cleanup.
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

AI assistance was used to draft this initial code and documentation. No external
renderer or research implementation is included at this stage. Verification
results are recorded separately in `../AI.md`, outside this Git repository.
