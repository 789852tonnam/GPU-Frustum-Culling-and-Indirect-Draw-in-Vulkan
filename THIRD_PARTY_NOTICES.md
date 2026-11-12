# Third-party notices and research credits

The project's original code and documentation use the [MIT License](LICENSE), selected by the project author. Upstream material retains its original copyright and license. This source repository does not bundle GLFW/GLM/Vulkan libraries, the Vulkan SDK, glslc or the RenderDoc executable.

## Dependencies and copied material

| Component | Use in this project | License and retained notice |
|---|---|---|
| GLFW 3.4 | Window, input and Vulkan surface support. CMake uses an installed 3.4.0 package or the upstream `3.4` tag. | zlib-style license; [full notice](licenses/GLFW-LICENSE.md), [upstream](https://github.com/glfw/glfw/blob/3.4/LICENSE.md). Copyright Marcus Geelnard and Camilla Löwy. |
| GLM 1.0.1+ | Header-only vectors/matrices and camera mathematics. CMake's fallback tag is `1.0.1`; an installed compatible package may be newer. | Used under the offered MIT option. [Complete upstream copying notice](licenses/GLM-COPYING.txt), [upstream 1.0.1](https://github.com/g-truc/glm/blob/1.0.1/copying.txt). Copyright G-Truc Creation. |
| RenderDoc application API header | Copied header used only by the optional capture harness. The harness requests API 1.6.0. | MIT, copyright 2015-2026 Baldur Karlsson. Full notice is retained at the top of [the header](experiments/renderdoc/reproduce/renderdoc_app.h) and its frozen provenance copy. [Upstream](https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/api/app/renderdoc_app.h). |
| Vulkan-Headers and Vulkan-Loader | System-provided API headers and dynamic loader. The recorded machine used SDK packages 1.4.341.0; the GPU driver version is recorded separately. | Headers have per-file Apache-2.0/MIT terms; [upstream notice](https://github.com/KhronosGroup/Vulkan-Headers/blob/vulkan-sdk-1.4.341.0/LICENSE.md). The loader is primarily Apache-2.0 with more-permissive exceptions; [upstream notice](https://github.com/KhronosGroup/Vulkan-Loader/blob/vulkan-sdk-1.4.341.0/LICENSE.txt). These SDK/library files are not copied into this repository. |

GLFW and GLM fetched by CMake remain under the ignored build directory with their upstream notices. If redistributing a binary package with libraries or tools, include the notices for the actual bundled versions and their dependencies; the current package contains project source and recorded experiment artifacts only.

## Research and implementation references

- Joris Rijsdijk, Christoph Peters, Michael Weinmann and Ricardo Marroquim, **Gaussian Point Splatting**, TOG/SIGGRAPH 2026, section 3.5. [Project and paper](https://jorisar.nl/gaussian_point_splatting/). The project adapts the group-then-member culling mechanism to regular mesh instances and static spatial grid blocks. It does not reproduce the full Gaussian renderer or its occlusion/work-distribution system. The paper PDF and upstream Gaussian implementation are not bundled.
- **Khronos Vulkan Samples: GPU Rendering and Multi-Draw Indirect**. [Sample documentation](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html), [sample repository license](https://github.com/KhronosGroup/Vulkan-Samples/blob/main/LICENSE). Reference for fixed-slot indirect commands controlled with `instanceCount = 0/1`; it is not a runtime dependency.

The sphere mesh, regular-grid scene, shaders and experiment data are generated within this project. No downloaded scene dataset, texture, music or pretrained model is included. RenderDoc captures record this project's own scene. AI assistance is disclosed in [AI_USE.md](AI_USE.md).
