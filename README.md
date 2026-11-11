# GPU Frustum Culling and Indirect Draw in Vulkan

A C++20 / Vulkan project for GPU frustum culling and indirect drawing.
The current version renders a checkerboard floor and a 400-sphere grid with individual or grouped GPU frustum culling and the same fixed-slot indexed indirect drawing.

## Current behavior

- Inspects physical devices and queue families, then selects a GPU with graphics, compute, and presentation support in one queue family.
- Creates a window surface, logical device, swapchain, and image views.
- Draws a 20 × 20 metre floor with 1-metre gray checker cells over a dark-blue background using dynamic rendering and an indexed graphics pipeline.
- Prepares 400 sphere positions on the CPU in a centered 20 × 20 grid, with 1-metre center spacing and radius 0.25 metres. Centers sit 0.25 metres above the floor.
- Generates one shared sphere mesh: 16 slices and 8 stacks, giving 114 local-space vertices and 224 triangles with outward winding. Uses a second vertex binding for per-instance positions and simple fixed directional lighting.
- Extracts six normalized frustum planes from the camera's Vulkan zero-to-one-depth view-projection matrix. A compute shader tests each sphere and rewrites one fixed indirect slot per object, setting `instanceCount` to 0 or 1 and preserving the object ID in `firstInstance`.
- `--grouped` selects a second compute shader: one workgroup tests each group AABB, then its lanes fine-test members only if the group survives. Rejected groups still clear every member's command slot. The default remains per-object testing.
- Builds 25 static groups from 4 × 4 spatial grid blocks, including full sphere radii in each AABB. An explicit member list preserves original object IDs without sorting or moving position data. `sphereGroupSide` in `src/main.cpp` is the initial experiment setting, not a measured optimum.
- Synchronizes compute writes before `vkCmdDrawIndexedIndirect` consumes all 400 slots. Both modes keep the same draw path and scene; there is no compaction or occlusion culling. The floor is drawn directly.
- Uploads floor/sphere vertices, indices and sphere positions into host-visible buffer memory through a shared create/allocate/upload helper. Sends the camera's view-projection matrix through push constants.
- Uses a depth attachment for correct opaque visibility, clears it every frame and recreates it with the swapchain size. Selects a supported depth-only format (D32 float preferred, D16 fallback).
- Rotates the camera in place once every ten seconds of active preview time, keeping its position at (0, 2, 0) and downward pitch at 20 degrees.
- Updates perspective for the current drawable size; minimize/zero-size pauses rendering and rotation, and restore resumes the same rotation phase.
- Handles resize and minimize/restore, and releases resources on exit. Press Escape or close the window to quit.
- Records GPU timestamp pairs around culling and drawing, and the interval spanning both. Prints mean `cull`, `draw` and `total` milliseconds every 120 presented frames, before resize, and at shutdown. Reads completed results after the existing frame fence; no extra per-frame wait is added.
- Reports include the mode, drawable dimensions and completed query sample count. Resize flushes the old segment and starts new statistics, so results from different resolutions are not averaged together.

## Dependencies

- CMake 3.24+ and a C++20 compiler.
- Vulkan 1.3+ headers and loader (development package or Vulkan SDK), plus a compatible GPU driver.
- `glslc` shader compiler (from the Vulkan SDK or the platform's shaderc package).
- A non-CPU Vulkan device supporting dynamic rendering, synchronization2, `VK_KHR_swapchain`, `multiDrawIndirect`, `drawIndirectFirstInstance`, and at least 400 indirect draws per call. The selected shared queue must support timestamps (`timestampValidBits > 0`) with positive `timestampPeriod`. Selection rejects devices missing these requirements.
- GLFW 3.4: CMake uses installed 3.4.0 or fetches the upstream 3.4 tag. Fetching requires Git/network access; building GLFW also needs the platform's window-system development packages.
- GLM 1.0.1+: CMake uses an installed package or fetches the upstream 1.0.1 tag, which requires Git/network access.
- A graphical desktop session to run.

## Build

From the repository root:

```sh
cmake -S . -B build
cmake --build build --config Debug
```

The build compiles the floor/sphere vertex/fragment shaders and both culling compute shaders into SPIR-V files in `build/shaders/`.
The executable loads shaders from the absolute build path recorded by CMake, so it can be launched from another working directory. Reconfigure and rebuild if relocating the build tree.

## Run

Linux:

```sh
./build/gpu_culling
```

To select grouped culling with the same scene and drawing path:

```sh
./build/gpu_culling --grouped
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

Flags combine in either order, for example `./build/gpu_culling --grouped --smoke-test`. Both modes check against the same individual-sphere CPU reference, so a coarse group rejection must not remove a sphere the individual method would keep.

Run the window-free CPU checks with:

```sh
ctest --test-dir build --output-on-failure
```

## GPU timing boundaries

Four `vkCmdWriteTimestamp2` queries use TOP/BOTTOM stages: cull start, cull end, draw start and draw end. `cull` includes compute command generation and its barriers; `draw` includes attachment transitions/clears, floor and sphere rendering, and the final colour transition. `total` spans cull start through draw end. CPU fence/acquisition/presentation call time is not measured, but GPU dependencies or idle time inside an interval can be included.

These are approximate GPU elapsed intervals, not isolated shader execution costs. Pipeline work can overlap, so **do not add cull + draw to obtain total**. Results are converted using the device's nanoseconds-per-tick and the queue's valid timestamp bits, including one counter wrap. The final submitted sample is collected before resize/shutdown releases its query pool.

This is timing instrumentation, not a benchmark protocol: FIFO/vsync is unchanged, no warm-up samples are discarded, and there is no CSV export or controlled statistical comparison yet.

## Source layout

| File | Responsibility |
|---|---|
| `src/main.cpp` | Floor/sphere scene data and uploads, shader loading, graphics pipelines, window/instance/surface setup, event loop, resize coordination, and shutdown. |
| `src/device.hpp / .cpp` | Device inspection, GPU selection, and logical-device/queue creation. |
| `src/swapchain.hpp / .cpp` | Swapchain state, images, image views, and cleanup. |
| `src/frame.hpp / .cpp` | Frame/query resources, command recording, completed GPU timing collection, synchronization, and presentation. |
| `src/depth.hpp / .cpp` | Depth attachment format, image, memory, view, and cleanup. |
| `src/culling.hpp / .cpp` | Spatial group/member generation, AABBs, frustum planes, CPU reference, compute resources, synchronization and smoke-test verification. |
| `src/timing.hpp` | GPU interval totals and timestamp-counter-to-millisecond conversion. |
| `src/vulkan_utils.hpp` | Shared Vulkan result-checking helper. |
| `shaders/floor.vert / .frag` | Camera transformation and procedural checkerboard color. |
| `shaders/sphere.vert / .frag` | Per-instance translation and simple sphere lighting. |
| `shaders/cull.comp` | Per-sphere GPU frustum tests and fixed-slot indirect commands. |
| `shaders/cull_grouped.comp` | Shared group-AABB rejection followed by per-member sphere tests and fixed-slot commands. |
| `tests/culling_test.cpp` | Six-plane boundaries, tangent spheres/AABBs, camera transforms, group coverage and partial-block checks. |
| `tests/timing_test.cpp` | Timestamp period conversion, valid-bit masks, wraparound and overlapping intervals. |
| `CMakeLists.txt` | Executable sources, dependencies, shader compilation, and compiler options. |
