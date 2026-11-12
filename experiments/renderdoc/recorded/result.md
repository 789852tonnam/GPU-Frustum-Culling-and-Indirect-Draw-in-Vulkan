# RenderDoc capture verification

Both requested rendering methods have a saved, successfully replayed `.rdc` file. Captures are correctness evidence and are excluded from the performance measurements.

| Check | Per-object | Grouped |
|---|---:|---:|
| Capture | per-object_capture.rdc | grouped_capture.rdc |
| Scene | 6,400 spheres | 6,400 spheres |
| Compute dispatch at event 11 | 100 × 1 × 1 | 25 × 1 × 1 |
| Group size | Not used | 16 × 16 spheres |
| Fixed indirect command slots | 6,400 | 6,400 |
| Visible / rejected | 1,623 / 4,777 | 1,623 / 4,777 |
| Indexed indirect API calls | 1 | 1 |
| Recorded synchronization2 barriers | 4 | 4 |
| Replay debug messages | 0 | 0 |

All 128,000 bytes of indirect-command data match between the replayed captures. Every command has indexCount=672, instanceCount=0/1, firstIndex=0, vertexOffset=0 and firstInstance equal to its original object ID. The final command oracle passes against the production CPU reference in both capture runs. Camera yaw remains 35° for all four submitted frames, so the captured third frame and the oracle's final frame use identical scene, projection and visibility inputs.

The exported color images are pixel-identical at 960 × 540, verified by comparing complete RGBA byte arrays. See `per-object-replay.png`, `grouped-replay.png`, `image-comparison.json` and `replay-results.json`.

## Opening and inspecting a capture

Open either `.rdc` in RenderDoc 1.45 or a compatible newer version. The portable tool used here remains under `/tmp/cg-renderdoc-fedora`; it is temporary, so install an official RenderDoc build separately if it is later removed.

For the currently extracted tool:

```sh
LD_LIBRARY_PATH=/tmp/cg-renderdoc-fedora/usr/lib64/renderdoc \
  /tmp/cg-renderdoc-fedora/usr/bin/qrenderdoc \
  /home/tonumi/Desktop/CGTermProject/docs/verification/renderdoc-captures/grouped_capture.rdc
```

1. Select compute dispatch event 11. Compare dispatch dimensions and the bound compute shader between files.
2. Inspect the 128,000-byte indirect buffer after dispatch as 20-byte records: uint indexCount, uint instanceCount, uint firstIndex, int vertexOffset, uint firstInstance.
3. Inspect the following graphics events. A single indexed indirect call consumes all 6,400 slots; RenderDoc expands this into individual draw actions. Zero-instance commands remain in the list.
4. Inspect the color/depth attachments and exported replay images. There is also one directly drawn checkerboard floor.

## Capture protocol and limits

- Same production renderer modules and SPIR-V shaders; an external main only fixes yaw, starts/ends capture around frame 3, and exits after frame 4. No application source changes.
- Static 80 × 80 sphere grid, radius 0.25 m, spacing 1 m; same camera and draw path. Captures deliberately use a smaller scene than the timing experiment to make inspection manageable.
- RenderDoc 1.45 from the official Fedora 44 package; `rpmkeys --checksig` returned `digests signatures OK`. Download URL and package/source hashes are in `protocol.json`.
- The installed user-layer registration referenced a missing file. Process-local XDG/Vulkan search paths avoid that stale registration. The user's registration and desktop session were not changed. X11/XWayland was selected for these capture processes because this RenderDoc build supports that path; timing runs retain their original window path and 1536 × 864 extent.
- Khronos validation and sync validation were requested for capture runs; final capture logs contain no VUID, validation-error or synchronization-hazard markers. Replay debug-message lists are empty. Replayed commands, API-call counts and output equality were checked by `replay_captures.py`.
- These two fixed-camera frames do not prove correctness for every camera, scene or device. Existing CPU boundary tests and GPU smoke checks provide additional coverage. Capture timestamps are not benchmark results.

Files: `capture_main.cpp`, `renderdoc_app.h`, `run_captures.py`, `replay_captures.py`, `protocol.json`, stdout/stderr logs, `captures.json`, replayed buffers/images and verification JSON.

Official tool source: https://github.com/baldurk/renderdoc
