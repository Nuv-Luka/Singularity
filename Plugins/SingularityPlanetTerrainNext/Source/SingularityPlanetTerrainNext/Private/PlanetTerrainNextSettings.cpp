#include "PlanetTerrainNextSettings.h"

#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<float> CVarTargetPixelError(
	TEXT("singularity.TerrainNext.TargetPixelError"),
	4.0f,
	TEXT("Split a patch when its projected geometric error exceeds this many pixels."));

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
	2.0f,
	TEXT("Game-thread milliseconds per frame for CPU patch builds (at least one build per frame)."));

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
	2.0f,
	TEXT("Morph = 1 - smoothstep(Target, Target * MorphEnd, PixelError). 2 matches DeepSpaceEngine's 1x..2x band."));

TAutoConsoleVariable<float> CVarMorphForce(
	TEXT("singularity.TerrainNext.MorphForce"),
	-1.0f,
	TEXT("Force Morph on every drawn patch (0 = fine, 1 = coarse). Negative = drive from pixel error."));

TAutoConsoleVariable<int32> CVarCpuTileCache(
	TEXT("singularity.TerrainNext.CpuTileCache"),
	1024,
	TEXT("Max canonical CPU height tiles. Separate from GPU atlas residency."));

TAutoConsoleVariable<int32> CVarTileUploadsPerFrame(
	TEXT("singularity.TerrainNext.TileUploadsPerFrame"),
	16,
	TEXT("Max new GPU atlas uploads (and associated mesh builds) per frame."));

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
} // namespace

FPlanetTerrainNextRuntimeSettings FPlanetTerrainNextRuntimeSettings::Read()
{
	FPlanetTerrainNextRuntimeSettings Out;
	Out.Lod.TargetPixelError = FMath::Max(0.25, static_cast<double>(CVarTargetPixelError.GetValueOnGameThread()));
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
	Out.CpuTileCacheSize = FMath::Clamp(CVarCpuTileCache.GetValueOnGameThread(), 8, 16384);
	Out.TileUploadsPerFrame = FMath::Clamp(CVarTileUploadsPerFrame.GetValueOnGameThread(), 1, 256);
	Out.MaxBakeWorkers = FMath::Clamp(CVarMaxBakeWorkers.GetValueOnGameThread(), 1, 16);
	Out.MaxBakeQueue = FMath::Clamp(CVarMaxBakeQueue.GetValueOnGameThread(), 8, 4096);
	Out.PrefetchPerFrame = FMath::Clamp(CVarPrefetchPerFrame.GetValueOnGameThread(), 0, 64);
	Out.CollisionRadiusM = FMath::Clamp(static_cast<double>(CVarCollisionRadius.GetValueOnGameThread()), 10.0, 5000.0);
	Out.CollisionSpacingM = FMath::Clamp(static_cast<double>(CVarCollisionSpacing.GetValueOnGameThread()), 0.5, 50.0);
	Out.CollisionMaxPatches = FMath::Clamp(CVarCollisionMaxPatches.GetValueOnGameThread(), 1, 128);
	Out.CollisionCooksPerFrame = FMath::Clamp(CVarCollisionCooksPerFrame.GetValueOnGameThread(), 0, 16);
	Out.bCastShadow = CVarCastShadow.GetValueOnGameThread();
	Out.PeripheralLodScale = FMath::Clamp(static_cast<double>(CVarPeripheralLodScale.GetValueOnGameThread()), 0.25, 1.0);
	Out.Lod.PeripheralLodScale = Out.PeripheralLodScale;
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
