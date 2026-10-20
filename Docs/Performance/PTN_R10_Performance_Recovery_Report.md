# PTN R10 Performance Recovery Report

Status: measurement stage in progress. No new production optimization has been accepted from this guide yet.

## R10 recovery milestone: Game-thread stall removal

The descent evidence is **[USER-REPORTED]**: Frame, Game and Input spike together while GPU timings do not. The first implementation milestone therefore targets synchronous Game-thread work before material or shader changes.

- `BuildNode()` now permits `GetOrBake()` only during the explicit six-root `InitializeTerrain()` bootstrap. Runtime refinement consumes resident CPU tiles and leaves the existing parent covering until asynchronous scheduler completion.
- `CookCollisionNode()` no longer bakes or loads missing height tiles synchronously. Collision demand is added to the bounded tile scheduler, then cooked only after a tile is resident. Local collision is released above the configurable `CollisionMaxAltitudeM` threshold (10 km default).
- `TileCache::Insert()` now places worker results in memory immediately and queues bounded, deduplicated persistence jobs. Serialization, pruning and file writes run on the disk worker. The synchronous disk path remains limited to bootstrap `GetOrBake()` compatibility.
- Mesh commits are bounded by `MeshCommitsPerFrame` and the existing build-time budget. Unreal Insights scopes now distinguish BuildNode, cache lookup/insert, background disk save/prune, collision cook and mesh-section upload work.
- Shadow writes use one cached reconciliation path, and idle fade processing only visits active transition participants.
- Terrain material generation now exposes a stable v6 opaque, single-sided, WPO-free variant and retains v5 as the masked transition variant. Reusable slots keep both MIDs; only active fades switch to the transition MID, then restore the stable MID at completion. Detail, color and morph parameters are synchronized between both variants.
- `singularity.TerrainNext.EmergencyMode` is an opt-in startup preset that disables terrain shadows, dithered transitions, disk cache writes and collision cooks, and caps patches at 64 while retaining detail projection. It defaults off.

This milestone preserves pooled sections, corrected L1/L2 geometry, quadtree coverage, geological detail projection and the existing fade state machine. It is not a performance recovery claim until a responsive build can be profiled again.

### S2 high-speed descent diagnosis [MEASURED LOCALLY]

The user capture `S2_HighSpeedDescent_Run1` confirms the reported Game-thread stall pattern. Across 549 frames, terrain frame time was median **22.498 ms**, P95/P99 **400 ms**, with **64 frames at or above 100 ms**. The terrain tick `UpdateMs` was median **0.055 ms**, but P95 **802.230 ms** and P99 **1577.241 ms**. The scheduler phase accounted for most of those stalls: `ScheduleMs` P95 **606.357 ms** and P99 **1261.788 ms**, while `MeshBuildMs` P95 was only **2.159 ms** and `CollisionMs` P95 **0.002 ms**. This rules out mesh upload, collision cooking, and GPU work as the primary cause of the observed descent hitches in this run.

The worst frames reached `UpdateMs` **2200.685 ms** / `ScheduleMs` **1820.126 ms** while the scheduler drained newly completed tiles. S2 recorded 222 worker tile bakes and 262 queued disk writes. The source path showed that `TileCache::Insert()` called `DiskTileStore::EnqueueSave()` while the background `Save()` worker held the store mutex across directory enumeration, pruning, serialization, and file I/O. The enqueue therefore remained asynchronous in name but could block the Game thread behind an in-progress save. The recovery fix snapshots the disk configuration and performs all filesystem work outside the shared mutex; the game thread now only performs bounded queue bookkeeping. Automated validation after this change passed **40/40** PlanetTerrainNext tests, including R8 disk persistence, R10 geometry/fade, R9 detail determinism, and scheduler worker stress.

This S2 file is the pre-fix capture and must not be used as an optimized-performance claim. A post-fix descent capture should specifically verify that `ScheduleMs` no longer tracks disk activity and that frame P95/P99 recover during continuous movement.

For an editor that cannot reach the console, the recovery preset can be supplied before startup with `-singularity.TerrainNext.EmergencyMode=1` (or placed in a local startup configuration). It defaults to off and should be removed for normal visual/performance comparisons.

Validation for this milestone: `SingularityEditor Win64 Development` succeeded; R10 geometry/fade automation passed 4/4 in `Saved/Logs/PlanetTerrainNextR10RecoveryMaterialTests.log`; the earlier R9 detail determinism and R8 disk-cache tests also passed after the Game-thread changes.

## Pinned revisions

- Baseline `origin/main`: `76199c35b57b0e08fda5640ddb2cce01e0de69f2`
- Candidate `origin/feature/planet-terrain-next-fine-tune`: `61af2bc838c1fab8f5e60bf4ac112737eddcfe16` at discovery
- Discovery inventory commit: `f8f3714`
- Merge base: baseline SHA; the candidate is a direct descendant.

## Measurement instrumentation

The candidate now has an opt-in per-frame CSV writer in `PlanetTerrainNextComponent.cpp::WritePerfSample`. It writes `Saved/Logs/PlanetTerrainNextPerf.csv` with scenario, frame delta, terrain tick phases, frame percentiles, patch/fade counts, mesh section lifecycle, CPU morph, detail, tile/disk, GPU residency, collision and coverage fields.

