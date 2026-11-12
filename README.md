# GPU Frustum Culling and Indirect Draw in Vulkan

A C++20 / Vulkan project for GPU frustum culling and indirect drawing.
The current version renders a checkerboard floor and a configurable sphere grid (400 spheres by default) with individual or grouped GPU frustum culling and the same fixed-slot indexed indirect drawing.

## Current behavior

- Inspects physical devices and queue families, then selects a GPU with graphics, compute, and presentation support in one queue family.
- Creates a window surface, logical device, swapchain, and image views.
- Draws a 20 × 20 metre floor with 1-metre gray checker cells over a dark-blue background using dynamic rendering and an indexed graphics pipeline.
- Prepares sphere positions on the CPU in a centered square grid (20 × 20 by default), with 1-metre center spacing and radius 0.25 metres. `--grid-side N` selects N × N spheres. Centers sit 0.25 metres above the floor plane.
- Generates one shared sphere mesh: 16 slices and 8 stacks, giving 114 local-space vertices and 224 triangles with outward winding. Uses a second vertex binding for per-instance positions and simple fixed directional lighting.
- Extracts six normalized frustum planes from the camera's Vulkan zero-to-one-depth view-projection matrix. A compute shader tests each sphere and rewrites one fixed indirect slot per object, setting `instanceCount` to 0 or 1 and preserving the object ID in `firstInstance`.
- `--grouped` selects a second compute shader: one workgroup tests each group AABB, then its lanes fine-test members only if the group survives. Rejected groups still clear every member's command slot. The default remains per-object testing.
- Builds static groups from spatial grid blocks (4 × 4 by default, giving 25 groups in the default scene), including partial edge blocks and full sphere radii in each AABB. `--group-level L` doubles the block side with each level. An explicit member list preserves original object IDs without sorting or moving position data; the default group size is not a measured optimum.
- Synchronizes compute writes before `vkCmdDrawIndexedIndirect` consumes one slot per sphere. Both modes keep the same draw path and scene; there is no compaction or occlusion culling. The floor is drawn directly.
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
- A non-CPU Vulkan device supporting dynamic rendering, synchronization2, `VK_KHR_swapchain`, `multiDrawIndirect`, `drawIndirectFirstInstance`, and the requested number of indirect draws per call (400 by default). The selected shared queue must support timestamps (`timestampValidBits > 0`) with positive `timestampPeriod`. The grid must also fit compute dispatch and storage-buffer limits. Unsupported sizes are rejected before allocating the CPU scene.
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

Clean-source verification (12 November 2026): a new Release build from Git-visible working-tree files, with no existing build directory/cache, passed the renderer build, all three CTests, the optional `experiment_tools` build and evidence verification. Both per-object/grouped GPU smoke tests also passed with validation when launched from `/tmp`, using the new build's shaders. This was checked on Fedora 44/Radeon 860M with system-installed development packages; Windows and dependency-fetch fallback were not revalidated.

## Run

Linux:

```sh
./build/gpu_culling
```

To select grouped culling with the same scene and drawing path:

```sh
./build/gpu_culling --grouped
```

To draw a 40 × 40 grid (1,600 spheres) without rebuilding:

```sh
./build/gpu_culling --grid-side 40
./build/gpu_culling --grid-side 40 --grouped
```

`--grid-side` accepts an integer from 1 to 65535; the actual maximum depends on GPU limits and available memory. Omitting it keeps 20 × 20. Spacing, radius, camera and the 20-metre floor stay fixed, so larger grids extend beyond the floor and can extend beyond the camera's 100-metre far plane.

To use larger groups while keeping the same spheres:

```sh
./build/gpu_culling --grid-side 40 --grouped --group-level 2
```

`--group-level` defaults to 1 and accepts 1–15. Each group spans up to `4 × 2^(L-1)` rows and columns: level 1 = 4 × 4, level 2 = 8 × 8, level 3 = 16 × 16, and level 7 = 256 × 256. Level 15 spans every supported grid. A block larger than the grid produces one group. Bounds are recomputed from the actual members, including sphere radii; the spheres stay in place. Use `--grouped` to enable group tests; per-object mode ignores group bounds.

Larger groups can reduce parallelism and require more member tests when a coarse box survives, so increasing the level does not guarantee a speedup.

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

Normal preview logging does not discard warm-up or export CSV. The separate [experiment package](experiments/README.md) contains the controlled protocol, frozen results, raw clock/log evidence and opt-in reproduction tools.

## Recorded experiments

The selected [primary results](experiments/level-search/recorded/result.md) cover a fresh per-count group-level search: 42 screening runs and 64 independent confirmation runs on Radeon 860M/RADV. Fixed-L3 count studies and two verified RenderDoc captures are retained separately in `experiments/`.

Run `python3 experiments/verify_evidence.py` to check the archived files, clock/oracle records and statistics without a GPU. See [experiments/README.md](experiments/README.md) for the folder inventory, measured machine, limitations and commands for rebuilding/rerunning the tools. New runs go under `build/` and do not overwrite the recorded data.

## License and AI assistance

Original project code and documentation use the [MIT License](LICENSE). Upstream material retains its own terms; see [third-party notices and research credits](THIRD_PARTY_NOTICES.md). The [AI-use log](AI_USE.md) records contribution roles, assistance and verification, with design decisions and analysis attributed to the project author.

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
| `tests/grid_arguments_test.cmake` | Reject invalid or missing grid dimensions and group levels before window creation. |
| `CMakeLists.txt` | Executable sources, dependencies, shader compilation, and compiler options. |
