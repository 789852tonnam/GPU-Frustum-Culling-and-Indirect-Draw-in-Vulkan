# GPU Frustum Culling and Indirect Draw in Vulkan

A C++20 / Vulkan project for GPU frustum culling and indirect drawing.
The current version is a working Vulkan window skeleton; culling and scene geometry are not implemented yet.

## Current behavior

- Inspects physical devices and queue families, then selects a GPU with graphics, compute, and presentation support in one queue family.
- Creates a window surface, logical device, swapchain, and image views.
- Clears the window dark blue using dynamic rendering and presents frames.
- Handles resize and minimize/restore, and releases resources on exit. Press Escape or close the window to quit.

## Dependencies

- CMake 3.24+ and a C++20 compiler.
- Vulkan 1.3+ headers and loader (development package or Vulkan SDK), plus a compatible GPU driver.
- A non-CPU Vulkan device supporting dynamic rendering, synchronization2, and `VK_KHR_swapchain`.
- GLFW 3.4: CMake uses installed 3.4.0 or fetches the upstream 3.4 tag. Fetching requires Git/network access; building GLFW also needs the platform's window-system development packages.
- A graphical desktop session to run.

## Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build --config Debug
```

## Run

Linux:

```sh
./build/gpu_culling
```

To select X11/XWayland when available:

```sh
XDG_SESSION_TYPE=x11 ./build/gpu_culling
```

Windows with a Visual Studio multi-configuration build:

```powershell
.\build\Debug\gpu_culling.exe
```

Append `--smoke-test` for a short rendering and shutdown check.

## Source layout

| File | Responsibility |
|---|---|
| `src/main.cpp` | Window/instance/surface setup, event loop, resize coordination, and shutdown. |
| `src/device.hpp / .cpp` | Device inspection, GPU selection, and logical-device/queue creation. |
| `src/swapchain.hpp / .cpp` | Swapchain state, images, image views, and cleanup. |
| `src/frame.hpp / .cpp` | Frame resources, command recording, synchronization, and presentation. |
| `src/vulkan_utils.hpp` | Shared Vulkan result-checking helper. |
| `CMakeLists.txt` | Executable sources, dependencies, and compiler options. |
