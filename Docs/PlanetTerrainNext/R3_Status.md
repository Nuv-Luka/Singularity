# R3 status — shader morph and parent-compatible seams

**Date:** 2026-10-18
**Engine:** Unreal 5.8
**State:** compiled and automation-tested. **Not visually signed off** (check morph in PIE before R4).

## What R3 adds

| Piece | Notes |
|---|---|
| Fine + coarse per vertex | Fine = `HeightAt(dir, fineWL)`. Coarse = bilinear reconstruction of the **parent patch vertex grid** (roots: coarse = fine). |
| UV packing | `UV1.xy` + `UV2.x` = `(Coarse − Fine)` in cm. VertexColor stores coarse normal for later shading. |
| Morph scalar | DSE convention: **0 = fine, 1 = parent-compatible coarse**. `Morph = 1 − smoothstep(Target, Target·MorphEnd, PixelError)`. Default `MorphEnd = 2`. |
| Material WPO | `M_PlanetTerrainNextPatch` (auto-created under the plugin Content on first editor load). WPO = TransformLocalToWorld((Coarse−Fine)·Morph). Only `SetScalarParameterValue("Morph")` changes per frame — **no** `UpdateMeshSection` for morph. |
| Handoff | New children start at Morph = 1 (look like the parent they replace), then ease toward 0 as pixel error grows. |
| Debug | `DebugColor = 3` (Morph): blue = fine, red = coarse. F6 panel: MorphEnabled, MorphEnd, MorphForce. |

Skirts remain as emergency cover for Morph ≈ 0 T-junctions against a coarser non-parent neighbour. Exact equal-level and Morph=1 parent edges need no skirt.

## Acceptance gates (automation)

| Test | Result |
|---|---|
| `R3.MorphCurve` | Smoothstep band: PE ≤ Target → 1, PE ≥ 2·Target → 0, monotone between |
| `R3.ParentCompatible` | Over 24 random children: Morph=1 even vertices match parent (**0.000000 cm**); T-midpoints on parent edges (**0.000000 cm**); fine differs (up to ~1.1 km on coarse levels) |
| `R3.MorphHandoff` | Four children of one parent seal it at Morph=1 (0 cm gaps) |
| `R3.NoMeshRebuildInvariant` | UV-packed offset reconstructs lerp; fine/coarse buffers never mutate under Morph probes |
| All R1 + R2 tests | Still Success |

CPU patch build (357 verts, now with parent grid): median **0.34 ms**, p95 **0.47 ms**.

## Not in R3

- GPU height tiles / atlas (R4)
- Async bake, cache eviction (R4–R5)
- Collision matching morphed surface (R6)
- Blended normals in the pixel shader (coarse normal is packed; lighting still uses fine vertex normals)
- Exact stitch against a **non-parent** coarser neighbour at Morph=0 (skirts + eventual split)

## How to check in PIE

1. F6 → spawn moon → fly 1000 km → 10 m.
2. Debug **Morph**: patches near the merge threshold go red; close patches go blue.
3. `singularity.TerrainNext.MorphForce 1` — whole moon should look like the next-coarser LOD, no cracks at child boundaries.
4. `MorphForce 0` — full fine detail; skirts hide any residual T-junctions against coarser neighbours.
5. On-screen line shows mean morph and how many morph uniforms updated this frame (should be small, not one mesh rebuild per patch).

## Acceptance before R4

- [x] Compiles
- [x] Automation tests pass (12 total under `Singularity.PlanetTerrainNext`)
- [ ] PIE: MorphForce 0↔1 and a live descent look continuous (no popping silhouette)
- [ ] Confirm `M_PlanetTerrainNextPatch` saved under the plugin Content after first editor session
