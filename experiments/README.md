# Experiment evidence and reproduction

The project author selected **level-search/recorded/result.md** as the primary result set. All recorded files below are frozen copies of the original workspace evidence. They preserve original paths and machine metadata for provenance; use the `reproduce/` tools for the current checkout. No executable, package download, cache, private AI log or instructor PDF is included.

| Folder | Contents and role |
|---|---|
| [level-search/recorded/](level-search/recorded/result.md) | Primary fresh per-count search: 42 screening runs and 64 independent confirmation runs. Includes summary tables, unrounded CSV/JSON, selection/order/protocol, restoration proof and raw logs/clock traces. |
| [count-ablation/recorded/](count-ablation/recorded/result.md) | Supplementary fixed-L3 comparison: 24 runs at 102,400 / 409,600 / 1,638,400 spheres. |
| [six-million/recorded/](six-million/recorded/result.md) | Supplementary fixed-L3 extension: 8 runs at 6,553,600 spheres; combined historical table kept separately. |
| [renderdoc/recorded/](renderdoc/recorded/result.md) | Two `.rdc` captures, replay PNGs, indirect-buffer dumps, protocol and replay/correctness verification. |
| [level-search/reproduce/](level-search/reproduce/) | Current-checkout benchmark harnesses, clock calibration/guard, selection/frame-parser checks, equivalent-level CPU check, runner and analyzer. |
| [renderdoc/reproduce/](renderdoc/reproduce/) | Current-checkout capture harness, launch/replay scripts and licensed RenderDoc API header. |

Within every `recorded/` directory, `raw/` holds per-run stdout/stderr and clock CSVs. `provenance/` holds the exact original harness/scripts and their source notices. Those old scripts are historical inputs, not the entry points for a new run. Earlier failed/authentication attempts remain in the external workspace archive rather than the selected submission evidence.

## Verify recorded evidence without a GPU

From the repository root, with Python 3 (standard library only):

```sh
python3 experiments/verify_evidence.py
python3 experiments/test_verify_evidence.py
```

`manifest.json` records the SHA-256 and size of every frozen file. Verification also checks all 138 run records against clock traces/oracle logs, recalculates reported medians/ranges/reductions from per-run means, and checks both replayed indirect buffers. It does not rerun GPU work or assert that the measurements generalize to other hardware.

## Build the reproduction tools

Linux only; use the renderer's existing CMake/Vulkan/glslc/GLFW/GLM dependencies. Python 3 and a graphical desktop are needed to run experiments. These are opt-in build targets; normal application builds do not build the experiment executables.

```sh
cmake -S . -B build
cmake --build build --target experiment_tools -j 4
```

Executables are generated in `build/experiments/`. The benchmark targets explicitly use `-O3` and `NDEBUG`; timestamps come from the same production renderer modules and shaders. Scene setup in the external harness is a frozen snapshot of the measured scene, separate from normal preview behavior.

## Rerun the primary level search

The recorded target is Ryzen AI 7 350 / Radeon 860M, Fedora 44, Mesa RADV 26.1.8, device Vulkan 1.4.354, AC power and 1536×864 drawable size. The clock guard is deliberately specific to AMD `1002:1114` at `/sys/class/drm/card1/device`, original auto/600–3000MHz, and the ACAD/BAT0 paths. It refuses a different device/state; this is not a generic clock-lock tool for other GPUs. Verify OS/driver/device metadata before reproducing on a changed machine.

```sh
python3 experiments/level-search/reproduce/run_levels.py build/runs/level-search-01
python3 experiments/level-search/reproduce/summarize.py build/runs/level-search-01
```

The output directory must not exist. New files go under the ignored `build/` tree, so recorded evidence is not overwritten. Preflight runs CPU/parser/calibration/rollback checks before desktop PolicyKit authentication. Only the clock guard runs privileged; renderers remain unprivileged. The guard restores original limits on completion, controller exit/EOF or a 1800-second deadline.

Each workload warms up for at least 5 seconds, then requires at least a 1-second observed window within 1990–2010MHz and ≤10MHz spread. Clocks are monitored during measurement. A deviation, resize, timeout, missing command oracle or query count invalidates the run. Screening covers every distinct group size; shader-read equivalence of larger one-group levels is checked on the CPU. The 6.55-million screening uses 36 angles to bound pathological group runtime; other screening and all independent four-round confirmation use 180 angles. Screening is never pooled into confirmation medians.

The script retains the original target-machine fields as a protocol template and records current binary/source/shader hashes. Selected device/API and actual extent are printed in every run log. Use the recorded tables for the reported results; rebuilt binaries have different build-path/compiler hashes and create a new experiment session.

## Capture and replay

Install an official RenderDoc 1.45 or compatible newer build separately. The RenderDoc program itself is not vendored. On Fedora, the default library path is `/usr/lib64/renderdoc/librenderdoc.so` and `renderdoccmd` is found on PATH. For a portable build, provide its actual paths explicitly:

```sh
RENDERDOC_COMMAND=/path/to/renderdoccmd \
RENDERDOC_LIBRARY=/path/to/librenderdoc.so \
python3 experiments/renderdoc/reproduce/run_captures.py build/runs/captures-01

CG_REPLAY_DIR="$PWD/build/runs/captures-01" \
qrenderdoc --python "$PWD/experiments/renderdoc/reproduce/replay_captures.py"
```

The new output directory must not exist. Process-local layer/XDG paths avoid stale user registration, and X11/XWayland is selected for these capture processes. No persistent layer or desktop settings are changed. The capture harness uses 6,400 spheres, yaw35°, captures frame3 and exits after frame4. It compares fixed-slot commands against the CPU reference. Replay verifies recorded compute/barriers/indirect draw, all 6,400 commands and identical buffer data, and exports both color attachments. For the recorded files, open `renderdoc/recorded/per-object_capture.rdc` or `grouped_capture.rdc` directly in RenderDoc. Copy them to a new output directory before running the replay script if you want fresh exported files.

## Interpretation and source notices

- Reported numbers are **medians of four per-run means**, each mean covering 180 measured frames. The lowest observed candidate median is not proof of a statistically unique global optimum. Read ranges and paired reductions too.
- GPU elapsed intervals can include dependency waits and overlap. Do not sum cull+draw or interpret total as FPS. FIFO is unchanged; memory clocks, package power and desktop activity were not controlled. Capture overhead is excluded from timings.
- The main search optimizes group size per count. Keep the separate fixed-L3 studies when interpreting the object-count factor; never pool sessions or describe a changing-group comparison as count-only ablation.
- The project author chooses the design, evaluates the results and controls proposal content. The assistant executed the agreed measurements and prepared this evidence package.
- `renderdoc/reproduce/renderdoc_app.h` retains Baldur Karlsson's complete MIT notice, as does its provenance copy. Upstream: https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/api/app/renderdoc_app.h. RenderDoc tool/package sources are recorded in the capture protocol. Project-wide license selection and final AI-use disclosure remain separate submission work.
