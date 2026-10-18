# R2 status — deterministic height field and coverage quadtree

**Date:** 2026-10-18
**Engine:** Unreal 5.8
**State:** compiled and automation-tested. **Not visually signed off** (no PIE screenshot, no Insights capture yet).

## What R2 adds

| Piece | File | Notes |
|---|---|---|
| Height field | `PlanetTerrainNextHeightField.h/.cpp` | Pure `HeightAt(direction, minWavelength)`. Double-precision value noise (4 macro octaves from 3000 km, 17 roughness octaves from 80 km down to 1.2 m, Hurst 0.7), 4 basins + 56 craters. Seeded by descriptor seed + generator version. |
| Band limit | same | DSE `OctavesFor` rule: a feature of wavelength λ is fully present when λ ≥ 2·minWL, absent when λ ≤ minWL, smoothstep between. Patches sample at minWL = 2 × their vertex spacing. |
| Geometric error | same | Curvature sag + removed/under-sampled noise + crater interpolation. Estimate, not a proven bound. L0 ≈ 8.6 km, L8 ≈ 102 m, L12 ≈ 10.5 m, L17 ≈ 0.74 m. |
| Quadtree | `PlanetTerrainNextQuadtree.h/.cpp` | Six roots, always present. One split criterion: projected error > `TargetPixelError`. Merge below target / 1.5. Offscreen nodes never split. |
| Patch builder | `PlanetTerrainNextPatchMesh.h/.cpp` | 16×16 cells, 289 grid + 68 skirt vertices, one shared index buffer. Vertices relative to the patch center. Normals from a one-vertex halo. |
| Runtime | `PlanetTerrainNextComponent.h/.cpp` | Pooled `UProceduralMeshComponent` slots, one per resident node. Built inside a per-frame millisecond budget. |
| Settings | `PlanetTerrainNextSettings.h/.cpp` | `singularity.TerrainNext.*` CVars, written by the panel at console priority. |

Vertex spacing at level L is 2R / (16 · 2^L), so L17 is about 1.66 m on the 1,737 km moon.

## Coverage rules (as implemented)

1. All six roots are built synchronously in `InitializeTerrain` before the first draw.
2. A node gets children only while it is itself resident. Merging back is always to a ready parent.
3. The draw set takes a node's children only when all four are covered. Otherwise the node itself is drawn.
4. If `MaxPatches` is reached, splits are refused and counted as `blocked`. The parent stays.
5. Visibility only gates splitting, never drawing. A visible node: camera inside its bounds, within max(500 m, altitude) geodesic of the camera nadir, or inside the horizon and the view cone.
6. Each built patch reports its sampled height range back to the tree, which tightens that node's bounding sphere.

Changes to the draw set show the new patches before hiding the old ones, and visibility is touched only for nodes that changed.

## Not in R2 (by design)

- Exact T-junction stitching and geomorphing. Skirts hang 2·GE(L−2) + 1 m below each edge as crack cover. Proper seams and morph are R3.
- GPU tiles, async workers, cache, eviction policy (R4–R5). Patch builds are synchronous on the game thread within `BuildBudgetMs`.
- Collision and shadows. Patches have collision off and `CastShadow` off. R6/R7.
- One draw call per patch. This is the R2 simplification the spec allows (simple CPU meshing).

## Tests

Ran with `UnrealEditor-Cmd -NullRHI`, `Automation RunTests Singularity.PlanetTerrainNext`. All 8 pass.

| Test | What it checks | Result |
|---|---|---|
| `R2.HeightField` | Bit-identical heights for the same descriptor; a different seed changes > 90% of samples; identity hash; heights inside declared bounds at four band limits; basin 0 identical from 500 km band limit to full detail; macro shape identical from 150 km to 1 m; crater profile continuous at the rim and zero outside its influence; geometric error never grows with level | Success |
| `R2.QuadtreeCoverage` | Descent from 3 R to 10 m over a cube corner, a face center, a face edge and an off-axis point; 60 frames of 180° turns at 10 m; ascent back to 3 R. Every frame: each face's drawn area sums to 1, no node is drawn with its ancestor, every drawn node is resident, 64 random directions each land in exactly one drawn node | Success. All four reach L17 at 10 m under the 512 cap and merge back to ≤ 10 nodes |
| `R2.QuadtreeBudget` | `MaxNodes = 12`; one build per frame; children never ready (only six roots drawn); child 3 denied on every split (roots only, denied nodes never drawn); frozen LOD (never splits). Coverage checked every frame | Success |
| `R2.Patch` | Index buffer size and range; UE winding faces outward on all faces at L0/L3/L9; normals outward; same-face seams close (< 0.01 cm); cross-face seams at L2 (every face-edge vertex has a twin < 1 cm); L/L+1 T-junction gap never deeper than the covering skirt | Success. Cross-face gap 0.000000 cm, worst gap/skirt 0.05 |
| `R2.LodProfile` | Informational: drawn nodes by level with no cap | Success |
| R1 `Scale`, `FaceCoverage`, `Precision` | Re-run after the R1 winding fix | Success |

### Profile numbers (CPU, Development editor, this machine)

- CPU patch build (357 vertices): median 0.19 ms, p95 0.27 ms. The default 2 ms budget allows about ten builds per frame, before mesh upload cost.
- Uncapped node demand at a 4 px target: 446 at 100 km, 490 at 10 km, 926 at 1 km, about 2,290 at 100 m and 10 m. With the default cap of 512, the closest nodes still reach L17. The budget is spent nearest the camera, and distant terrain stays coarser.
- At 2 px the uncapped demand is about 5,900 nodes at 10 m, so a 2 px target is not practical with one component per patch. R4 and R5 have to fix that with instanced GPU tiles.

The PMC upload cost, the render-thread cost and the real frame time have **not** been measured. That needs a PIE session with `stat unit` or Insights.

## Bug found while testing

The first run reached only L12 under the cap. The test simulator never reported built height ranges back to the tree, so every bounding sphere spanned the global −7.4 km…+4.2 km range. Every node within about 6 km then looked like it was at distance zero. The runtime component already reports the ranges, but the simulator now does the same. Without that feedback, demand at 10 m was over 100,000 nodes.

## How to test in PIE

1. Play, press **F6**.
2. **Spawn in front of camera.** This also selects renderer mode Next.
3. Use **Fly to altitude**: 1000 km → 100 km → 10 km → 1 km → 100 m → 10 m.
4. Switch the colour to **LOD hue** to see the levels, or **Face** to see the cube faces.
5. The green on-screen line shows altitude, patches drawn, deepest level, nodes against the cap, pending builds and tick time. It turns red if a root is ever missing.
6. Coverage stress tests:
   - **Pause builds**, then fly: the terrain stays coarse but closed.
   - **Max patches** = 12: coarse but closed.
   - **Deny child** = 3: the planet must stay at the six roots and stay closed.
7. **Legacy** hides the patches and shows the R1 static shell again.

Near a Next moon, the fly pawn sets the near clip plane to 1% of altitude (10 cm to 10 km) instead of the deep-space rule, which would cut away the ground.

## Acceptance before R3

- [x] Compiles (`SingularityEditor Win64 Development`)
- [x] Automation tests pass
- [ ] PIE: no holes, no inside-out faces, during a descent to 10 m over a corner and an edge
- [ ] PIE: `stat unit` frame time at 10 m and at 1000 km recorded here
