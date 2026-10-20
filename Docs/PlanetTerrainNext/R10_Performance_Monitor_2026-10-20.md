# R10 Performance Monitor — 2026-10-20

## Capture

- Branch: `feature/planet-terrain-next-fine-tune`
- Build: `SingularityEditor Win64 Development`, successful after the editor was closed.
- Editor process: `UnrealEditor` PID 24476.
- Capture window: 22:02:52–22:03:52 Europe/Zagreb.
- Sampling interval: 5 seconds.

## Observed runtime signals

| Signal | Observation |
|---|---:|
| Process CPU delta per 5 s | 5.6–8.2 CPU seconds |
| Approximate total CPU load | 112–164% of one logical core; about 130% over the sample |
| Working set | 3,361–3,363 MB, ending at 3,339 MB |
| Missing disk-tile warnings | 0 during the capture |
| Terrain initialization | 16.9 ms; 6 roots; 512 GPU slots; 512×512 detail textures |
| Terrain assertions/fatal errors | None observed |

The previous run produced 388 `LogStreaming` warnings for expected disk-cache misses. After the `FileExists` guard in `PlanetTerrainNextDiskTileStore::TryLoad`, the live capture produced no new missing-tile warnings. This removes warning formatting and log I/O from the terrain miss path, but the remaining CPU load shows that traversal/build/render work still needs frame-level profiling.

## What this capture does not establish

The terrain performance HUD reports frame P50/P95/P99, Game, Draw, RHI, GPU, visible patches, mesh creates/updates, CPU morph uploads, and detail upload cost through on-screen debug messages. Those values are not written to `Singularity.log`, so this process/log capture cannot substitute for Unreal Insights or a recorded HUD sample. No FPS or frame-time recovery claim is made from this capture.

## Next analysis pass

Run the same stationary, orbital approach, high-speed descent, and ascent trajectories with the HUD visible and record one sample per trajectory. For each sample, capture:

1. Frame P50/P95/P99 and Game/Draw/RHI/GPU timings.
2. Visible patches, draw calls, mesh creates/updates/rebuilds.
3. Active CPU morph patches and morph upload milliseconds.
4. Detail bake/upload milliseconds and drawn patch count.

The zero-warning result should be retained as the baseline for those trajectory comparisons.
