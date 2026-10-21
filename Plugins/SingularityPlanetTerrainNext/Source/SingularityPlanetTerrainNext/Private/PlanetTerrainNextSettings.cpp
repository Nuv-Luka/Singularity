#include "PlanetTerrainNextSettings.h"

#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<float> CVarTargetPixelError(
	TEXT("singularity.TerrainNext.TargetPixelError"),
	4.0f,
	TEXT("Split a patch when its projected geometric error exceeds this many pixels."));

TAutoConsoleVariable<float> CVarLodQueueDistanceScale(
	TEXT("singularity.TerrainNext.LodQueueDistanceScale"),
	10.0f,
	TEXT("Maximum LOD queue/refine distance multiplier at MaxLevel. The multiplier ramps from 1x at L0; 10 = L0 x1 through L17 x10."));

TAutoConsoleVariable<int32> CVarMaxLevel(
	TEXT("singularity.TerrainNext.MaxLevel"),
	17,
	TEXT("Deepest quadtree level. Level L vertex spacing is 2R / (16 * 2^L); 17 is about 2.6 m on the Moon."));

TAutoConsoleVariable<int32> CVarMaxPatches(
	TEXT("singularity.TerrainNext.MaxPatches"),
	512,
	TEXT("Hard cap on quadtree nodes. When reached, parents stay drawn instead of splitting."));

TAutoConsoleVariable<float> CVarBuildBudgetMs(
	TEXT("singularity.TerrainNext.BuildBudgetMs"),
	6.0f,
	TEXT("Game-thread milliseconds per frame for CPU patch builds (default 6 ms; at least one build per frame)."));

TAutoConsoleVariable<bool> CVarFreezeLod(
	TEXT("singularity.TerrainNext.FreezeLod"),
	false,
	TEXT("Stop split/merge decisions. Pending builds still complete."));

TAutoConsoleVariable<bool> CVarPauseBuilds(
	TEXT("singularity.TerrainNext.PauseBuilds"),
	false,
	TEXT("Stop building patches. The tree keeps drawing what is resident."));

TAutoConsoleVariable<int32> CVarDenyChildIndex(
	TEXT("singularity.TerrainNext.DenyChildIndex"),
	-1,
	TEXT("Test hook: refuse to build child index 0-3 of every split. Parents must keep covering. -1 disables."));

TAutoConsoleVariable<int32> CVarDebugColor(
	TEXT("singularity.TerrainNext.DebugColor"),
	0,
	TEXT("0 = regolith, 1 = LOD level hue, 2 = cube face, 3 = morph weight (blue=fine, red=coarse)."));

TAutoConsoleVariable<bool> CVarShowStats(
	TEXT("singularity.TerrainNext.ShowStats"),
	true,
	TEXT("On-screen terrain stats line."));

TAutoConsoleVariable<bool> CVarMorphEnabled(
	TEXT("singularity.TerrainNext.MorphEnabled"),
	true,
	TEXT("Shader geomorph between fine and parent-compatible coarse. Uniform-only; no mesh rebuilds."));

TAutoConsoleVariable<float> CVarMorphEnd(
	TEXT("singularity.TerrainNext.MorphEnd"),
	3.5f,
	TEXT("Morph = 1 - smoothstep(Target, Target * MorphEnd, PixelError). Wider = softer LOD pops."));

TAutoConsoleVariable<float> CVarMorphForce(
	TEXT("singularity.TerrainNext.MorphForce"),
	-1.0f,
	TEXT("Force Morph on every drawn patch (0 = fine, 1 = coarse). Negative = drive from pixel error."));

TAutoConsoleVariable<float> CVarMorphSmooth(
	TEXT("singularity.TerrainNext.MorphSmooth"),
	0.25f,
	TEXT("Seconds to exponentially chase Morph toward its target. 0 = snap each frame."));

TAutoConsoleVariable<float> CVarMorphReveal(
	TEXT("singularity.TerrainNext.MorphReveal"),
	0.45f,
	TEXT("Seconds for a new child patch to blend from Morph=1 (parent) to its PE target."));

TAutoConsoleVariable<int32> CVarMorphHandoffFrames(
	TEXT("singularity.TerrainNext.MorphHandoffFrames"),
	6,
	TEXT("Frames to keep the parent drawn after children are ready (children hidden; no co-draw)."));

TAutoConsoleVariable<bool> CVarSkirtsEnabled(
	TEXT("singularity.TerrainNext.SkirtsEnabled"),
	true,
	TEXT("R9.1: when false, skirt depth is 0 (hides skirt geometry for winding diagnostics)."));

TAutoConsoleVariable<int32> CVarMorphCpuMaxLevel(
	TEXT("singularity.TerrainNext.MorphCpuMaxLevel"),
	2,
	TEXT("R9.1: LOD <= this bake Morph into vertices (no F16 UV WPO). Default 2 covers L0–L2."));

