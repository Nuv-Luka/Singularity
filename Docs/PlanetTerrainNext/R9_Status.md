# R9 status

**Date:** 2026-10-20  
**Engine:** Unreal 5.8  

## R9.1 — geometry (L0–L2 holes) — OPEN

Still reproduces in PIE after F16 normalize + CPU morph for L0–L2. **Parked per user** — do not block later milestones. See prior notes on F16 overflow / F16×scale quantization / skirts.

## R9.2 — LOD handoff ↔ morph sync — DONE (code)

| Change | Detail |
|---|---|
| Morph only when shown | `ApplyMorph` iterates final `Drawn` + `bShown`; hidden residents freeze |
| Commit reset | `BeginVisibleHandoff`: Morph=1, MorphReveal=0, CPU/GPU upload at parent seal |
| Hide freeze | On hide: MorphReveal=0, Morph=1 so re-show does not inherit mid-reveal |
| Reveal curve | SmoothStep on MorphReveal when blending 1 → PE target |
| Stats | Drawn counts use post-handoff `Drawn` set |

Split parent-hold (`MorphHandoffFrames`) unchanged. Full merge state-machine (hold children until Morph≈1) still light — hide snaps Morph=1.

## R9.3 — orbital surface detail — DONE (code)

Six root-face **albedo + tangent-space normal** textures from the same `HeightField` as geometry. Independent of mesh LOD residency.

| Piece | Detail |
|---|---|
| `FPlanetTerrainNextDetailTile` | Id + Albedo/Normal FColor arrays; `Bake` band-limits by texel wavelength |
| `FPlanetTerrainNextDetailStore` | Async bake (≤2 workers), ≤1 GPU upload/frame; 6 root faces |
| Material v4 | `DetailAlbedo`/`DetailNormal` + `DetailUVScale`/`DetailUVOffset`/`DetailBlend`; FaceUV = PatchUV×Scale+Offset |
| Component | Pins textures, rebinds MIDs when uploads land; debug color modes force Blend=0 |

### CVars

- `singularity.TerrainNext.DetailProjection` (default 1)  
- `singularity.TerrainNext.DetailResolution` (default 512; on next init)  
- `singularity.TerrainNext.DetailMaxConcurrentBakes` (default 2)  
- `singularity.TerrainNext.DetailUploadsPerFrame` (default 1)  

### Tests

- `Singularity.PlanetTerrainNext.R9.DetailBakeDeterminism`  
- `Singularity.PlanetTerrainNext.R9.DetailFaceUvMapping`  

### Validate in PIE

1. Orbit at several radii — craters/ridges should read in albedo+lighting on L0–L2 meshes.  
2. Toggle `DetailProjection 0/1` — surface should fall back to flat regolith Color.  
3. HUD: `detail N/6` should climb to 6 without hitching the game thread.  
4. F16 material: first editor run creates `/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v4`.

## Next: R9.4 — hierarchical visual streaming

Child detail tiles, parent UV fallback, texture crossfade (`TextureLodBlend` ≠ Morph).

## CVars (R9.1 leftovers still useful)

- `MorphCpuMaxLevel` (default 2)  
- `MorphGpuMaxScaleCm` (default 1000)  
- `MorphWpoEnabled` (0 = force CPU morph / idle WPO)  
