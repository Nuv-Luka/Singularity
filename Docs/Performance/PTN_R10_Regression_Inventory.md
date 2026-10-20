# PlanetTerrainNext R10 Regression Inventory

Prepared 2026-10-20 from the local checkout, before any new optimization edits.

## Revision provenance

| Revision | SHA |
|---|---|
| `origin/main` | `76199c35b57b0e08fda5640ddb2cce01e0de69f2` |
| `origin/feature/planet-terrain-next-fine-tune` | `61af2bc838c1fab8f5e60bf4ac112737eddcfe16` |
| Merge base | `76199c35b57b0e08fda5640ddb2cce01e0de69f2` |

The merge base equals `origin/main`; the candidate is a descendant of main, with no rebase or cherry-pick ambiguity in this comparison. The working tree was clean at the start of discovery. The exact comparison used here is `git diff BASE FINE`, not the three-dot merge-base shorthand.

The candidate contains the commits `fdb77a7`, `330ea83`, `d3c47c8`, `dbe925a`, and `61af2bc` after the merge base. The full BASE→FINE diff is 30 paths, 3,229 insertions and 162 deletions, including 2 material assets and 28 source/header paths.

## Machine and render configuration

- Engine/toolchain: UE 5.8 (`C:\Program Files\Epic Games\UE_5.8`); the candidate editor build completed successfully locally before this inventory.
- GPU: NVIDIA GeForce RTX 3050 Laptop GPU, driver 610.88, 4096 MiB. [MEASURED LOCALLY]
- CPU model: unavailable because the Windows CIM query was access denied. Core count is therefore not inferred. [MEASURED LOCALLY]
- RHI target: DX12 (`DefaultGraphicsRHI=DefaultGraphicsRHI_DX12`). [MEASURED LOCALLY]
- Virtual Shadow Maps: enabled (`r.Shadow.Virtual.Enable=1`). Static lighting is disabled. Occlusion queries and HZB occlusion are disabled; early Z is disabled. [MEASURED LOCALLY]
- Resolution, FOV, screen percentage, VSync, AA mode, ray tracing, Lumen, power state, and actual runtime RHI were not recorded by the local process/log capture. They must be pinned before A/B timing.

## Changed paths and execution phase

| Path | Relevant change | Execution phase |
|---|---|---|
| `Private/PlanetTerrainNextComponent.cpp` / `Public/PlanetTerrainNextComponent.h` | pooled PMC section reuse, dithered draw-set reconciliation, fade parameters, CPU morph counters, detail/collision/terrain telemetry | every frame; transition; tile commit |
| `Private/PlanetTerrainNextDetailStore.cpp` / `Public/PlanetTerrainNextDetailStore.h` | prioritized face jobs, texture creation/upload and mip handling | every frame; tile/detail commit |
| `Private/PlanetTerrainNextDetailTile.cpp` / `Public/PlanetTerrainNextDetailTile.h` | cached bordered height samples, normals and mip chains | detail bake job |
| `Private/PlanetTerrainNextDiskTileStore.cpp` | existence guard before load; existing whole-directory prune path retained | disk activity |
| `Private/PlanetTerrainNextLodFade.cpp` / `Public/PlanetTerrainNextLodFade.h` | parent/child fade state and render-set construction | every frame while transitions exist |
| `Private/PlanetTerrainNextMorphMaterial.cpp` / `Public/PlanetTerrainNextMorphMaterial.h` | runtime-created masked v5 material with dither, WPO and detail samples | initialization; parameter updates |
| `Content/Materials/M_PlanetTerrainNextPatch_v4.uasset` | opaque v4 material asset | initialization; stable material candidate |
| `Content/Materials/M_PlanetTerrainNextPatch_v5.uasset` | masked v5 material asset | initialization; current transition material |
| `Private/PlanetTerrainNextPatchMesh.cpp` / `Public/PlanetTerrainNextPatchMesh.h` | shared topology/patch construction changes | tile commit |
| `Private/PlanetTerrainNextQuadtree.cpp` | quadtree behavior adjustment | every frame |
| `Private/PlanetTerrainNextSettings.cpp` / `Public/PlanetTerrainNextSettings.h` | runtime flags and budgets | every frame settings read |
| `Private/PlanetTerrainNextDiag.cpp` / `Public/PlanetTerrainNextDiag.h` | frozen geometry/material diagnostic path | diagnostic mode only |
| `Private/PlanetTerrainNextMoonActor.cpp`, `Private/SPlanetTerrainNextSpawnPanel.cpp` | initialization/editor integration | initialization/editor |
| `Private/Tests/PlanetTerrainNextR2/R3/R4/R6/R8/R9/R10Tests.cpp` | automated geometry, detail, collision and R10 checks | tests only |
| `Source/Singularity/Private/Pug/TileGen.cpp` | unrelated project-side generation change in the candidate diff | project generation path |

