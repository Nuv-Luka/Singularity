# PlanetTerrainNext R11 visual LOD continuity

Branch: `feature/planet-terrain-next-r11-complete`

## Implemented

- R11 stage A keeps parent and child geometry resident through a merge fade and uses one shared complementary dither mask.
- `UploadPatch()` now rejects invalid pooled meshes and mismatched vertex attributes before entering `UProceduralMeshComponent`.
- R11 stage B adds cube-face-aware neighbor lookup and a conservative 2:1 quadtree balance gate. A split is deferred when any sampled neighbor edge is two or more levels coarser; the existing covering patch remains active.
- `FPlanetTerrainNextQuadtreeStats::BalanceBlocked` records balance deferrals for telemetry.
- Child patch baking now applies a two-row local edge band: the boundary row is parent-compatible and the adjacent row is half blended, with fine geological detail preserved in the interior.
- The runtime frame stats and `PlanetTerrainNextPerf.csv` now expose `balance_blocked`; Unreal Insights scopes cover quadtree updates and fade-parameter reconciliation.
- The default game-thread patch build budget is 6 ms (`singularity.TerrainNext.BuildBudgetMs`).
- LOD queue/refinement uses `singularity.TerrainNext.LodQueueDistanceScale`, default 2.0. The effective screen-space error threshold is halved, so the same LOD work is queued at approximately twice the camera distance while `MaxPatches` and the existing balance gate remain in force.

## Validation

- `R11` automation: 3/3 passed (`ComplementaryDitherCoverage`, `EdgeBandConstrainsMixedLodBoundary`, `MergeKeepsBothSidesResident`).
- `R10` automation: 4/4 passed (`DiagCollectNodes`, `FlatGeometryL0L1L2`, `HeightGeometryL0L1L2`, `LodFadeComplement`).
- Editor was rebuilt and reopened after the stage B change.

## Remaining acceptance work

The final branch still needs the user trajectory capture for orbital view, approach, descent, surface traversal and ascent. Record frame P50/P95/P99, Game/Draw/RHI/GPU timings, visible patches, draw calls, transition counts, mesh updates/creates, and `BalanceBlocked` while checking cube-face seams and reverse split/merge transitions. No performance recovery claim is made from the automation tests alone.