TAutoConsoleVariable<float> CVarMorphGpuMaxScaleCm(
	TEXT("singularity.TerrainNext.MorphGpuMaxScaleCm"),
	1000.0f,
	TEXT("R9.1: if MaxAbsCoarseDeltaCm exceeds this, force CPU morph (F16×scale quantization)."));

TAutoConsoleVariable<bool> CVarMorphWpoEnabled(
	TEXT("singularity.TerrainNext.MorphWpoEnabled"),
	true,
	TEXT("R9.1 diagnostic: false = disconnect WPO (CPU morph all patches, Morph scalar=0)."));

TAutoConsoleVariable<int32> CVarCpuTileCache(
	TEXT("singularity.TerrainNext.CpuTileCache"),
	1024,
	TEXT("Max canonical CPU height tiles. Separate from GPU atlas residency."));

TAutoConsoleVariable<bool> CVarDiskCache(
	TEXT("singularity.TerrainNext.DiskCache"),
	true,
	TEXT("R8: persist height tiles to Saved/SingularityPlanetTerrainNext/Tiles."));

TAutoConsoleVariable<int32> CVarDiskCacheMaxMB(
	TEXT("singularity.TerrainNext.DiskCacheMaxMB"),
	512,
	TEXT("R8: soft cap on disk tile folder size in megabytes (0 = unlimited)."));

TAutoConsoleVariable<int32> CVarTileUploadsPerFrame(
	TEXT("singularity.TerrainNext.TileUploadsPerFrame"),
	16,
	TEXT("Max new GPU atlas uploads (and associated mesh builds) per frame."));

TAutoConsoleVariable<int32> CVarMeshCommitsPerFrame(
	TEXT("singularity.TerrainNext.MeshCommitsPerFrame"),
	4,
	TEXT("Maximum ProceduralMeshComponent section commits per frame during runtime refinement."));

TAutoConsoleVariable<int32> CVarMaxBakeWorkers(
	TEXT("singularity.TerrainNext.MaxBakeWorkers"),
	4,
	TEXT("Concurrent CPU height-tile bake workers (2/4/6). Workers never touch UObjects."));

TAutoConsoleVariable<int32> CVarMaxBakeQueue(
	TEXT("singularity.TerrainNext.MaxBakeQueue"),
	256,
	TEXT("Max queued height-tile bake requests. Coverage can displace optional prefetch."));

TAutoConsoleVariable<int32> CVarPrefetchPerFrame(
	TEXT("singularity.TerrainNext.PrefetchPerFrame"),
	8,
	TEXT("Bounded speculative sibling tile requests per planner pass. 0 disables."));

TAutoConsoleVariable<float> CVarCollisionRadius(
	TEXT("singularity.TerrainNext.CollisionRadiusM"),
	200.0f,
	TEXT("Geodesic metres of local collision coverage around the camera/vehicle."));

TAutoConsoleVariable<float> CVarCollisionMaxAltitude(
	TEXT("singularity.TerrainNext.CollisionMaxAltitudeM"),
	10000.0f,
	TEXT("Maximum altitude for local collision residency; visual terrain streaming is unaffected."));

TAutoConsoleVariable<float> CVarCollisionSpacing(
	TEXT("singularity.TerrainNext.CollisionSpacingM"),
	4.0f,
	TEXT("Target collision patch vertex spacing in metres (~2–5 m for landing)."));

TAutoConsoleVariable<int32> CVarCollisionMaxPatches(
	TEXT("singularity.TerrainNext.CollisionMaxPatches"),
	24,
	TEXT("Hard cap on cooked local collision patches."));

TAutoConsoleVariable<int32> CVarCollisionCooksPerFrame(
	TEXT("singularity.TerrainNext.CollisionCooksPerFrame"),
	2,
	TEXT("Max new collision mesh cooks per frame."));

TAutoConsoleVariable<bool> CVarCastShadow(
	TEXT("singularity.TerrainNext.CastShadow"),
	true,
	TEXT("Drawn visual patches cast shadows (bounds already cover WPO morph)."));

TAutoConsoleVariable<float> CVarPeripheralLodScale(
	TEXT("singularity.TerrainNext.PeripheralLodScale"),
	1.0f,
	TEXT("R8: multiply projected pixel error toward frustum rim (1=off, 0.7=mild periphery coarsening)."));

TAutoConsoleVariable<bool> CVarDetailProjection(
	TEXT("singularity.TerrainNext.DetailProjection"),
	true,
	TEXT("R9.3: sample root-face albedo/normal detail textures on patches."));

TAutoConsoleVariable<int32> CVarDetailResolution(
	TEXT("singularity.TerrainNext.DetailResolution"),
	512,
	TEXT("R9.3: root detail texture resolution (64–1024). Takes effect on next InitializeTerrain."));

