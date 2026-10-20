# R8 status — performance and polish

**Date:** 2026-10-18  
**Engine:** Unreal 5.8  
**State:** compiled; automation for peripheral LOD, handoff, horizon, disk cache. R7 descent remains deferred.

## What R8 adds

| Piece | Notes |
|---|---|
| Peripheral LOD | `PeripheralLodScale` (CVar + F6). Multiplies projected PE by a smooth center→rim weight. `1` = off (default). |
| Stage timings | On-screen stats: `tree / sch / mesh / coll` ms per tick. |
| Stress coverage | Automation turns the camera across four axes under a tight `MaxNodes` budget. |
| LOD handoff | One-frame defer: parent-only, then children (no co-draw). |
| Horizon slack | Horizon cull adds patch angular radius + 15%; nadir safety 5 km; invisible grace 2 s. |
| Coverage stats | On-screen `L0/L1/L2/L3+`, `handoff`, `cov ok/HOLE`. |
| Disk tile cache | `singularity.TerrainNext.DiskCache` + `DiskCacheMaxMB`. Memory miss → disk → bake; write-through on bake/Insert. Files under `Saved/SingularityPlanetTerrainNext/Tiles/*.sptn`. |
| Pop reduction | Wider `MorphEnd` (3.5), `MorphSmooth` chase, `MorphReveal` parent→fine ramp on new children, multi-frame `MorphHandoffFrames` parent hold. |

## Known open (does not block R8)

LOD approach can still show **checkerboard / patch holes** in PIE for some users. Handoff + horizon + morph smoothing landed; hole root cause not fully closed. Do not treat green `cov ok` as visual sign-off for that defect.

## Deferred within R8

- Insights/GPU profiles on target hardware
- Richer materials / rocks (after longer free-flight sign-off)
- Full orbit→land→walk replay (belongs with R7)

## Acceptance gates (automation)

| Test | Result |
|---|---|
| `R8.PeripheralLod` | Scale 0.5 never increases max PE vs scale 1 |
| `R8.PeripheralDisabledParity` | Scale 1 keeps a positive on-axis PE |
| `R8.CameraTurnCoverage` | 40 frames of 90° turns; every face stays in the draw set |
| `R8.LodHandoff` | Defer parent-only then commit children |
| `R8.HorizonSlack` | Limb camera still covers all six faces |
| `R8.DiskCache` | Bake→disk→memory clear→reload bit-identical; disabled skips disk |
| All R0–R6 + R5 tests | Still Success |

## How to check in PIE

1. Default: `PeripheralLodScale 1`, `DiskCache 1`, `MorphEnd 3.5`, `MorphSmooth 0.25`, `MorphReveal 0.45`, `MorphHandoffFrames 6`.
2. Fly, watch stats `disk hits/misses/writes`; second session over same tiles should show disk hits after memory eviction.
3. F6: disk cache + morph smooth/reveal/handoff rows. Raise smooth/reveal if pops remain; lower for snappier LOD.
4. Set `DiskCache 0` to force CPU bake only.

## Next

- Visual hole/checkerboard investigation (separate from R8 disk cache)
- Resume **R7** when ready for assisted descent / orbit→surface flight
