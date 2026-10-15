# GPU Culling in Vulkan

## Current milestone: S1 - window foundation

A small C++20 application that creates a 960 x 540 GLFW window, processes events,
and releases its resources on exit. Press Escape or use the window's close button
to exit. The event loop waits between events instead of busy-spinning.

This milestone does **not** initialize Vulkan or draw an image. In particular,
some window systems may not display the window until a rendering surface presents
its first image. S2 will add a Vulkan instance, device, swapchain, and clear frame.
S1 alone is not the completed Vulkan skeleton required for D1.

## Build

Requires CMake 3.24+, a C++20 compiler, and a graphical desktop session to run.
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

Run with a Visual Studio multi-configuration build on Windows:

```powershell
.\build\Debug\gpu_culling.exe
```

For a short startup/shutdown check in a desktop session, append `--smoke-test`.
It creates the same window and exits normally after about half a second.
This is not a rendering or Vulkan correctness test.

## Code and attribution

- `src/main.cpp`: startup, window, event loop, and cleanup in one file.
- `CMakeLists.txt`: build target and GLFW dependency.
- GLFW: <https://github.com/glfw/glfw/tree/3.4>, zlib/libpng license.
- API reference: <https://www.glfw.org/docs/latest/vulkan_guide.html>.

AI assistance was used to draft this initial code and documentation. No external
renderer or research implementation is included at this stage. Verification
results are recorded separately in `../AI.md`, outside this Git repository.