TAutoConsoleVariable<int32> CVarDetailMaxConcurrentBakes(
	TEXT("singularity.TerrainNext.DetailMaxConcurrentBakes"),
	2,
	TEXT("R9.3: concurrent CPU detail-tile bake workers (1–6)."));

TAutoConsoleVariable<int32> CVarDetailUploadsPerFrame(
	TEXT("singularity.TerrainNext.DetailUploadsPerFrame"),
	1,
	TEXT("R9.3: max detail texture GPU uploads per frame (0–6)."));

TAutoConsoleVariable<bool> CVarForceFreshMeshSection(
	TEXT("singularity.TerrainNext.ForceFreshMeshSection"),
	false,
	TEXT("Diagnostic: force Clear+Create PMC sections on node upload."));

TAutoConsoleVariable<bool> CVarEnableDitheredLOD(
	TEXT("singularity.TerrainNext.EnableDitheredLOD"),
	true,
	TEXT("R10.2: complementary dithered parent/child LOD crossfade (masked material)."));

TAutoConsoleVariable<bool> CVarEmergencyMode(
	TEXT("singularity.TerrainNext.EmergencyMode"),
	false,
	TEXT("Opt-in recovery preset: disables terrain shadows, dither, disk writes and collision cooks; caps patches."));

TAutoConsoleVariable<bool> CVarEnableLegacyMorph(
	TEXT("singularity.TerrainNext.EnableLegacyMorph"),
	false,
	TEXT("R10.2: enable PE/WPO/CPU morph path (off = Morph forced to 0)."));

TAutoConsoleVariable<float> CVarLodTransitionSeconds(
	TEXT("singularity.TerrainNext.LodTransitionSeconds"),
	0.45f,
	TEXT("R10.2: sibling-group dither fade duration in seconds."));

TAutoConsoleVariable<int32> CVarMaxConcurrentTransitionGroups(
	TEXT("singularity.TerrainNext.MaxConcurrentTransitionGroups"),
	24,
	TEXT("R10.2: max simultaneous dither fade groups (excess use exclusive handoff)."));

TAutoConsoleVariable<bool> CVarPerfCapture(
	TEXT("singularity.TerrainNext.PerfCapture"),
	false,
	TEXT("R10 measurement: append per-frame terrain telemetry to Saved/Logs/PlanetTerrainNextPerf.csv."));

TAutoConsoleVariable<FString> CVarPerfScenario(
	TEXT("singularity.TerrainNext.PerfScenario"),
	TEXT("UNSET"),
	TEXT("R10 measurement scenario label written to the terrain performance CSV."));
} // namespace

