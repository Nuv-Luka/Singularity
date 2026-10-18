# SINGULARITY — Port WebGPU Sky Atmosphere to Unreal Engine 5.8

**Cursor engineering implementation guide | revision 1 | 2026-10-14**

**Project:** `C:\Users\luka\Documents\Unreal Projects\Singularity` (UE5.8 PUG port)

**Reference repository:** https://github.com/JolifantoBambla/webgpu-sky-atmosphere

**Mathematical reference:** https://sebh.github.io/publications/egsr2020.pdf

**Existing verified comparator:** Bevy `hillaire_raymarch.wgsl` and `DOCS/WiP/hillaire_atmosphere_port.md` (inspect actual current files; do not assume their paths or implementations beyond the user's record).

**License:** Upstream `webgpu-sky-atmosphere` is MIT (copyright Lukas Herzberger, 2024). Preserve copyright and MIT notice in any adapted/distributed source and notices. Inspect dependency and asset licenses separately. The original Hillaire demonstration is credited by upstream as MIT; this does **not** license arbitrary proprietary Unreal Engine shader source for redistribution.

**Status:** Architecture and migration specification only. The Unreal source tree has not been inspected or compiled by the author of this guide. Names of newly proposed files are suggestions; Unreal 5.8 callback signatures and rendering APIs must be verified against the engine headers installed on the developer machine. Do not claim that generated C++ / HLSL is built or working until tested in editor and packaged game.

## 0. Decision and guardrails

**APPROVED architecture:** standalone UE5.8 `SingularityAtmosphere` runtime plugin, built around explicit scene/planet geometry, custom HLSL compute passes through Unreal Render Dependency Graph (RDG), and pre-tonemap scene-color composition. First produce correct single-planet limb scattering; then add Hillaire LUTs; then multiple visible planets.

**Do NOT**:

- Reuse the previously failing `FGlobalShader` **raster/full-screen pixel pass** and `AddDrawScreenPass` integration before a minimal D3D12 PSO validation. Prefer `SF_Compute` shaders dispatched through `FComputeShaderUtils::AddPass`.
- Generate `UMaterialExpressionCustom` graphs dynamically at runtime. Use editor-authored materials only for an optional diagnostics/identity fallback.
- Apply scattering after tonemapping to LDR scene color.
- Show an additive/Fresnel atmosphere shell as a substitute for physical scattering.
- Enable UE's global `USkyAtmosphereComponent` as a workaround when the requirement is multiple **planet-local** atmospheres.
- Pass absolute AU-scale coordinates, centimeters, and kilometers interchangeably; adopt explicit planet-relative camera coordinates in **kilometers**.
- Render atmosphere on rays that do not intersect an atmosphere volume. The exact miss behavior is `L_out = L_scene`.
- Change cube-sphere mesh winding; the previous square-hole issue was a separate, already-fixed mesh problem.
- Port every shader and all features in one commit. Require an explicit acceptance gate after every milestone.

**Keep:** `Pug/AtmosphereGen.cpp`, `FPugAtmosphereDescriptor` in `PugCore.h`, working star light, planet meshes, and black-space scene defaults. Do not modify the Bevy branch. Atmosphere toggle `r.SkyAtmosphere 0` can remain because this plugin is independent of Unreal's stock global sky.

## 1. Upstream research: what is actually being ported

The repository is a WebGPU/TypeScript + WGSL implementation of **Sébastien Hillaire's EGSR 2020** atmospheric scattering. It supports ground-to-space rendering, clear-sky postprocess, a direct full-resolution raymarch mode, LUT-based modes, optional two directional lights and externally supplied shadow sampling. The library defaults to distances expressed with **1 world unit = 1 kilometer**, controlled with `fromKilometersScale` in the WebGPU host integration. These features are documented in its README; they are not UE plugins.

Read these files from a *pinned upstream commit*, then preserve that commit SHA in `THIRD_PARTY.md`:

| Upstream path | Responsibility | Proposed Unreal port |
|---|---|---|
| `src/atmosphere.ts` | Rayleigh, Mie, absorption, planet geometry, Earth example | `AtmosphereTypes.h`, `AtmosphereProfile.cpp` |
| `src/config.ts` | LUT/pass options, quality, lights, phase functions | `AtmosphereSettings.h` |
| `src/uniforms.ts`, `src/resources.ts` | Camera/light packing, GPU resources, default LUT sizes | `AtmosphereUniforms.h`, `AtmosphereResources.cpp` |
| `src/pipelines.ts`, `src/lut-renderer.ts` | Pass dependencies and dispatch | `AtmosphereLuts.cpp` |
| `src/shaders.ts` | Assembles WGSL common modules and entrypoints | Separate `.ush` includes and `.usf` entrypoints |
| `src/shaders/common/intersection.wgsl` | Sphere intersections | `AtmosphereGeometry.ush` |
| `src/shaders/common/medium.wgsl` | Density/extinction/scattering coefficients | `AtmosphereMedium.ush` |
| `src/shaders/common/phase.wgsl` | Rayleigh/Mie phase functions | `AtmospherePhase.ush` |
| `src/shaders/common/coordinate_system.wgsl`, `uv.wgsl` | Camera/world/UV mapping | `AtmosphereCoordinates.ush` |
| `src/shaders/common/multiple_scattering.wgsl` | Multiple scattering integration | `AtmosphereScattering.ush` |
| `src/shaders/common/aerial_perspective.wgsl`, `sky_view.wgsl`, `blend.wgsl` | LUT lookups, scene composition | `AtmosphereSampling.ush`, `AtmosphereComposite.ush` |
| `src/shaders/render_transmittance_lut.wgsl` | Transmittance compute | `TransmittanceCS.usf` |
| `src/shaders/render_multi_scattering_lut.wgsl` | Multiple scattering compute | `MultiScatteringCS.usf` |
| `src/shaders/render_sky_view_lut.wgsl` | Camera-specific sky-view compute | `SkyViewCS.usf` |
| `src/shaders/render_aerial_perspective_lut.wgsl` | Camera-frustum aerial LUT | `AerialPerspectiveCS.usf` |
| `src/shaders/render_sky_raymarching.wgsl` | Per-pixel raymarch | `AtmosphereRaymarchCS.usf` |
| `src/shaders/render_sky_with_luts.wgsl` | Lookup-based sky | `AtmosphereCompositeCS.usf` |

Also inspect `render_sky_luts_and_raymarch.wgsl`, `shadow_base.wgsl`, `sample_sagment_t.wgsl` (upstream spells `sagment`), and the chosen Mie phase-function files. Do not *assume* one-to-one generated HLSL translation: translate functions and verify their outputs against upstream in unit tests.

**Upstream LUT defaults**, sourced from `src/resources.ts`:

| Texture | Default | Proposed texture format | Update rule |
|---|---|---|---|
| Transmittance | `256×64` | `PF_FloatRGBA` / RGBA16F | When atmosphere optical parameters/radii change |
| Multiple scattering | `32×32` | RGBA16F | When profile parameters change, after transmittance |
| Sky view | `192×108` | RGBA16F | View/light-dependent; only when used |
| Aerial perspective | `32×32×32` | RGBA16F | View/light-dependent; only when used |

Upstream uses `rgba16float` for these textures. Unreal's platform-specific RDG format flags and UAV support must be checked. `PF_FloatRGBA` is a proposed mapping, **not an assertion about any particular RHI resource**.

Source repo permits two paths: LUT sky compositing, and full-resolution raymarching. It states that full-resolution raymarching produces smoother orbital transitions and colored transmittance, while LUTs reduce cost. Because SINGULARITY's immediate goal is *planetary limbs viewed from space*, prioritize **masked direct orbital raymarching with transmittance + multiple-scattering LUTs**. Add sky-view/aerial LUTs for near-ground gameplay **later**. This ordering is our integration recommendation, not an upstream requirement.

## 2. Unreal plugin structure

Create a self-contained runtime plugin without changing core planetary generation:

```text
Plugins/SingularityAtmosphere/
  SingularityAtmosphere.uplugin
  LICENSES/webgpu-sky-atmosphere-MIT.txt
  THIRD_PARTY.md
  Source/SingularityAtmosphere/
    SingularityAtmosphere.Build.cs
    Public/
      SingularityAtmosphereSubsystem.h
      SingularityAtmosphereTypes.h
      SingularityAtmosphereSettings.h
    Private/
      SingularityAtmosphereModule.cpp
      SingularityAtmosphereSubsystem.cpp
      SingularityAtmosphereViewExtension.h/.cpp
      SingularityAtmosphereLuts.h/.cpp
      SingularityAtmosphereResources.h/.cpp
      SingularityAtmosphereShaders.h/.cpp
      SingularityAtmosphereDiagnostics.cpp
  Shaders/Private/
      AtmosphereGeometry.ush
      AtmosphereCoordinates.ush
      AtmosphereMedium.ush
      AtmospherePhase.ush
      AtmosphereSampling.ush
      IdentityCS.usf
      AtmosphereMaskCS.usf
      TransmittanceCS.usf
      MultiScatteringCS.usf
      AtmosphereSingleScatterCS.usf
      AtmosphereCompositeCS.usf
      SkyViewCS.usf
      AerialPerspectiveCS.usf
```

- The plugin's module must load at **`PostConfigInit`** to register its virtual shader directory before engine shader initialization. This is specified by Epic's global-shader documentation.
- In `StartupModule()`, register something like `AddShaderSourceDirectoryMapping(TEXT("/SingularityAtmosphere"), PluginShaderAbsolutePath)` and use that virtual path in `IMPLEMENT_GLOBAL_SHADER(...)`. Validate path resolution and compile logs once. Do not repeat the old runtime registration mistake.
- Initial `Build.cs` likely needs `Core`, `CoreUObject`, `Engine`, `RenderCore`, `RHI`, `Renderer`, `Projects`; inspect 5.8 header/module dependencies rather than treating this as exhaustive.
- `USingularityAtmosphereSubsystem`: gathers eligible atmosphere bodies from existing PUG descriptors and maintains game-thread-safe immutable snapshots. Do not capture mutable `UObject*` pointers in RDG lambdas.
- `FSingularityAtmosphereViewExtension` (subclass `FSceneViewExtensionBase`): takes an immutable per-view snapshot on render thread and inserts passes at a verified **pre-tonemap** postprocessing location. Confirm UE5.8 overloads: use `SubscribeToPostProcessingPass(EPostProcessingPass, const FSceneView&, FPostProcessingPassDelegateArray&, bool)`. The older overload without `FSceneView` is deprecated in UE5.8.
- Respect multiple editor/game views, viewport rectangles, screen percentage, and eye-specific matrices. Never hardcode backbuffer size or assume view starts at `(0,0)`.
- Maintain a render-thread LUT cache keyed by atmosphere optical profile, radius and solver version; keep per-view state separate from profile state.
- Expose console variables `r.SingularityAtmosphere.Enable`, `.Debug`, `.Quality`, `.MaxPlanets` and a profiler scope for each pass.

### Suggested C++ descriptor (illustrative, **not** drop-in compiled UE code)

```cpp
struct FAtmospherePlanetProxy
{
    uint64 BodyId = 0;
    FVector3d PlanetCenterWorldCm = FVector3d::ZeroVector;
    double VisualBottomRadiusKm = 6371.0;
    double VisualTopRadiusKm = 6471.0;

    FVector3f RayleighScatteringPerKm;
    float RayleighScaleHeightKm = 8.0f;
    FVector3f MieScatteringPerKm;
    FVector3f MieExtinctionPerKm;
    float MieScaleHeightKm = 1.2f;
    FVector3f AbsorptionPerKm;
    float MieAnisotropyG = 0.8f;

    FVector3f DirectionToStar = FVector3f(0,0,1);
    FVector3f StarIlluminanceLinear = FVector3f(1,1,1);
    uint32 AtmosphereProfileHash = 0;
};
```

Use the **actual** upstream `Atmosphere` members as the authoritative contract; the structure above is intentionally simplified and not sufficient for a 1:1 Hillaire implementation. CPU `double` for large coordinates; GPU `float` in planet-relative kilometers. For UE LWC, compute `(CameraWorldCm - PlanetCenterWorldCm)` in double **on CPU**, then convert to `float3` kilometers after verifying it fits the chosen local frame.

## 3. Mathematical contracts, geometry and rendering

### 3.1 One coordinate system, one unit system

UE internally uses centimeters, but the reference atmosphere parameters are in kilometers. **Never** pass AU positions straight to the scattering code.

```text
cameraRelativeKm = (CameraPositionWorldCm - PlanetCenterWorldCm) / 100000.0
rayDirection     = normalized camera view ray in engine-compatible world axes
bottomRadiusKm   = rendered planet/cloud-top mesh radius in kilometers
outerRadiusKm    = bottomRadiusKm + selected atmosphere thicknessKm
```

If the game's on-screen planets use nonphysical visual scaling, derive the conversion from **rendered geometry**, not the astrophysical catalog radius, or intersection/depth will disagree. Record `VisualKmToWorldCm` on the body if global scaling differs from ordinary `100000 cm/km`. All distances that enter a given ray test must use the *same* scale.

The upstream implementation supports different atmosphere centers and a Y-up or Z-up choice; UE is Z-up. Convert axes explicitly, including star direction. Start from a camera-relative frame located near a single planet. Reproduce identity tests before placing atmospheres at system scale.

### 3.2 Ray/sphere intersection

At render time, reconstruct normalized view ray `d` and use camera relative to atmosphere center `o` in kilometers. For sphere radius `r`:

```text
b = dot(o, d)
c = dot(o, o) - r*r
h = b*b - c
if h < 0: miss
else t0 = -b - sqrt(h); t1 = -b + sqrt(h)
```

This assumes normalized `d`, units matching `r`, and positive ray parameter. Intersect with atmosphere outer sphere, then subtract any solid-body segment. Cases:

- Outside atmosphere, ray misses outer sphere: pass through **exact scene color**.
- Outside, ray crosses atmosphere but misses planet: finite atmosphere chord `[max(0,t_atmoNear), t_atmoFar]`.
- Outside, ray hits opaque planet/cloud sphere: stop integration at its front intersection (also clip to scene depth).
- Camera inside atmosphere: start `t=0`, stop at atmosphere exit, solid body, or scene depth, whichever comes first.
- Camera below visual ground: clamp/test explicitly; return safe debug output (no NaN or full-frame haze).
- Camera facing empty space from outside: **no atmospheric modification** even if a global pass runs.

Test tangent/grazing rays, camera precisely at sphere edge, and intersections across near/far clipping planes. Clamp discriminant and division safely.

### 3.3 Scene depth and HDR composition

For each pixel:

1. Obtain previous *linear HDR* scene color (`PostProcessInput0` for a test material; actual RDG scene-color SRV in plugin).
2. Reconstruct camera ray using **UE5.8 View uniforms and inverse view-projection conventions**. Check reversed-Z depth, clip-space conventions and dynamic-resolution view rectangle. Do not construct NDC rays from pixel-coordinate `ScreenPosition` or reuse matrices from Bevy unchanged.
3. Convert scene depth to an actual positive world-space distance along that ray using engine-supported depth reconstruction. If depth is absent, treat the scene as sky/background and stop at the atmosphere exit. Do not blindly treat raw depth as meters.
4. Find per-planet overlap with the valid finite view-ray segment.
5. Integrate scattering and extinction across that segment (start with simple analytic coefficients, then reference Hillaire).
6. Compose:

   `L_out = T_rgb * L_scene + L_inscatter_rgb`.

   `T_rgb` is dimensionless in `[0,1]`; `L_inscatter` has the same radiometric/exposure convention as scene color.
7. Unreal may render scene color **pre-exposed**. When using pre-exposed scene color, multiply physical in-scattering radiance by the appropriate per-view pre-exposure **once**; do not scale transmittance. Use a known neutral scene-color comparator and auto-exposure-disabled test to verify. Never silently disable project-wide exposure to paper over bad units.

Do not composite into an input texture that the compute shader also reads: allocate a new UAV-capable RDG output, read the input as SRV, write output as UAV, return the new `FScreenPassTexture` in the view-extension callback. Honor `Inputs.OverrideOutput` and the exact callback contract. When disabled, refrain from subscribing, or explicitly return the untouched input through the engine-supported helper with correct view rect; check active UE5.8 edge cases for split-screen.

### 3.4 Multiple planet compositing

For N distinct atmospheric segments on one ray, collect only the intersections in front of scene depth, sort by *entry distance from camera*, then compose front-to-back:

```text
accumulatedRadiance  = 0
accumulatedTransmission = float3(1,1,1)
for atmosphere in orderedVisibleSegments:
    accumulatedRadiance += accumulatedTransmission * atmosphere.Inscatter
    accumulatedTransmission *= atmosphere.Transmittance
final = accumulatedTransmission * SceneColor + accumulatedRadiance
```

This uses each segment's outgoing radiance as viewed from its entry boundary. For physically overlapping media or nested atmospheres, this simple independent-segment model is not sufficient: merge/integrate optical properties over overlaps, or reject overlap with a diagnostic in the MVP. Similarly, background interplanetary nebulae/glows require correct depth order. Start with at most two disjoint visible atmospheres.

No globally unbound full-screen tint. A full-screen compute dispatch is permissible **only when pixels with zero valid intersected segments write unmodified scene color**. Optimize later with scissored dispatch/ROI.

## 4. Rendering pipeline and stage contracts

### Stage P0 — Identity compute without atmosphere (first priority)

- In a **separate UE5.8 test level**, keep planet mesh + directional star light, black space, default PUG descriptors.
- Plugin registered at `PostConfigInit` and HLSL shader path mapped. Minimal `FGlobalShader` compute type (`SF_Compute`).
- Insert view-extension callback at verified **pre-tonemap** `EPostProcessingPass` location. Check `EPostProcessingPass`/postprocess stages in installed 5.8; a callback inserted at `AfterDOF` or immediately before `Tonemap` is a candidate, not a guarantee of scene color layout.
- `IdentityCS.usf` reads the input texture and writes identical RGB to a separate RGBA16F UAV output. Return that texture/`ViewRect` correctly.
- PIE must not crash; result must be pixel-equivalent within tiny floating point error. Verify resolution changes, editor dock/undock, PIE versus Standalone, and empty space.
- If even this fails, fix RDG/render-target/PSO state **before any atmosphere math**. Do not go back to screen-pass raster PSOs.

### Stage P1 — Debug geometry and ray reconstruction

- Add test modes `ViewRayRGB`, `PlanetMask`, `AtmosphereEntryDistance`, `SegmentLengthKm`, `SceneDepthDistanceKm`.
- Render one atmosphere **only** where the ray intersects the outer atmosphere. Outside ROI return original scene color.
- Validate outside, grazing, inside, horizon, planet occlusion, look-away, far-away and extremely close cameras. Manually compare ray/sphere code with double-precision CPU test cases.
- Validate the physical mesh position/radius vs descriptor values. No reliance on UV sphere material or Fresnel effect.
- Match view-port UV and pixel-coordinates in UE, particularly `ScreenPosition` output pin 0 (`ViewportUV`) vs pin 1 (`PixelPosition`) in any optional material diagnostics.

### Stage P2 — Finite-segment extinction and single scattering

- Implement Rayleigh, Mie and absorption *on the valid segment only*, with atmosphere density based on altitude above bottom radius.
- Use a physically consistent fixed number of steps first (e.g., 16–32 for the diagnostic, tune later).
- Validate `T=exp(-opticalDepth)` is bounded `[0,1]`, no NaN, increases toward 1 as density approaches zero, and correctly depends on path length and wavelength.
- Compose colored attenuation over the existing textured planet. Keep clear sky black outside valid atmosphere segments.
- Use the same math and comparable atmospheric parameter sets in Bevy and UE and log sampled `T` / `S` pairs at the same relative ray positions.

### Stage P3 — Constant Hillaire LUTs (from upstream)

Port in dependency order:

1. **`TransmittanceCS.usf`:** radius/zenith LUT 256×64, computing colored transmittance toward top of atmosphere.
2. **`MultiScatteringCS.usf`:** 32×32, sampling the transmittance LUT. Respect upstream phase and sampling semantics.
3. Cache per optical profile/radii/version; do not update when merely moving the camera around the same planet.
4. Compare GPU samples against upstream-generated reference values from matching atmosphere profiles, where available; otherwise compare numerical CPU integration values and expected invariants.
5. Integrate `T` and approximate multiple scattering into the finite-segment orbital pass.

Use RGBA16F UAVs supported by D3D12, explicit resource dependencies in RDG, named GPU event scopes and fixed workgroup sizes matching the *ported* kernel. Upstream uses 16×16 group partitioning for some LUT pipelines; do not assume every entrypoint uses identical dimensions without reading its WGSL.

### Stage P4 — Camera-dependent sky and aerial LUTs

- **`SkyViewCS.usf` 192×108** for local surface/inside-atmosphere sky. Recompute when camera altitude/orientation and star direction change materially, as required by upstream mapping.
- **`AerialPerspectiveCS.usf` 32×32×32** for atmospheric attenuation/scattering of scene geometry through camera frustum, only when near/inside the active atmosphere.
- Validate UE camera projection including near plane and reverse-Z, and consistent scene depth in the composite.
- Use the LUT-based path where image error is acceptable. Prefer direct finite-segment raymarch near the limb or at transitions, as upstream notes improved smoothness in orbital conditions.
- Do not add moonlight, sun disk, shadow injection or temporal reprojection yet.

### Stage P5 — Multiple planets, caches, quality and shipping

- System query chooses nearby or screen-relevant bodies with `FPugAtmosphereDescriptor` and sorts candidate atmosphere intersections per view.
- Cache constant LUTs by optical profile hash + radii + version. Add LRU memory budget; background LUT recomputation only when profile changes.
- Support two disjoint planets visible at once and prove that both limbs appear in the same image, without altering the view's black empty space.
- Quality switches: no active planet = no passes; distant dot = lightweight disk/analytic attenuation; orbital = masked lower-sample raymarch; atmosphere approach = higher samples or sky/aerial LUT hybrid.
- GPU time target for a lower-end laptop RTX 3050: profile, don't assume. Try to keep the atmosphere pipeline around **1–2 ms at 1080p medium** as an aspiration; drop quality/ROI until measured costs are acceptable.
- Test DX12 shipping/standalone and PIE, packaged shader compilation, no runtime shader registration asserts, cache recreation after resize/device loss where applicable, editor preview, dynamic resolution and multiple viewports.

## 5. Source WGSL → HLSL translation checklist

The TypeScript host code is **not** part of the C++ runtime. Port shader math and the resource/pass dependency model, not npm or WebGPU wrappers.

| WGSL | Unreal/HLSL |
|---|---|
| `@compute @workgroup_size(...)` | `[numthreads(...)]` and `SV_DispatchThreadID` |
| `texture_storage_2d<rgba16float, write>` | `RWTexture2D<float4>` with matching RDG UAV |
| `texture_2d<f32>` | `Texture2D<float4>` or typed SRV |
| `textureSampleLevel()` | `.SampleLevel(sampler, uv, lod)` |
| `textureLoad()` | `.Load(int3(pixel, level))` |
| `vec3<f32>` | `float3` |
| `mat4x4<f32>` | `float4x4`, **validate multiplication order** |
| `select`, `mix`, `clamp` | `lerp`/conditionals, `lerp`, `clamp`; validate argument semantics |
| `atan2`/`acos`/`sqrt` | HLSL equivalents; clamp inputs and guard boundary cases |
| Separate WebGPU bind groups | UE5 `SHADER_PARAMETER_*`, `SHADER_PARAMETER_RDG_TEXTURE[_UAV]`, samplers |
| WebGPU dynamic pipeline creation | Unreal global shader compile at plugin load, dispatch via RDG |

Pay special attention to:

- WGSL/HLSL row-major vs column-major matrix interpretation, transpose and view-projection multiplication order.
- `normalize()` of near-zero vectors; horizon tangent precision.
- `exp(-sigma * distance)` units: coefficients must be in `km^-1` if `distance` is kilometers.
- Mie phase function conventions (`dot(viewDir, sunDir)` vs sign conventions); validate front/forward scattering.
- LUT coordinates and UV half-texel mapping: preserve upstream parameterization, not arbitrary screen UVs.
- Hillaire planetary height and ray/sphere root selection when camera is inside the atmosphere.
- Mixed physical radiance and Unreal pre-exposure.
- Linear HDR color and texture storage; no sRGB encoding applied inside radiance integration.
- Correct shadowing of the atmosphere by the solid planet, and prevention of light contribution through the far side of the body.

## 6. Proposed automated and visual test suite

Write small deterministic CPU reference tests independent of Unreal render code for ray intersections and extinction. Generate images/CSV diagnostics for geometry/shader stages.

| Test | Pass criterion |
|---|---|
| Identity, disabled | No runtime blendable or shader pass changes scene; exact scene color |
| Identity, enabled | Single compute pass returns scene color unchanged, no PSO fatal in PIE |
| Sphere misses | `T=1`, `S=0` or exact untouched copy; no full-screen muddy tint |
| Two identical cameras | Identical atmosphere and correct viewport handling |
| Planet orbit approach | Limb transitions smoothly, texture remains visible through attenuation |
| Ground view | Sky covers correct hemisphere, no ray inversion or horizon flip |
| Occluders in foreground | No atmosphere incorrectly drawn across nearer opaque meshes |
| Eclipse/night side | No spurious sunlight scattering through planet body |
| HDR test | Results track scene exposure consistently, no after-tonemap color muddiness |
| Multiple planets | Two separate visible limbs; look-away sky unchanged |
| Scale test | Same image for an equivalent planet rendered in different absolute AU positions after recentering |
| Validation | No NaN/Inf, negative optical depths, out-of-range LUT coords or D3D12 validation errors |
| Performance | GPU scopes per pass, 1080p on RTX 3050 profile, minimal latency on approach |

Save fixed screenshot baselines and GPU capture/traces. Recommended Unreal tools: `stat GPU`, GPU Visualizer, RenderDoc if available for chosen RHI, Unreal Insights and RenderDoc compatible configurations. Enable `r.ShaderDevelopmentMode=1` for useful shader error diagnostics.

### Failure isolation: symptom → likely subsystem

- **Everything becomes a muddy fullscreen tint:** inspect look-away ray miss, view reconstruction, depth clipping, postprocess location, pre-exposure, stale blendable.
- **A hard planet-sized glow disk:** scattering/solid sphere visibility, transmittance and cloud depth mismatch.
- **Invisible atmosphere:** inconsistent world scale, sphere centers, wrong sign of ray, LUT never generated/bound, runtime atmosphere disabled.
- **PSO crash on PIE:** render-target format/sample count, shader parameter binding, uninitialized output, pass stage; run identity compute test first.
- **Halo offset from planet:** CPU/GPU planet-relative coordinate convention, world origin shifting, screen rect and projection.
- **Works in editor but not packaged:** plugin shader registration stage, shader virtual path, platform shader permutation, missing shader cooking.
- **Works at origin only:** absolute AU-scale transforms leaked into GPU instead of planet-relative km.

## 7. Suggested milestone deliverables and commit gates

**M0 — Repo audit / safe baseline:** Audit current UE5.8 source and PUG descriptors, identify exact render hook, create test map/branch, verify `r.SkyAtmosphere=0`. Output design diff, compile baseline. No scene appearance changes.

**M1 — No-op compute in PIE:** Plugin compiles in Editor, Standalone and packaged build; identity shader is active and returns `FScreenPassTexture` with correct input/output and viewport; no D3D12 PSO error. **Stop if this fails.**

**M2 — Geometry / depth:** Planet mask, finite atmosphere ray segments, correct scene depth and look-away identity, including inside-atmosphere case. Debug images stored and reviewed.

**M3 — Basic physical color:** Single-scattering and Beer–Lambert transmittance in scene-linear HDR, compare Bevy sample rays and planet visual reference; preserve ground texture.

**M4 — Upstream LUTs:** Port 256×64 transmittance and 32×32 multiple-scattering LUTs, validate numeric outputs, introduce cache; benchmark isolated passes.

**M5 — View-dependent features:** sky-view + aerial-perspective LUTs, quality switching for inside/outside planet; stability on approach.

**M6 — Multiple planets / polish:** disjoint atmosphere sorting, screen-area culling, runtime quality levels, throughput/VRAM budgets, packaged build, screenshots. Only consider advanced optimization after these pass.

Every milestone requires: changed file list, build result, shader logs, one or more screenshots/diagnostic outputs, GPU cost, regression tests, unresolved issues, and a commit. Cursor should not begin the next milestone on its own if tests fail.

## 8. Paste this as Cursor's starting task

> We are implementing physically based planet-local Hillaire atmospheric scattering in UE5.8 SINGULARITY. Treat `SINGULARITY_UE58_WebGPU_Hillaire_Atmosphere_Cursor_Guide.md` as the ordered requirements. Study the pinned MIT source at `https://github.com/JolifantoBambla/webgpu-sky-atmosphere`, including `src/shaders.ts`, `src/resources.ts`, `src/atmosphere.ts`, and the WGSL shader entries referenced in the guide. Preserve the upstream MIT notice. Inspect our Unreal source and existing `Pug/AtmosphereGen.cpp` / `FPugAtmosphereDescriptor` before making changes. Begin **only with M0 and M1**: audit the engine's 5.8 scene-view-extension callback signature and implement an editor-safe `PostConfigInit` shader plugin containing an RDG identity **compute** pass that returns unchanged pre-tonemap HDR scene color. Do not add scattering LUTs, postprocess Custom nodes, a global `USkyAtmosphere`, or Fresnel shells at this stage. Test PIE, viewport resize, Standalone, black-space look-away and packaged shader loading. Report the exact build status, GPU captures, changed files, risks and next milestone. If identity fails, stop and diagnose without attempting M2.

## 9. Source references and validation links

**Authoritative upstream:**

- Repo: https://github.com/JolifantoBambla/webgpu-sky-atmosphere
- MIT license: https://github.com/JolifantoBambla/webgpu-sky-atmosphere/blob/main/LICENSE
- README: https://raw.githubusercontent.com/JolifantoBambla/webgpu-sky-atmosphere/main/README.md
- LUT default resource sizes: https://raw.githubusercontent.com/JolifantoBambla/webgpu-sky-atmosphere/main/src/resources.ts
- WGSL module/entrypoint assembly: https://raw.githubusercontent.com/JolifantoBambla/webgpu-sky-atmosphere/main/src/shaders.ts
- LUT dependency scheduling: https://raw.githubusercontent.com/JolifantoBambla/webgpu-sky-atmosphere/main/src/lut-renderer.ts
- Hillaire EGSR 2020 paper: https://sebh.github.io/publications/egsr2020.pdf

**Unreal 5.8 verification:**

- Epic: Add Global Shaders — https://dev.epicgames.com/documentation/unreal-engine/adding-global-shaders-to-unreal-engine
- Epic: RDG — https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine
- Epic: UE5.8 `SubscribeToPostProcessingPass` overloads — https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/ISceneViewExtension/SubscribeToPostProcessingPass
- Epic: UE5.8 `FScreenPassTexture` — https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Renderer/FScreenPassTexture
- Epic: UE5.8 `FPostProcessMaterialInputs` — https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Renderer/FPostProcessMaterialInputs
- Epic: Post Process Materials / linear HDR — https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials-in-unreal-engine

**Compatibility caution:** The installed engine's actual `SceneViewExtension.h`, postprocess render pass callbacks and view uniform helpers are authoritative for compiling code. Public examples targeting UE5.3–5.7 may require changes on UE5.8. The details in this guide are a proposed architecture and contracts, not a compiled integration.
