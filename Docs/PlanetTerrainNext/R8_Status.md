# R8 status — performance and polish (partial)

**Date:** 2026-10-18  
**Engine:** Unreal 5.8  
**State:** compiled and automation-tested. R7 descent remains deferred.

## What R8 adds (this pass)

| Piece | Notes |
|---|---|
| Peripheral LOD | `PeripheralLodScale` (CVar + F6). Multiplies projected PE by a smooth center→rim weight. `1` = off (default, no regression). `<1` coarsens frustum rim to save splits. |
| Stage timings | On-screen stats: `tree / sch / mesh / coll` ms per tick. |
| Stress coverage | Automation turns the camera across four axes under a tight `MaxNodes` budget; six faces stay covered. |

## Deferred within R8

- Insights/GPU profiles on target hardware (needs PIE session with user GPU)
- Richer materials / rocks (only after longer free-flight sign-off)
- Disk tile cache
- Full orbit→land→walk replay (belongs with R7 flight integration)

## Acceptance gates (automation)

| Test | Result |
|---|---|
| `R8.PeripheralLod` | Scale 0.5 never increases max PE vs scale 1 |
| `R8.PeripheralDisabledParity` | Scale 1 keeps a positive on-axis PE |
| `R8.CameraTurnCoverage` | 40 frames of 90° turns; every face stays in the draw set |
| All R0–R6 + R5 tests | Still Success |

## How to check in PIE

1. Default: `PeripheralLodScale 1` — behavior matches R6.
2. Set `0.7` — periphery should refine less; tick `tree`/`mesh` often drop under the same `MaxPatches`.
3. Stats line shows stage ms; watch `coll` while descending.

## Next

- User sign-off on R6 collision/shadows + R8 peripheral toggle  
- Resume **R7** when ready for assisted descent / orbit→surface flight integration  