FPlanetTerrainNextRuntimeSettings FPlanetTerrainNextRuntimeSettings::Read()
{
	FPlanetTerrainNextRuntimeSettings Out;
	Out.Lod.TargetPixelError = FMath::Max(0.25, static_cast<double>(CVarTargetPixelError.GetValueOnGameThread()));
	Out.Lod.QueueDistanceScale = FMath::Clamp(static_cast<double>(CVarLodQueueDistanceScale.GetValueOnGameThread()), 1.0, 16.0);
	Out.Lod.MaxLevel = FMath::Clamp(CVarMaxLevel.GetValueOnGameThread(), 0, 24);
	Out.Lod.MaxNodes = FMath::Clamp(CVarMaxPatches.GetValueOnGameThread(), 6, 16384);
	Out.Lod.bFreeze = CVarFreezeLod.GetValueOnGameThread();
	Out.BuildBudgetMs = FMath::Max(0.0, static_cast<double>(CVarBuildBudgetMs.GetValueOnGameThread()));
	Out.bPauseBuilds = CVarPauseBuilds.GetValueOnGameThread();
	Out.DenyChildIndex = FMath::Clamp(CVarDenyChildIndex.GetValueOnGameThread(), -1, 3);
	Out.DebugColor = static_cast<EPlanetTerrainNextDebugColor>(FMath::Clamp(CVarDebugColor.GetValueOnGameThread(), 0, 3));
	Out.bShowStats = CVarShowStats.GetValueOnGameThread();
	Out.bMorphEnabled = CVarMorphEnabled.GetValueOnGameThread();
	Out.MorphEndFactor = FMath::Max(1.01, static_cast<double>(CVarMorphEnd.GetValueOnGameThread()));
	Out.MorphForce = CVarMorphForce.GetValueOnGameThread();
	Out.MorphSmoothSeconds = FMath::Max(0.0f, CVarMorphSmooth.GetValueOnGameThread());
	Out.MorphRevealSeconds = FMath::Max(0.0f, CVarMorphReveal.GetValueOnGameThread());
	Out.MorphHandoffFrames = FMath::Clamp(CVarMorphHandoffFrames.GetValueOnGameThread(), 1, 120);
	Out.bSkirtsEnabled = CVarSkirtsEnabled.GetValueOnGameThread();
	Out.MorphCpuMaxLevel = FMath::Clamp(CVarMorphCpuMaxLevel.GetValueOnGameThread(), -1, 24);
	Out.MorphGpuMaxScaleCm = FMath::Max(1.0f, CVarMorphGpuMaxScaleCm.GetValueOnGameThread());
	Out.bMorphWpoEnabled = CVarMorphWpoEnabled.GetValueOnGameThread();
	Out.CpuTileCacheSize = FMath::Clamp(CVarCpuTileCache.GetValueOnGameThread(), 8, 16384);
	Out.bDiskCache = CVarDiskCache.GetValueOnGameThread();
	Out.DiskCacheMaxMB = FMath::Clamp(CVarDiskCacheMaxMB.GetValueOnGameThread(), 0, 1024 * 1024);
	Out.TileUploadsPerFrame = FMath::Clamp(CVarTileUploadsPerFrame.GetValueOnGameThread(), 1, 256);
	Out.MeshCommitsPerFrame = FMath::Clamp(CVarMeshCommitsPerFrame.GetValueOnGameThread(), 1, 64);
	Out.MaxBakeWorkers = FMath::Clamp(CVarMaxBakeWorkers.GetValueOnGameThread(), 1, 16);
	Out.MaxBakeQueue = FMath::Clamp(CVarMaxBakeQueue.GetValueOnGameThread(), 8, 4096);
	Out.PrefetchPerFrame = FMath::Clamp(CVarPrefetchPerFrame.GetValueOnGameThread(), 0, 64);
	Out.CollisionRadiusM = FMath::Clamp(static_cast<double>(CVarCollisionRadius.GetValueOnGameThread()), 10.0, 5000.0);
	Out.CollisionMaxAltitudeM = FMath::Clamp(static_cast<double>(CVarCollisionMaxAltitude.GetValueOnGameThread()), 100.0, 1000000.0);
	Out.CollisionSpacingM = FMath::Clamp(static_cast<double>(CVarCollisionSpacing.GetValueOnGameThread()), 0.5, 50.0);
	Out.CollisionMaxPatches = FMath::Clamp(CVarCollisionMaxPatches.GetValueOnGameThread(), 1, 128);
	Out.CollisionCooksPerFrame = FMath::Clamp(CVarCollisionCooksPerFrame.GetValueOnGameThread(), 0, 16);
	Out.bCastShadow = CVarCastShadow.GetValueOnGameThread();
	Out.PeripheralLodScale = FMath::Clamp(static_cast<double>(CVarPeripheralLodScale.GetValueOnGameThread()), 0.25, 1.0);
	Out.Lod.PeripheralLodScale = Out.PeripheralLodScale;
	Out.bDetailProjection = CVarDetailProjection.GetValueOnGameThread();
	Out.DetailResolution = FMath::Clamp(CVarDetailResolution.GetValueOnGameThread(), 64, 1024);
	Out.DetailMaxConcurrentBakes = FMath::Clamp(CVarDetailMaxConcurrentBakes.GetValueOnGameThread(), 1, 6);
	Out.DetailUploadsPerFrame = FMath::Clamp(CVarDetailUploadsPerFrame.GetValueOnGameThread(), 0, 6);
	Out.bForceFreshMeshSection = CVarForceFreshMeshSection.GetValueOnGameThread();
	Out.bEnableDitheredLOD = CVarEnableDitheredLOD.GetValueOnGameThread();
	Out.bEnableLegacyMorph = CVarEnableLegacyMorph.GetValueOnGameThread();
	Out.LodTransitionSeconds = FMath::Clamp(CVarLodTransitionSeconds.GetValueOnGameThread(), 0.05f, 5.0f);
	Out.MaxConcurrentTransitionGroups = FMath::Clamp(CVarMaxConcurrentTransitionGroups.GetValueOnGameThread(), 1, 128);
	Out.bEmergencyMode = CVarEmergencyMode.GetValueOnGameThread();
	if (Out.bEmergencyMode)
	{
		Out.bCastShadow = false;
		Out.bEnableDitheredLOD = false;
		Out.bDiskCache = false;
		Out.CollisionCooksPerFrame = 0;
		Out.Lod.MaxNodes = FMath::Min(Out.Lod.MaxNodes, 64);
	}
	Out.bPerfCapture = CVarPerfCapture.GetValueOnGameThread();
	Out.PerfScenario = CVarPerfScenario.GetValueOnGameThread();
	return Out;
}

namespace PlanetTerrainNextSettings
{
void SetCVar(const TCHAR* Name, const FString& Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(*Value, ECVF_SetByConsole);
	}
}

FString GetCVarString(const TCHAR* Name)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return Variable->GetString();
	}
	return FString();
}
}
