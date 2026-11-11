# GPU Frustum Culling and Indirect Draw in Vulkan

A C++20 / Vulkan project for GPU frustum culling and indirect drawing.
The current version renders a checkerboard floor and a 400-sphere grid with per-object GPU frustum culling and indexed indirect drawing.

## Current behavior

- Inspects physical devices and queue families, then selects a GPU with graphics, compute, and presentation support in one queue family.
- Creates a window surface, logical device, swapchain, and image views.
- Draws a 20 × 20 metre floor with 1-metre gray checker cells over a dark-blue background using dynamic rendering and an indexed graphics pipeline.
- Prepares 400 sphere positions on the CPU in a centered 20 × 20 grid, with 1-metre center spacing and radius 0.25 metres. Centers sit 0.25 metres above the floor.
- Generates one shared sphere mesh: 16 slices and 8 stacks, giving 114 local-space vertices and 224 triangles with outward winding. Uses a second vertex binding for per-instance positions and simple fixed directional lighting.
- Extracts six normalized frustum planes from the camera's Vulkan zero-to-one-depth view-projection matrix. A compute shader tests each sphere and rewrites one fixed indirect slot per object, setting `instanceCount` to 0 or 1 and preserving the object ID in `firstInstance`.
- Synchronizes compute writes before `vkCmdDrawIndexedIndirect` consumes all 400 slots. Rejected slots remain in the buffer; there is no compaction, grouped culling or occlusion culling yet. The floor is drawn directly.
- Uploads floor/sphere vertices, indices and sphere positions into host-visible buffer memory through a shared create/allocate/upload helper. Sends the camera's view-projection matrix through push constants.
- Uses a depth attachment for correct opaque visibility, clears it every frame and recreates it with the swapchain size. Selects a supported depth-only format (D32 float preferred, D16 fallback).
- Rotates the camera in place once every ten seconds of active preview time, keeping its position at (0, 2, 0) and downward pitch at 20 degrees.
- Updates perspective for the current drawable size; minimize/zero-size pauses rendering and rotation, and restore resumes the same rotation phase.
- Handles resize and minimize/restore, and releases resources on exit. Press Escape or close the window to quit.

## Dependencies

- CMake 3.24+ and a C++20 compiler.
- Vulkan 1.3+ headers and loader (development package or Vulkan SDK), plus a compatible GPU driver.
- `glslc` shader compiler (from the Vulkan SDK or the platform's shaderc package).
- A non-CPU Vulkan device supporting dynamic rendering, synchronization2, `VK_KHR_swapchain`, `multiDrawIndirect`, `drawIndirectFirstInstance`, and at least 400 indirect draws per call. Selection rejects devices missing these requirements.
- GLFW 3.4: CMake uses installed 3.4.0 or fetches the upstream 3.4 tag. Fetching requires Git/network access; building GLFW also needs the platform's window-system development packages.
- GLM 1.0.1+: CMake uses an installed package or fetches the upstream 1.0.1 tag, which requires Git/network access.
- A graphical desktop session to run.

## Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build --config Debug
```

The build compiles the floor/sphere vertex/fragment shaders and the culling compute shader into SPIR-V files in `build/shaders/`.
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

Append `--smoke-test` for a short rendering and shutdown check. After GPU work finishes, it reads the final indirect commands and checks every object's visibility and draw parameters against a CPU reference. Normal rendering does not read visibility back to the CPU. This is a correctness check, not a benchmark.

Run the window-free CPU checks with:

```sh
ctest --test-dir build --output-on-failure
```

## Source layout

| File | Responsibility |
|---|---|
| `src/main.cpp` | Floor/sphere scene data and uploads, shader loading, graphics pipelines, window/instance/surface setup, event loop, resize coordination, and shutdown. |
| `src/device.hpp / .cpp` | Device inspection, GPU selection, and logical-device/queue creation. |
| `src/swapchain.hpp / .cpp` | Swapchain state, images, image views, and cleanup. |
| `src/frame.hpp / .cpp` | Frame resources, command recording, synchronization, and presentation. |
| `src/depth.hpp / .cpp` | Depth attachment format, image, memory, view, and cleanup. |
| `src/culling.hpp / .cpp` | Frustum planes, CPU reference, compute resources, synchronization and smoke-test verification. |
| `src/vulkan_utils.hpp` | Shared Vulkan result-checking helper. |
| `shaders/floor.vert / .frag` | Camera transformation and procedural checkerboard color. |
| `shaders/sphere.vert / .frag` | Per-instance translation and simple sphere lighting. |
| `shaders/cull.comp` | Per-sphere GPU frustum tests and fixed-slot indirect commands. |
| `tests/culling_test.cpp` | Six-plane boundary, tangent-sphere, perspective and rotated-camera checks. |
| `CMakeLists.txt` | Executable sources, dependencies, shader compilation, and compiler options. |
