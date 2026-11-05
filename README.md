# GPU Frustum Culling and Indirect Draw in Vulkan

A C++20 / Vulkan project for GPU frustum culling and indirect drawing.
The current version renders a checkerboard floor with a rotating camera; culling and indirect drawing are not implemented yet.

## Current behavior

- Inspects physical devices and queue families, then selects a GPU with graphics, compute, and presentation support in one queue family.
- Creates a window surface, logical device, swapchain, and image views.
- Draws a 20 × 20 metre floor with 1-metre gray checker cells over a dark-blue background using dynamic rendering and an indexed graphics pipeline.
- Uploads floor vertices and indices into host-visible buffer memory and sends the camera's view-projection matrix to the vertex shader through push constants.
- Rotates the camera in place once every ten seconds of active preview time, keeping its position at (0, 2, 0) and downward pitch at 20 degrees.
- Updates perspective for the current drawable size; minimize/zero-size pauses rendering and rotation, and restore resumes the same rotation phase.
- Handles resize and minimize/restore, and releases resources on exit. Press Escape or close the window to quit.

## Dependencies

- CMake 3.24+ and a C++20 compiler.
- Vulkan 1.3+ headers and loader (development package or Vulkan SDK), plus a compatible GPU driver.
- `glslc` shader compiler (from the Vulkan SDK or the platform's shaderc package).
- A non-CPU Vulkan device supporting dynamic rendering, synchronization2, and `VK_KHR_swapchain`.
- GLFW 3.4: CMake uses installed 3.4.0 or fetches the upstream 3.4 tag. Fetching requires Git/network access; building GLFW also needs the platform's window-system development packages.
- GLM 1.0.1+: CMake uses an installed package or fetches the upstream 1.0.1 tag, which requires Git/network access.
- A graphical desktop session to run.

## Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build --config Debug
```

The build compiles `shaders/floor.vert` and `shaders/floor.frag` into SPIR-V files in `build/shaders/`.
The executable loads shaders from the absolute build path recorded by CMake, so it can be launched from another working directory. Reconfigure and rebuild if relocating the build tree.

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
| `src/main.cpp` | Floor/camera resources, shader loading, graphics pipeline setup, window/instance/surface setup, event loop, resize coordination, and shutdown. |
| `src/device.hpp / .cpp` | Device inspection, GPU selection, and logical-device/queue creation. |
| `src/swapchain.hpp / .cpp` | Swapchain state, images, image views, and cleanup. |
| `src/frame.hpp / .cpp` | Frame resources, command recording, synchronization, and presentation. |
| `src/vulkan_utils.hpp` | Shared Vulkan result-checking helper. |
| `shaders/floor.vert / .frag` | Camera transformation and procedural checkerboard color. |
| `CMakeLists.txt` | Executable sources, dependencies, shader compilation, and compiler options. |
