# R6 status — local collision and shadows

**Date:** 2026-10-18
**Engine:** Unreal 5.8
**State:** compiled and automation-tested. **Not visually signed off**.

## What R6 adds

| Piece | Notes |
|---|---|
| Collision manager | `FPlanetTerrainNextCollisionManager` — geodesic footprint around camera/vehicle + short predicted path; level from target spacing (~4 m); capped patches. |
| Collision meshes | Invisible `UProceduralMeshComponent` pool, `QueryAndPhysics`, cooked from height tiles (fine surface). Independent of visual LOD. Old patches stay until the new footprint is ready + grace frames. |
| Height queries | `TrySampleSurfaceHeightM` / `GetAltitudeMeters` sample resident collision tiles (bilinear fine), else band-limited `HeightField`. No GPU readback. |
| Shadows | Drawn visual patches `SetCastShadow` from `singularity.TerrainNext.CastShadow` (default on). Bounds already cover WPO morph extremes. |
| Pins | Collision residency pins `EPlanetTerrainNextPinReason::Collision`. |
| CVars | `CollisionRadiusM`, `CollisionSpacingM`, `CollisionMaxPatches`, `CollisionCooksPerFrame`, `CastShadow`. |

## Acceptance gates (automation)

| Test | Result |
|---|---|
| `R6.CollisionLevel` | Spacing→level maps into a sensible 2–8 m band |
| `R6.CollisionFootprint` | Non-empty, capped; works near cube edges |
| `R6.CollisionSample` | Cooked-tile sample sane vs field; altitude ~10 m |
| `R6.CollisionResidency` | Move focus → retire old after grace; keep new |
| `R6.ShadowBounds` | Patch morph deltas available for shadow bounds |
| All prior tests | Still Success |

## Not in R6

- Character movement / ship landing integration (needs gameplay pawn)
- Dedicated hill fixture scene for sun-angle screenshots
- VSM page profiling
- Disk cache / RDG atlas sampling

## How to check in PIE

1. F6 spawn → descend to ~100 m AGL. Stats show `coll resident/desired Llevel cook N`.
2. Walk/fly across a cube edge at low altitude — collision resident should stay non-zero; altitude line stays sensible.
3. `CastShadow 0/1` — hills darken/light under the sun when enabled.
4. Stress: `CollisionCooksPerFrame 1` — old collision persists until the new footprint finishes cooking.

## Acceptance before R7

- [x] Compiles
- [x] Automation under `Singularity.PlanetTerrainNext`
- [ ] PIE: no fall-through near focus; shadows visible on relief
- [ ] Confirm collision cook p95 stays bounded while descending
