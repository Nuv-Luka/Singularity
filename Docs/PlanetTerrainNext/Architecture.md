# Planet Terrain Next — architecture

**Milestone:** R8 (performance polish; R7 descent deferred). See `R2_Status.md`–`R8_Status.md` and `R7_Status.md`.

**Scope:** one airless rocky moon. No atmosphere, ocean, ice tectonics, foliage, planetary rotation, or a second landable body.

## Renderer selection

`singularity.TerrainRenderer.Mode`

| Value | Owner |
|---|---|
| `0` Legacy (default) | Existing `UPlanetTerrainComponent` if something still initializes it. Gameplay does not spawn it today. |
| `1` Next | `APlanetTerrainNextMoon` and the types below. |

`UPlanetTerrainComponent::TickComponent` and `InitializeTerrain` call `SuspendForNextRenderer` when the mode is Next: the legacy scheduler is shut down and legacy meshes are hidden. Next does not call legacy LOD, morph, height, or landing code.

The console command `singularity.TerrainNext.SpawnStaticMoon` only spawns the R1 actor. It does not move stars, planets, or the atmosphere.

## Data ownership

| Type | Owns | Must not own | Status |
|---|---|---|---|
| `FPlanetTerrainNextDescriptor` | Radius (m), mass, seed, generator version, height bound, R1 cell count | Meshes, jobs, camera | R1 |
| `PlanetTerrainNext` coordinates | Face/UV mapping, planet-local meters, camera-relative uu | Rendering | R1 |
| `FPlanetTerrainNextHeightField` | Pure `HeightAt(direction, minWavelength)`, height bounds, geometric error per vertex spacing | Camera, quadtree, tiles | R2 done |
| `FPlanetTerrainNextQuadtree` | Six-face hierarchy, split/merge by screen-space error, bounds, draw set, pending list | Meshes, GPU resources, workers | R2 done |
| `PlanetTerrainNextPatch` | Shared topology, fine+parent-position-coarse vertices, UV-packed morph deltas, skirts; `BuildFromTile` | Scheduling, visibility | R3–R4 done (CPU) |
| `PlanetTerrainNextMorphMaterial` | WPO morph material (`Morph` scalar, `Color`) | Tile bake | R3 done |
| `FPlanetTerrainNextTileId` / `HeightTile` | Deterministic bake identity + RG height grids | Camera, UObjects | R4 done |
| `FPlanetTerrainNextTileCache` | CPU height tiles; LRU; skip GPU-pinned keys | GPU upload | R4 done |
| `FPlanetTerrainNextGpuSlotPool` | Atlas residency, pin reasons, CPU→GPU upload only | Bake policy | R4 done |
| `UPlanetTerrainNextComponent` | Mesh slots + tile cache + GPU pool, build budget, morph uniforms | LOD policy (asks the tree) | R4 done |
| `FPlanetTerrainNextTilePlanner` | Coverage vs refine vs prefetch desired set | Baking | R5 done |
| `FPlanetTerrainNextTileScheduler` | Bounded async bake, cancel, dedupe, generation | Mesh UObjects | R5 done |
| `FPlanetTileRenderer` | Reused patch topology, shader morph, draw bounds | Scheduling | R3–R4 |
| `FPlanetTerrainNextCollisionManager` | Geodesic collision footprint, height samples, retire grace | Visual LOD | R6 done |
| `APlanetTerrainNextMoon` | Adapter: descriptor, R1 shell (editor / Legacy proxy), terrain component | Orbital integration | R2 |

## Threading

R1 runs on the game thread inside `OnConstruction` / `BeginPlay` and builds six mesh sections once.

In R2, `UPlanetTerrainNextComponent` ticks in `TG_PostUpdateWork` on the game thread, after the camera has moved. Each tick it updates the tree, then builds pending patches in priority order until `BuildBudgetMs` is spent (always at least one), then applies the draw set. There are no workers and no readbacks. `CreateMeshSection` is called once per pooled slot; refills use `UpdateMeshSection` with the same topology.

Later rules, not implemented:

- Workers may fill CPU tiles. They may not touch `UObject`s.
- The render thread consumes RDG passes. No `FlushRenderingCommands` on the streaming path.
- A completion for a different seed or generator version cannot attach to a tile.

## Coordinates

- Simulation and height: `FVector3d` meters, origin at the moon center.
- Actor location: moon center in world centimeters (`FVector`, double under LWC).
- Mesh vertices: actor-local centimeters. Actor scale is forced to `(1,1,1)`.
- GPU deltas: subtract in double, then cast to float (`CameraRelativeGpuUu`).
- Bounds scale stays 1. Occlusion queries are not disabled globally.

`1737000 m * 100 = 173700000 uu` for the 1,737 km test radius.

## R1 draw path

`APlanetTerrainNextMoon` has one `UProceduralMeshComponent` with six sections (one cube face, 16×16 cells). Sections are created when the radius or cell count changes, then left alone. Material is the engine default surface. Shadows use the component’s real bounds. Collision is off until R6.

## R2 / R3 draw path

In play with mode Next, the shell is hidden and each resident node owns one pooled `UProceduralMeshComponent` at the patch centre. Vertices are fine positions; `(Coarse − Fine)` is packed into UV1/UV2. Each slot has an MID of `M_PlanetTerrainNextPatch`. Each frame, morph is a scalar from projected pixel error (`0` = fine, `1` = parent-compatible coarse) written with `SetScalarParameterValue` only. Bounds scale covers the coarse extreme so WPO cannot cull. Legacy mode hides patches and shows the R1 shell.

Debugging: F6 panel (LOD, morph, colours including Morph, altitude teleports, live stats). On-screen line is red if a root is missing.

## R4 draw path (current)

Same PMC + WPO morph as R3. On build: identity → CPU cache bake → GPU slot allocate/upload → `BuildFromTile` (fine from tile heights, coarse from parent **position** bilinear) → mesh once per residency. Atlas is resident for later RDG sampling; the visual morph path still uses packed UV deltas. No readbacks.

## R5 streaming path (current)

Each tick: quadtree pending → planner desired set → scheduler SetDesired/Pump/Drain into CPU tile cache → mesh builds from cache (sync bake fallback for coverage). Workers never touch UObjects. GPU atlas upload still deferred.

## R6 collision / shadows (current)

Local collision patches (invisible PMC, QueryAndPhysics) follow the camera geodesic footprint at a spacing-driven LOD, independent of the visual draw set. Height queries prefer cooked fine tiles. Drawn visual patches cast shadows when `CastShadow` is on; bounds cover morph WPO.

## R7 (deferred)

Assisted descent / orbit→surface flight integration — see `R7_Status.md`. Not blocking R8 polish.

## R8 polish (current)

Optional `PeripheralLodScale` frustum-rim PE weight (default 1 = off). Per-tick stage timings on the stats line. Camera-turn coverage stress test.
