# GPU Culling in Vulkan

## Current milestone: S2.1 - Vulkan instance

A small C++20 application that creates a 960 x 540 GLFW window, processes events,
and creates a Vulkan instance requesting API 1.3. The instance enables the window
system extensions supplied by GLFW and is destroyed when the application exits.
Press Escape or use the window's close button to exit when the window is visible.
The event loop waits between events instead of busy-spinning.

This milestone does not select a GPU or create a device, Vulkan surface, swapchain,
or rendered image. An instance alone does not prove device support for API 1.3.
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
shutdown prints `Vulkan instance destroyed.` This is not a rendering or GPU
performance test.

On Linux with the Khronos validation layer installed, an instance lifecycle check
can also be run with:

```sh
XDG_SESSION_TYPE=x11 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/gpu_culling --smoke-test
```

This enables validation for this run through the Vulkan loader. The application
does not yet install a debug messenger of its own.

## Code and attribution

- `src/main.cpp`: window, Vulkan instance, event loop, and cleanup in one file.
- `CMakeLists.txt`: build target, GLFW dependency, and Vulkan loader linkage.
- GLFW: <https://github.com/glfw/glfw/tree/3.4>, zlib/libpng license.
- API reference: <https://www.glfw.org/docs/latest/vulkan_guide.html>.
- Vulkan instance reference: <https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateInstance.html>.

AI assistance was used to draft this initial code and documentation. No external
renderer or research implementation is included at this stage. Verification
results are recorded separately in `../AI.md`, outside this Git repository.
