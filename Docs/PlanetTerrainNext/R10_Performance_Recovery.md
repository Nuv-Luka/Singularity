# R10 Performance Recovery

**Branch:** `feature/planet-terrain-next`  
**Engine:** Unreal Engine 5.8  
**Status:** Source changes implemented; runtime trajectory profiling still required.

## Findings

- `ReleaseNode()` cleared each pooled ProceduralMeshComponent section. `ForceFreshMeshSection` also defaulted to true, so uploads repeatedly called `ClearMeshSection` and `CreateMeshSection`, rebuilding section render resources.
- With legacy morph disabled by the current R10.2 settings, `BuildNode()` still classified L0–L2 patches for CPU morph. Those patches retained fine/coarse arrays and performed an initial temporary-array upload before `ApplyMorph()` restored fine geometry.
- Detail baking evaluated the height field four extra times per texel for finite-difference normals. Detail textures contained only a base mip and were uploaded in face-number order.
- No `.utrace` capture or saved stationary/approach/descent run was present in `Saved`. The latest editor log reports tracing into the in-memory cache, so there is no measured baseline for Game, Draw, RHI, GPU, or frame-time percentiles. The reported ~90% regression remains the user's estimate until comparable captures are collected.

## Changes

- Released slots retain their mesh section. Reuse requires matching index-buffer version, vertex count, and index count; incompatible sections are cleared and recreated with the current shared index buffer. Forced recreation now defaults off.
- CPU morph is enabled only when legacy morph is enabled. The current default leaves patches static and uses the existing material-based LOD fade path; unsafe GPU morph encodings and patch geometry were not changed.
- Detail workers now cache a one-texel height border and bilinearly sample it at the existing ±0.45-texel normal offsets. The same workers generate deterministic albedo and renormalized normal mip chains. Upload scheduling prioritizes cube faces by camera-facing direction and remains limited by `DetailUploadsPerFrame`.
- Stable patches skip unchanged `LodFade` and `LodFadeRole` writes. Insights CPU scopes were added for terrain ticks, patch uploads, CPU morph uploads, detail baking, and detail texture upload.
- The on-screen performance line reports rolling frame-time P50/P95/P99 from the component's observed `DeltaTime`, drawn patch count, section creates/updates/clears, section resource rebuild calls, active CPU morph patches/uploads/time, and detail bake/upload timings. Use Unreal Insights for exact Draw/RHI/GPU timings and draw calls.

## Validation completed

- `SingularityEditor Win64 Development` build: **passed**.
- R10 automation tests: **4 passed**, including flat and heighted L0–L2 geometry and complementary LOD fades.
- Detail bake test: **passed**. Repeated base levels and mip chains are deterministic. Against the former finite-difference implementation, sampled normal encoding differs by at most **2/255** per channel and averages **0.036/255** over 192 sampled channels.
- Visual comparison in the running viewport and continuous camera trajectories: **not captured**. Unreal Editor screen capture timed out in this session, and no prior trace was available.

## Required runtime comparison

Capture the previous visually correct build and this build on the same machine, map, resolution, scalability, and camera path. Use separate trace bookmarks or trace files for:

1. Stationary orbital view after streaming settles.
2. Orbital approach at a fixed speed through the same altitude interval.
3. Continuous descent and ascent through the same altitude interval, including streaming transitions.

Record at least 30 seconds per segment. Enable CPU, frame, rendering, RHI, and GPU trace channels. Collect `stat unit`, `stat rhi`, and `stat gpu`; then report frame-time P50/P95/P99, Game/Draw/RHI/GPU timings, drawn patches, actual draw calls, shadow cost, and overdraw from the selected Insights ranges. Also record section creates/updates/rebuilds, active CPU morph patches and uploads, detail bake/upload cost, and visual screenshots at matching camera positions. Current numeric FPS and subsystem timings remain **pending these captures**.
