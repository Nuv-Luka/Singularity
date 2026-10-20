# R10 status

**Date:** 2026-10-20  
**Engine:** Unreal 5.8  
**Branch:** `feature/planet-terrain-next`

## R10.1 — Geometry correctness — DONE

Stale PMC indices + shell occluder. Diag harness + validation tests. See prior notes.

## R10.2 — Dithered LOD transitions — DONE (code)

Cesium-style complementary masked dither replaces exclusive handoff as the primary path.

| Piece | Detail |
|---|---|
| `FPlanetTerrainNextLodFade` | Sibling-group Progress 0→1; split/merge + mid-fade reverse |
| `BuildRenderSet` | Desired ∪ fading parent+children (temporary co-draw) |
| Material **v5** | `BLEND_Masked`; `LodFade` + `LodFadeRole`; OpacityMask via DitherTemporalAA |
| Legacy morph | `EnableLegacyMorph` default **false** (Morph forced 0) |
| Shadows | Only dominant side casts during fade (Progress ≶ 0.5) |
| Fallback | `EnableDitheredLOD 0` restores exclusive `ApplyLodHandoff` |

### CVars

- `singularity.TerrainNext.EnableDitheredLOD` (default 1)  
- `singularity.TerrainNext.EnableLegacyMorph` (default 0)  
- `singularity.TerrainNext.LodTransitionSeconds` (default 0.45)  
- `singularity.TerrainNext.MaxConcurrentTransitionGroups` (default 24)  

### Tests

- `Singularity.PlanetTerrainNext.R10.LodFadeComplement`

### PIE check

1. Orbit / descend — HUD `fades N` should pulse during splits; no hard pops.  
2. Reverse camera mid-fade — Progress should reverse without a hole.  
3. `EnableDitheredLOD 0` — exclusive handoff returns.  
4. First editor run creates `M_PlanetTerrainNextPatch_v5`.

## Next: R10.3 — Watertight neighbors

2:1 LOD balance + edge stitching across cube faces.

## Later

- R10.4–R10.5 Hierarchical detail  
- R10.6 Perf  