## Current renderer and material topology

The runtime terrain patch material is `M_PlanetTerrainNextPatch_v5`: `BLEND_Masked`, `MSM_DefaultLit`, `TwoSided=true`, `OpacityMask` driven by complementary `LodFade`/`LodFadeRole` and `DitherTemporalAA`, `WorldPositionOffset` connected for morph, and projected albedo plus normal texture samples. [MEASURED LOCALLY]

The v4 asset is `BLEND_Opaque`, `MSM_DefaultLit`, with WPO and projected detail inputs but no dither function. It is an existing opaque asset, not a complete stable/fading state machine. [MEASURED LOCALLY]

The code exposes `ApplyDrawSetDithered` at `PlanetTerrainNextComponent.cpp:869` and `ApplyLodFadeParams` at `:1063`. `ApplyLodFadeParams` scans every pooled slot each call; scalar writes are cached by last value, but `SyncVisibilityToRenderSet` still performs shadow-state work for shown slots. This is a measured code difference and a candidate CPU/render-thread hypothesis, not a measured frame-time attribution. [MEASURED LOCALLY] [HYPOTHESIS]

## Mesh and transition lifecycle

- `ReleaseNode` retains a compatible PMC section in the pool (`:448–479`); it does not clear every released section.
- `ClearSlotSection` is used when topology/counts are incompatible or a diagnostic rebuild requires it (`:481–492`).
- `UploadPatch` and `UploadCpuMorphedSection` select `CreateMeshSection_LinearColor` only for incompatible topology and `UpdateMeshSection_LinearColor` otherwise (`:636–807`). Counters record creates, updates, clears and render-resource rebuilds.
- `bForceFreshMeshSection` defaults false. The diagnostic flag `singularity.TerrainNext.Diag.ForceFreshSection` defaults true, but only in diagnostic mode.
- Resident visual slots are `UProceduralMeshComponent` instances. Materials are assigned at slot initialization/commit; per-frame fade and morph parameters use MIDs. Shadow state changes occur on visibility/fade transitions and are also touched for shown slots in `SyncVisibilityToRenderSet`.
- Collision uses separate invisible PMC slots and a bounded collision manager. `CollisionCooksPerFrame` defaults to 2 and `CollisionMaxPatches` to 24.

## Verified defaults in code

| Setting | Default |
|---|---:|
| Target pixel error / max level / max patches | 4.0 / 17 / 512 |
| Build budget / tile uploads | 2.0 ms / 16 |
| Height tile cache / disk cache / disk budget | 1024 / enabled / 512 MB |
| Bake workers / queue / prefetch | 4 / 256 / 8 |
| Detail resolution / bake workers / uploads | 512 / 2 / 1 per frame |
| Dithered LOD / legacy CPU morph | enabled / disabled |
| Transition duration / max groups | 0.45 s / 24 |
| WPO morph / CPU morph max level | enabled / 2 when legacy morph is enabled |
| Cast shadow | enabled |
| Collision radius / spacing / max patches / cooks | 200 m / 4 m / 24 / 2 |
| Force fresh mesh section | disabled |

## Statements disproven or corrected by this checkout

- [USER-REPORTED] “The current implementation clears pooled mesh sections on every release and forces new sections.” **Disproven locally:** normal `ReleaseNode` retains sections and the default force-fresh flag is false.
- [USER-REPORTED] “Legacy CPU morph is active by default.” **Disproven locally:** `bEnableLegacyMorph=false`; active CPU morph still requires the runtime flag.
- [USER-REPORTED] “Projected detail normals have no sample cache or mip generation.” **Disproven locally:** bordered height samples, cached normals and mip chains are implemented in `PlanetTerrainNextDetailTile`.
- [USER-REPORTED] “Missing disk tiles still spam the log.” **Disproven in the latest local capture:** the existence guard produced zero new missing-tile warnings; this does not prove disk save/prune is cheap.
- [HYPOTHESIS] “Masked dither is the dominant regression.” **Unresolved:** the candidate material is permanently masked/two-sided, but no GPU pass timing or A/B frame capture has yet established its cost.
- [HYPOTHESIS] “Disk pruning is a regression.” **Unresolved:** `Save` still holds `Mutex` through `PruneToBudget`, and pruning still enumerates the directory; it must be instrumented before changing.

## Discovery gate

No production optimization was implemented during this inventory pass. The next permitted stage is measurement: standalone/packaged and editor captures for S0–S4, with identical trajectories and settings, Unreal Insights CPU/GPU/frame channels, `stat unit`, `stat gpu`, `stat rhi`, `stat scenerendering`, and raw per-frame CSV telemetry. Current process CPU and working-set numbers remain diagnostic context only; they are not frame-time evidence.
