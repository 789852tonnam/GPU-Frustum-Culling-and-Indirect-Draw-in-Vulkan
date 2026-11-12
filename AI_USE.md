# AI use log and contribution roles

Submission-facing summary through 12 November 2026. The project uses OpenAI Codex as an interactive assistant. This document summarizes assistance relevant to the submitted work; it does not contain private conversation transcripts or credentials.

## Contribution roles

**Project author:** sets the research question and scope; selects the research mechanism; designs and chooses the scene, algorithms, experiment conditions and reported result set; analyzes and interprets the evidence; determines proposal content; reviews and explains the implementation and is responsible for the submitted work.

**AI assistant:** proposes implementation methods and tradeoffs for the author to consider; helps write/refactor/comment code under the agreed design; helps run builds, correctness checks and the agreed experiment protocol; computes reproducible statistical summaries; organizes evidence; and edits/formats proposal text according to the author's instructions. Numerical summaries and explanations produced with assistance remain subject to the author's analysis and review.

## Activity log

| Period | Author's direction and decisions | AI assistance and verification |
|---|---|---|
| 15-16 October 2026 | Develop and review a small Vulkan foundation in readable stages. | Helped implement GLFW/CMake setup, instance/device/queue/surface/swapchain setup and cleanup; ran build and initialization/lifecycle checks. These early checks alone do not validate the later renderer. |
| Early November 2026 | Review the checkerboard/camera code; choose a regular sphere-grid scene and retain readable English comments. | Helped implement mesh/instance buffers, shared upload helpers, graphics pipelines and depth attachment; explained code and added requested section comments. |
| 6-11 November 2026 | Implement GPU per-object culling/indirect drawing, then grouped culling and timestamps. Select the two-method comparison and exclude compaction/sorting. | Helped write GLSL/C++ modules, synchronization and CPU references; ran CTest and GPU smoke/lifecycle/failure checks. Added runtime grid/group arguments under the author's selected definitions. |
| 11-12 November 2026 | Choose object counts and group sizes, require controlled clocks, reject assuming one group's optimum applies to all counts, and select the final result set. | Executed the agreed measurements and deterministic summaries. The primary archive contains 42 screening plus 64 independent confirmation runs. Separate fixed-L3 count studies contain 24+8 runs. Raw logs, clock traces and final-command CPU checks are retained. |
| 12 November 2026 | Request RenderDoc evidence and confirm the latest per-count search as the primary results; use L5 for the 1,638,400-sphere result. | Captured and replayed both methods; checked 6,400 commands, matching visibility/buffers and output images. Packaged results/reproduction tools, tested archive integrity, and updated the native proposal with the author's decisions and restrained formatting. |
| 12 November 2026 | Prepare repository notices/disclosure and check a clean build; select MIT for the project's original work. | Prepared this public log, third-party notices and the selected license. Clean-build verification details are maintained in the README. |

## Evidence and review

- [Primary results and limitations](experiments/level-search/recorded/result.md), [raw run summaries](experiments/level-search/recorded/runs.json) and [protocol](experiments/level-search/recorded/protocol.json).
- [Fixed-L3 count ablation](experiments/count-ablation/recorded/result.md) and [six-million extension](experiments/six-million/recorded/result.md), kept separate from per-count tuning.
- [RenderDoc verification](experiments/renderdoc/recorded/result.md) and [reproduction instructions](experiments/README.md).
- CPU unit tests, GPU final-command comparisons and preserved raw evidence support correctness claims. AI explanations alone are not validation. The final-command oracle does not check every measured frame.

The reported metric is a median of four per-run mean GPU elapsed intervals. Per-metric candidate selection, overlapping ranges, dependencies, memory/power/desktop variability and single-machine scope are disclosed with the results. The measurements do not establish a unique global optimum or a guaranteed FPS gain. Current full-renderer/benchmark evidence is from Radeon 860M on Linux; earlier Windows skeleton checks are not validation of the current full experiment.

The author retains control of the design, analysis and proposal. This log records assistance transparently without attributing those decisions to the assistant. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for upstream credits and materials.
