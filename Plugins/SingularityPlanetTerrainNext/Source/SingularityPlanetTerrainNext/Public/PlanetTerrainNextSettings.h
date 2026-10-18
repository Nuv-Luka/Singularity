#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextQuadtree.h"

enum class EPlanetTerrainNextDebugColor : int32
{
	Regolith = 0,
	LodLevel = 1,
	Face = 2,
	Morph = 3
};

/** Snapshot of singularity.TerrainNext.* console variables, read once per tick. */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextRuntimeSettings
{
	FPlanetTerrainNextLodSettings Lod;
	double BuildBudgetMs = 2.0;
	bool bPauseBuilds = false;
	/** Child index (0-3) whose builds are refused; -1 = none. Coverage test hook. */
	int32 DenyChildIndex = -1;
	EPlanetTerrainNextDebugColor DebugColor = EPlanetTerrainNextDebugColor::Regolith;
	bool bShowStats = true;
	/** Shader geomorph: Morph = 1 - smoothstep(Target, Target * MorphEnd, PixelError). */
	bool bMorphEnabled = true;
	double MorphEndFactor = 2.0;
	/** Force every drawn patch to this morph (0..1). < 0 = use SSE. Test hook. */
	float MorphForce = -1.0f;
	/** Max CPU height tiles kept (separate from GPU residency). */
	int32 CpuTileCacheSize = 1024;
	/** Max GPU atlas uploads (and mesh builds that need a new GPU slot) per frame. */
	int32 TileUploadsPerFrame = 16;
	/** Concurrent CPU height-tile bake workers (bench 2/4/6; default 4). */
	int32 MaxBakeWorkers = 4;
	/** Max queued bake requests (coverage can displace optional). */
	int32 MaxBakeQueue = 256;
	/** Speculative sibling warmup requests emitted per plan. */
	int32 PrefetchPerFrame = 8;
	/** Local collision geodesic radius (metres). */
	double CollisionRadiusM = 200.0;
	/** Target collision vertex spacing (metres). */
	double CollisionSpacingM = 4.0;
	int32 CollisionMaxPatches = 24;
	int32 CollisionCooksPerFrame = 2;
	/** Visual patches cast shadows when drawn. */
	bool bCastShadow = true;
	/**
	 * Peripheral LOD scale (1 = off). <1 coarsens frustum rim to save split budget.
	 * Copied into Lod.PeripheralLodScale each Read().
	 */
	double PeripheralLodScale = 1.0;

	static FPlanetTerrainNextRuntimeSettings Read();
};

namespace PlanetTerrainNextSettings
{
/** Writes a CVar with console priority so it cannot be shadowed by lower-priority code writes. */
SINGULARITYPLANETTERRAINNEXT_API void SetCVar(const TCHAR* Name, const FString& Value);
SINGULARITYPLANETTERRAINNEXT_API FString GetCVarString(const TCHAR* Name);
}
