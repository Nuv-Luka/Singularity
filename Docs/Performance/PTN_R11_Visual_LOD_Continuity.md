# PlanetTerrainNext R11 visual LOD continuity

Branch: `feature/planet-terrain-next-r11-visual-lod`

## Implemented

- R11 stage A keeps parent and child geometry resident through a merge fade and uses one shared complementary dither mask.
- `UploadPatch()` now rejects invalid pooled meshes and mismatched vertex attributes before entering `UProceduralMeshComponent`.
- R11 stage B adds cube-face-aware neighbor lookup and a conservative 2:1 quadtree balance gate. A split is deferred when any sampled neighbor edge is two or more levels coarser; the existing covering patch remains active.
- `FPlanetTerrainNextQuadtreeStats::BalanceBlocked` records balance deferrals for telemetry.

## Validation

- `R11` automation: 2/2 passed (`ComplementaryDitherCoverage`, `MergeKeepsBothSidesResident`).
- `R10` automation: 4/4 passed (`DiagCollectNodes`, `FlatGeometryL0L1L2`, `HeightGeometryL0L1L2`, `LodFadeComplement`).
- Editor was rebuilt and reopened after the stage B change.

## Remaining acceptance work

The branch still needs the user trajectory capture for orbital view, approach, descent, surface traversal and ascent. Record frame P50/P95/P99, Game/Draw/RHI/GPU timings, visible patches, draw calls, transition counts, mesh updates/creates, and `BalanceBlocked` while checking cube-face seams and reverse split/merge transitions. No performance recovery claim is made from the automation tests alone.
