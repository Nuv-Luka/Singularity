# R0 / R1 status

**Date:** 2026-10-18  
**Engine:** Unreal 5.8 (`EngineAssociation` 5.8)  
**Reference:** DeepSpaceEngine `6c16ec7d59906d28d85f41b790ca2dfd252f1e02`

## R0 — isolation

| Requirement | Result |
|---|---|
| New module, old terrain left in place | `Plugins/SingularityPlanetTerrainNext`. Legacy sources under `Source/Singularity/PlanetTerrain` were not deleted. |
| Feature flag | `singularity.TerrainRenderer.Mode` = `0` Legacy, `1` Next. Default Legacy. |
| One owner | While Next is selected, `UPlanetTerrainComponent` does not initialize or tick jobs, and hides legacy meshes. |
| Universe, gas giants, atmosphere | Not modified. `SpawnLandableMoon` remains the existing no-op. |
| Git branch `feature/planet-terrain-next` | **Not created.** `C:\Users\luka\Documents\Unreal Projects\Singularity` has no `.git`. A checkpoint commit was not invented on top of Binaries/Saved. |
| DeepSpaceEngine audit | `DOCS/PlanetTerrainNext/DeepSpaceEngine_Code_Audit.md` |

## R1 — static true-scale shell

| Requirement | Result |
|---|---|
| 1 uu = 1 cm, radius 1,737 km → 173,700,000 uu | `PlanetTerrainNext::LunarRadiusUu`, actor scale forced to 1 |
| Six faces, shared edges and corners | `FacePoint` / `TryUnprojectFace`, test `Singularity.PlanetTerrainNext.FaceCoverage` |
| No camera-relative float of absolute AU positions | `CameraRelativeGpuUu`, test `Singularity.PlanetTerrainNext.Precision` |
| No async, foveation, descent, or collision | Shell is built once. Collision disabled. No scheduler. |
| Bounds | `SetBoundsScale(1)`. Occlusion queries are not turned off. |
| Lighting | Default surface material, outward normals, `SetCastShadow(true)` |
| PIE capture | **Not done.** This milestone is compile- and automation-tested, not visually signed off. |

### How to look at it

In a running editor or PIE console:

```
singularity.TerrainNext.SpawnStaticMoon
singularity.TerrainNext.SpawnStaticMoon Far
singularity.TerrainRenderer.Mode 1
```

`Far` places the moon center one AU from the camera along +X. The command does not retarget the orbital simulation.

### Not claimed

- Seamless LOD, morph, GPU tiles, collision, walking, or orbit-to-surface flight.
- That the static sphere casts a meaningful terrain shadow. It is a smooth body; hill-shadow fixtures are R6.
- Visual confirmation from a PIE screenshot.

## Tests

Ran with `UnrealEditor-Cmd -NullRHI` on 2026-10-18. Exit code 0.

| Test | Result |
|---|---|
| `Singularity.PlanetTerrainNext.FaceCoverage` | Success |
| `Singularity.PlanetTerrainNext.Precision` | Success |
| `Singularity.PlanetTerrainNext.Scale` | Success |

Precision check: a 10 m gap above the surface, with the moon center an AU from the origin, survives double-then-float conversion within a millimeter. Casting each absolute centimeter coordinate to float first makes that same gap disappear. A lone `float` of 1 AU in centimeters is already off by about 3 km.

No PIE screenshot or Unreal Insights capture was taken. R1 is not visually signed off.
