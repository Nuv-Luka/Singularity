# R10 status

**Date:** 2026-10-20  
**Engine:** Unreal 5.8  
**Branch:** `feature/planet-terrain-next`

## R10.1 — Geometry correctness — IMPLEMENTED (gate)

### Root cause (code-side)

Two defects that match the L0–L2 checkerboard symptom after morph/skirt/winding work:

1. **Pooled PMC index lifetime** — Visual slots called `CreateMeshSection` once, then forever `UpdateMeshSection_LinearColor` **without indices**. `ReleaseNode` left `bHasSection=true`. After Live Coding / index-version bumps (or any topology change), recycled slots kept a **stale index buffer**. Collision path already cleared+recreated; visuals did not.

2. **Static shell occluder** — Shell `bUseAsOccluder=true` while patches are non-occluders. If the shell remained in the occlusion path (or briefly visible), patch pixels could be culled → orbital checkerboard holes.

Morph UV / F16 / skirts were already parked (R9.1); L0–L2 use CPU morph with skirt depth 0 by default — those paths were not the remaining defect.

### Fixes

| Change | Detail |
|---|---|
| `ClearSlotSection` on `ReleaseNode` | Next upload always `CreateMeshSection` with current `Indices()` |
| `IndexBufferVersion` on slot | Recreate if version ≠ `GetIndexBufferVersion()` |
| `ForceFreshMeshSection` CVar (default **true**) | BuildNode uploads Clear+Create; morph Updates still use Update |
| Shell at BeginPlay / Next mode | Hidden + `bUseAsOccluder=false` |
| `ApplySuspendedProxyVisibility` | Syncs visibility and occluder flag |

### Diagnostic harness

| CVar | Role |
|---|---|
| `singularity.TerrainNext.Diag.Enable` | Bypass streaming; frozen patches only |
| `Diag.Mode` | 0=SinglePatch, 1=SixFaces |
| `Diag.Level` | Frozen LOD |
| `Diag.Face` | Face for SinglePatch |
| `Diag.Height` | 0=flat sphere, 1=HeightField |
| `Diag.TwoSided` | Unlit diag material cull |
| `Diag.ForceFreshSection` | Diag Clear+Create |

Unlit material: `M_PlanetTerrainNextDiag_v1` (created in editor on demand).

### Tests (automation)

- `Singularity.PlanetTerrainNext.R10.FlatGeometryL0L1L2` — all faces × all tiles at L0–L2 flat  
- `Singularity.PlanetTerrainNext.R10.HeightGeometryL0L1L2` — mid-tile heighted per face/level  
- `Singularity.PlanetTerrainNext.R10.DiagCollectNodes` — node set sizes  

### Feature bisect (recorded)

| Step | Result |
|---|---|
| Flat six-face L0–L2 (CPU ValidatePatch) | Pass (automation) |
| Height mid-tiles L0–L2 | Pass (automation) |
| Pooled Update without Clear on release | **Fail hypothesis** — fixed by Clear+Create on release |
| Shell occluder under Next | **Fail hypothesis** — fixed by exclusive non-occluder hide |

### PIE checklist

1. `singularity.TerrainNext.Diag.Enable 1` + `Diag.Mode 1` + `Diag.Level 1` + `Diag.Height 0` — solid cube, no holes.  
2. `Diag.Level 2`, then `Diag.Height 1`.  
3. `Diag.Enable 0` — normal streaming; confirm shell stays hidden and `detail N/6` resumes.  
4. Toggle `ForceFreshMeshSection 0` only for A/B (keep default 1).

### Gate

R10.2 (dithered LOD) is **unblocked** only after PIE confirms solid L0–L2 without Diag. Automation green is necessary but not sufficient.

## Later (blocked until PIE gate)

- R10.2 Dithered LOD transitions  
- R10.3 2:1 stitch  
- R10.4–R10.5 Hierarchical detail  
- R10.6 Perf  
