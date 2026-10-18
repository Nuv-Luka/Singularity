# R4 status — height tiles, CPU cache, GPU atlas residency

**Date:** 2026-10-18
**Engine:** Unreal 5.8
**State:** compiled and automation-tested. **Not visually signed off** (check tile path in PIE before R5).

## What R4 adds

| Piece | Notes |
|---|---|
| Tile identity | `FPlanetTerrainNextTileId` — planet / seed / generator / height-field hash / node. **No camera epoch.** |
| Height tile bake | `PlanetTerrainNextHeightTile::Bake` — RG float grids (`FineHeightM` / `CoarseHeightM`), Res = 17 (one sample per patch vertex). Coarse heights = bilinear of parent height grid (GPU atlas). |
| CPU mesh from tile | `BuildFromTile` uses fine heights from the tile; **coarse mesh positions** stay R3 parent-**position** bilinear so Morph=1 seals (sphere ≠ height-along-ray). |
| CPU tile cache | `FPlanetTerrainNextTileCache` — LRU by last-used frame; never evicts keys the GPU pool reports pinned; separate from atlas residency. |
| GPU slot pool | `FPlanetTerrainNextGpuSlotPool` — **logical** residency + pins (Draw / Coverage / Collision). Full pinned → `Allocate` = `INDEX_NONE` (keep parent). Real D3D12 float atlas upload deferred (CreateTransient + `UpdateTextureRegions` fatals `E_INVALIDARG` on UE 5.8 D3D12). |
| Component wire-in | Build path: bake → allocate → upload → `BuildFromTile` → PMC. Draw/Coverage pins refreshed each frame. CVars: `CpuTileCache`, `TileUploadsPerFrame`. |

## Acceptance gates (automation)

| Test | Result |
|---|---|
| `R4.TileIdentity` | Same inputs → same id; seed/node change identity; no camera in key |
| `R4.HeightTileBake` | Deterministic bake; `BuildFromTile` ≡ `Build` (< 0.01 cm); root coarse height = fine |
| `R4.TileCache` | Hit/miss; LRU eviction; pinned entries survive |
| `R4.GpuSlotPool` | Alloc/upload; all-pinned fails; unpin → eviction victim gone |
| All R1–R3 tests | Still Success (Morph=1 gaps **0.000000 cm**) |

## Not in R4

- Async workers / prioritized bake queue (R5)
- RDG vertex sampling from the atlas (mesh still CPU; atlas is resident for later)
- Collision / shadows matched to morphed surface (R6–R7)
- Surface color/normal tiles

## How to check in PIE

1. F6 → spawn moon → fly 1000 km → 10 m (same as R3).
2. On-screen stats: tile cache hits/misses, bakes, GPU uploads — should rise on first descent then stabilize.
3. `MorphForce 1` still crack-free (parent-compatible mesh unchanged).
4. Stress: lower `CpuTileCache` / `MaxPatches` and confirm parents stay when GPU slots are pinned full (no holes).

## Acceptance before R5

- [x] Compiles
- [x] Automation tests pass under `Singularity.PlanetTerrainNext`
- [ ] PIE: descent looks continuous; cache/atlas stats behave
- [ ] Confirm atlas texture exists on the component while streaming