Capture is disabled by default and does not add file I/O when off. Set these console variables before a run:

```text
singularity.TerrainNext.PerfScenario S0_OrbitalStationary_Run1
singularity.TerrainNext.PerfCapture 1
```

Stop and flush the capture after the measured window:

```text
singularity.TerrainNext.PerfCapture 0
```

Use a new scenario label for every run. Keep `stat unit`, `stat gpu`, `stat rhi`, `stat scenerendering`, and `stat streaming` visible for a separate screenshot/recording; those engine distributions are not available in the terrain CSV or normal project log.

## Current local evidence

The previous live process sample was diagnostic context only: 5.6–8.2 CPU seconds per 5-second interval, approximately 3.36 GB working set, and zero missing disk-tile warnings after the `FileExists` guard. It did not contain Game/Draw/RHI/GPU frame-time distributions and is not used as a recovery claim.

### Preliminary candidate capture [MEASURED LOCALLY]

The first live telemetry file was successfully written to `Saved/Logs/PlanetTerrainNextPerf.csv`. It contains one partial stationary sample, `S0_OrbitalStationary_Run1`, with 457 rows (about 7.6 seconds at one sample per frame). The observed frame-delta distribution was median **22.781 ms** (about **43.9 FPS**), P95 **400 ms**, and P99 **400 ms**. There were **83 frames at or above 100 ms**, so this short run contains severe hitching and is not sufficient to characterize steady-state performance. The sample reported 6 drawn/resident patches, 0 mesh creations, 0 mesh updates, 0 active CPU-morph patches, 0 detail uploads, and 0 detail-bake activity during the captured window.

This is candidate-only evidence. It does not provide the required Game/Draw/RHI/GPU stat distributions, does not cover approach/descent/traversal/ascent, and is not an A/B recovery claim. Repeat each scenario for a 30–60 second steady window and capture the matching baseline before assigning a regression cause.

The next candidate sample, `S1_OrbitalApproach_Run1`, contains 493 rows (about 8.2 seconds). Its frame-delta median is **16.669 ms**, P95 **35.308 ms**, and P99 **400 ms**, with 22 frames at or above 100 ms. Average drawn patches rose to **6.4**; the sample recorded **94 mesh creations**, 0 mesh updates, 0 active CPU-morph patches, and 0 detail uploads. This indicates that approach activity is causing pooled-section creation work, but the short sample and missing engine stat overlays still prevent attribution.

The current renderer has VSM enabled, masked/two-sided v5 transition material, opaque v4 asset, dithered LOD enabled, legacy CPU morph disabled, and topology-compatible PMC reuse enabled by default. See `PTN_R10_Regression_Inventory.md` for the complete source inventory.

## Safeguard validation after telemetry change

- R10 automation: 4/4 passed (`DiagCollectNodes`, `FlatGeometryL0L1L2`, `HeightGeometryL0L1L2`, `LodFadeComplement`). Log: `Saved/Logs/PlanetTerrainNextR10TelemetryTests.log`.
- R9 detail determinism: 1/1 passed (`DetailBakeDeterminism`). Log: `Saved/Logs/PlanetTerrainNextR9TelemetryTests.log`.
- Editor target build: `SingularityEditor Win64 Development` succeeded with UE 5.8.

These are correctness/build checks, not frame-time measurements.

## Required capture table

| Scenario | Build | Run | Frame P50/P95/P99 | Game P50/P95/P99 | Draw P50/P95/P99 | RHI P50/P95/P99 | GPU P50/P95/P99 | Status |
|---|---|---:|---|---|---|---|---|---|
| S0 orbital stationary | candidate | 1–3 | pending | pending | pending | pending | pending | awaiting trajectory capture |
| S1 orbital approach | candidate | 1–3 | pending | pending | pending | pending | pending | awaiting trajectory capture |
| S2 high-speed descent | candidate | 1–3 | pending | pending | pending | pending | pending | awaiting trajectory capture |
| S3 surface traversal | candidate | 1–3 | pending | pending | pending | pending | pending | awaiting trajectory capture |
| S4 ascent | candidate | 1–3 | pending | pending | pending | pending | pending | awaiting trajectory capture |

The same five scenarios must be captured against the pinned baseline or a documented fast historical SHA before assigning blame or claiming recovery. A/B changes remain unimplemented until a repeatable frame-time difference and matching Insights pass/call path identify the culprit.

## Current hypotheses, explicitly unproven

1. The permanent masked/two-sided v5 material may raise stable GPU/VSM cost; test against v4 with identical projected detail.
2. `ApplyLodFadeParams` scans every pooled slot each frame; stable-state event-driven reconciliation may reduce Game/Draw work.
3. Disk saves still hold the store mutex through whole-directory pruning; instrument save, enumeration, lock wait and prune time before changing it.
4. Detail bake/upload, collision cooks, and transition overlap may explain only moving-scenario P95/P99; they need phase-aligned captures.

No percentage attribution is made for any hypothesis.
