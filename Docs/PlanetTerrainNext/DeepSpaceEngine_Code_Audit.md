# DeepSpaceEngine code audit

**Inspected:** https://github.com/lesbird/DeepSpaceEngine  
**Commit:** `6c16ec7d59906d28d85f41b790ca2dfd252f1e02` (`6c16ec7 chore: net8.0 target + GitHub Actions release workflow`)  
**Clone:** `C:\Users\luka\Documents\Unreal Projects\External\DeepSpaceEngine` (sibling of the UE project, not inside `Source/` or `Plugins/`)  
**License:** MIT, Copyright (c) 2026 Les Bird. Full text copied to `Plugins/SingularityPlanetTerrainNext/LICENSES/DeepSpaceEngine-MIT.txt`.

DeepSpaceEngine is C# / Silk.NET OpenGL. This audit records the algorithms worth translating. OpenGL calls, GLSL strings, water, grass, and biome shading are not ported.

## What Singularity already got wrong

Sources: `C:\Users\luka\Desktop\SINGULARITY\DOCS\PlanetTerrain\` (`Current_State.md`, `StreamingAudit.md`, `TerrainStreamingRepair.md`, `SeamlessPlanetTraversal.md`), frozen 2026-10-18. Gameplay spawn of `ALandableMoonActor` is already disconnected; `AUniverseActor::SpawnLandableMoon` logs and returns. The sources remain and must stay selectable.

Do not bring these back into Next:

1. Screen-space split **or** distance split (`bUseScreenSpaceLod` fallback in `PlanetTerrainQuadtree`).
2. Jobs that stay critical after the camera moves.
3. Whole-moon shell/patch swaps that hide regions when one root arrives. The old shell also used a 0.99 inset and, later, huge `BoundsScale` plus `r.AllowOcclusionQueries 0`.
4. Morph that reuploaded `UpdateMeshSection` every frame, or was forced off near the ground.
5. Chunk meshes kept alive with cache residency treated as the same thing.
6. `UProceduralMeshComponent` create/cook spikes and synchronous `FlushRenderingCommands` on the GPU bake path (`SingularityPlanetTerrain` plugin description still says compute + readback).
7. LOD 12 / 32-cell tiles treated as “enough” without computing meters per vertex.
8. A second height generator for orbital albedo versus landable displacement.
9. Play-arena rescaling (`UuPerMeter = 2.5 km / R`). That path was removed; Next must not revive it.

## PlanetTerrainRenderer.cs

`Game.App/PlanetTerrainRenderer.cs`, class at line 18.

| Constant / method | Lines | Role | Port decision |
|---|---|---|---|
| `GridN = 16`, `MaxLevel = 22` | 20–21 | Patch cells; depth until sub-meter on an Earth-sized world | R1 uses 16 cells on six root faces only. Max level waits for R2. |
| Distance split `dist < LodFactor * WorldSize`, merge at `1.3×` | 22–25, `NodeState` 1662–1674, `EnsureSubtreeGpu` 1677–1700 | **Not** screen-space error. One distance test. | Do **not** copy this criterion. Singularity’s spec requires one screen-space geometric-error test. DSE’s single-criterion and hysteresis ideas stay; the metric changes at R2. |
| Geomorph `mix(aFinePos, aCoarsePos, uMorph)` with `uMorph` 0 = fine, 1 = parent | 48–71, GPU `H()` 533, `DrawSubtreeGpu` 1722–1723 | Shader uniform only. `Smoothstep(splitDist, 2*splitDist, dist)`. | R3. Singularity’s old temporal morph reuploaded meshes; do not repeat that. Note DSE’s 0/1 convention is the reverse of a naive “0 = coarse” reading of the spec’s lerp example. Pick one convention and test it. |
| Parent drawn until every child tile is resident | `DrawSubtreeGpu` 1707–1718 | No hole if a sibling is missing. | Required from R2. R1 has no children. |
| `UploadsPerFrame = 32`, `GpuTileGensPerFrame = 16` | 32, 426 | Separate budgets. | R4/R5. Not hardcoded worker counts from the old Singularity scheduler. |
| `IsNodeVisible` frustum + horizon with patch angle and relief | 1991–2009 | Center test is padded by `WorldSize * 0.75 / R` and `sqrt(2*Amplitude/R)`. | R2. R1 does not cull faces. |
| `DirToFaceUV` / face layout | 2121–2141, `FacePointD` in the generator 1155–1167 | Six faces, UV in [0,1], `a,b` in [-1,1]. | **Ported in R1** (`PlanetTerrainNextCoordinates.h`). |
| `MakeNode` world size `R * (pi/2) * du` | 2146–2160 | Arc-length estimate for the distance split. | Deferred to R2 with the screen-error metric. |
| `QuadNode` | 2484–2507 | Hierarchy, fine/coarse collision heights, `TileId`, base VAO built once. | Ownership split across later types. R1 has no quadtree object. |
| `UpdateRoverLeaf` + `ReadTile` | 1630–1646, cache `ReadTile` 166–174 | GPU readback for collision, cached per leaf change. The cache comment says the read stalls. | Do **not** put a blocking readback on the visual hot path. R6 uses the CPU height field or an async cache. |
| Speculative depth, grass, water | 39, 456+, water shaders | Prefetch and surface dressing. | Out of v1. One airless moon. |

`BuildBaseMesh` (called from `EnsureDrawableGpu`, 1784) builds the cube-sphere patch once. Height comes from the tile. That split is the R3/R4 target. R1 bakes the sphere positions once because there is no height tile yet.

## PlanetTerrain.cs

`Game.Universe/PlanetTerrain.cs`, class at line 31.

| Method | Lines | Role | Port decision |
|---|---|---|---|
| `HeightAt(dir)` / `HeightAt(dir, sampleSpacing)` | 268, 342–351 | Pure function of unit direction and spacing. | R2. Not in R1. |
| `OctavesFor` | 1413–1419 | `maxFreq = pi * R / spacing` (Nyquist), `log2(maxFreq/baseFreq)+1`, clamped. | R2 band limit. |
| `HeightAt2` | 456+ | Fine and coarse samples for geomorph, shared warp. | R3. |
| `LodAnchorElevation` | 281–282 | Split distance uses coarse surface, not the base sphere. | R2, after the height field exists. |
| Continents, mountains, craters, dunes, strata, micro, volcanoes | fields 39–92 | Full geology. | v1 is macro relief + a few craters + bounded roughness. Not this whole stack. |
| `GpuTerrainParams` | 569+ | CPU parameters mirrored into the generator so the shader matches. | R4, with an explicit parity test. Do not assume C# float hashes match UE shader hashes. |

## Noise.cs

`Game.Universe/Noise.cs`. Deterministic value noise / FBM used by `PlanetTerrain`. Not read line-by-line for R1 because R1 has no height field. R2 will port the band-limit rule and a small crater kernel, and will keep the evaluator free of camera and tile seeds.

## TerrainTileGenerator.cs

`Game.App/TerrainTileGenerator.cs`, class at line 28.

| Item | Lines | Role | Port decision |
|---|---|---|---|
| `FloatSafeFreq = 700_000`, `DetailSafeFreq = 32_000_000` | 33, 39 | Octave ceiling so `dir * freq` does not collapse in float. Detail/mountain layers use a split cell base. | R4. R1 never samples noise in absolute float. |
| `FacePointD` | 1155–1167 | Double cube map. Faces: +X `(1,b,-a)`, -X `(-1,b,a)`, +Y `(a,1,-b)`, -Y `(a,-1,b)`, +Z `(a,b,1)`, -Z `(-a,b,-1)`. | Ported. |
| `SetGenUniforms` split cell base | 1059–1093 | Patch-center cell integer wrapped to 8192, fraction kept precise. | R4. |
| `Generate` / `GenerateSurface` | 965, 979 | Height tile at vertex spacing (RG fine/coarse). Separate higher-res normal+albedo pass. | R4 via RDG. Not a GL FBO. |
| `ReadPixels` is on the cache, not required to draw | cache 166–174 | Collision readback. | Not the visual path. |

## TerrainTileCache.cs

`Game.App/TerrainTileCache.cs`, class at line 18.

| Method | Lines | Role | Port decision |
|---|---|---|---|
| Atlas of slots, free stack, `slotOf` / `usedFrame` | 28–31, 38–80 | One 2D atlas because GL texture arrays were unreliable on macOS. | UE can use a texture array or atlas. The **slot pool** is the idea, not the GL limitation. |
| `TryGetLayer` touches LRU | 100–104 | Resident hit. | R4. Identity must not include camera epoch. |
| `AllocateLayer` | 109–117 | Evict LRU, or return -1 if every slot was touched this frame. Caller keeps the parent. | R4. Never overwrite a slot in the current draw. |
| `EvictOldest` skips `used >= frame` | 177–186 | Pins this frame’s tiles. | R4. Add explicit pin reasons (draw, coverage, collision) instead of only a frame stamp. |
| `ReadTile` | 166–174 | Stall. Comment says so. | Not used for Next visuals. |
| Height RG32/RGBA32F nearest; surface RGBA8 linear with a 1-texel gutter (`SurfTileInterior = 96`, `SurfTileSize = 98`) | renderer 431–432 | Mesh resolution ≠ material resolution. | R4. |

## UniversePosition.cs

`Engine.Core/UniversePosition.cs`.

- Sector edge = 1 AU (`1.495978707e11` m), lines 27–34.
- `absolute = Sector * SectorSize + Local`, `Normalize` at 62–79.
- `ToCameraRelative` subtracts sector and local in double, then casts to float (87–92).
- Tests: `Engine.Core.Tests/UniversePositionTests.cs`.

Singularity already places bodies with UE5 large-world `FVector` (double) actor locations. R1 keeps the moon center as the actor origin and stores mesh vertices in actor-local centimeters. It does **not** replace the universe coordinate system. `CameraRelativeGpuUu` is the same subtract-then-cast rule, covered by `Singularity.PlanetTerrainNext.Precision`. A sector type is only justified if a later test shows actor-local LWC is not enough.

## SpeedPolicy.cs

`Game.Systems/SpeedPolicy.cs`, lines 25–78. Self-converging proximity cap: `approachRate * d / (1 - d/engage)`, floor 10 km/s, planet zone `4000 * radius`. This is flight control, not terrain coverage. Deferred until R7, and it must not hide missing collision.

## R1 port boundary

Ported now:

- Face map and inverse (`FacePoint`, `DirectionToFaceUV`, `TryUnprojectFace`).
- Meters internally, centimeters on the mesh, actor scale 1.
- Double subtraction before any float the GPU would see.
- Static six-face shell, built once, bounds scale 1, shadows on, collision off.

Not ported:

- Quadtree, screen-error planner, morph uniform, GPU tiles, scheduler, collision, speed policy, water, vegetation, multi-body terrain.

## Known limits

- R1 is a sphere. No geology, so orbital impostor parity is not testable yet.
- A 1,737 km local float mesh, if the renderer quantizes local vertices to float, has about a 16 cm unit in the last place at that magnitude. That error is stable (it does not swim with the camera). Sub-meter landing geometry needs patch-local origins in a later milestone, not a smaller planet.
- PIE screenshots were not captured in this pass. Coordinate and coverage tests are automation tests.
- The UE project directory is not a git repository, so `feature/planet-terrain-next` was not created. See `R0_R1_Status.md`.
