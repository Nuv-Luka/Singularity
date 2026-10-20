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
	/** Wider band softens LOD pops (DSE used ~2; 3.5 is a gentler default). */
	double MorphEndFactor = 3.5;
	/** Force every drawn patch to this morph (0..1). < 0 = use SSE. Test hook. */
	float MorphForce = -1.0f;
	/** Exponential chase time for Morph toward its target (seconds). 0 = snap. */
	float MorphSmoothSeconds = 0.25f;
	/** After a child patch appears, blend Morph from 1 (parent) → PE target over this many seconds. */
	float MorphRevealSeconds = 0.45f;
	/** Keep parent drawn this many frames after children are ready (no co-draw). */
	int32 MorphHandoffFrames = 6;
	/** When false, skirt depth is forced to 0 (R9.1 diagnostic). Topology unchanged. */
	bool bSkirtsEnabled = true;
	/**
	 * Levels at or below this use CPU morph (no PMC UV WPO). Also used when
	 * MaxAbsCoarseDeltaCm exceeds MorphGpuMaxScaleCm (F16×scale quantization).
	 */
	int32 MorphCpuMaxLevel = 2;
	/** Above this |Δ|cm, force CPU morph even at fine levels (F16 ULP×scale ≈ cm error). */
	float MorphGpuMaxScaleCm = 1000.0f;
	/** When false, all patches CPU-morph and WPO Morph is forced to 0 (disconnect WPO). */
	bool bMorphWpoEnabled = true;
	/** Max CPU height tiles kept (separate from GPU residency). */
	int32 CpuTileCacheSize = 1024;
	/** Persist height tiles under Saved/SingularityPlanetTerrainNext/Tiles. */
	bool bDiskCache = true;
	/** Soft cap on disk tile folder size (megabytes). 0 = unlimited. */
	int32 DiskCacheMaxMB = 512;
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

	/** R9.3: project root-face albedo/normal textures onto patches. */
	bool bDetailProjection = true;
	/** Root detail texture edge length (64–1024). Applied on next terrain init. */
	int32 DetailResolution = 512;
	/** Concurrent detail-tile bake workers (independent of height-tile workers). */
	int32 DetailMaxConcurrentBakes = 2;
	/** GPU detail texture uploads per frame. */
	int32 DetailUploadsPerFrame = 1;
	/** Diagnostic switch; compatible topology is reused by default. */
	bool bForceFreshMeshSection = false;

	/** R10.2: complementary dithered parent/child LOD crossfade (masked). */
	bool bEnableDitheredLOD = true;
	/** R10.2: keep PE/WPO/CPU morph path. Off = Morph forced to 0 (fine surface). */
	bool bEnableLegacyMorph = false;
	/** Seconds for a sibling-group fade (Progress 0→1). */
	float LodTransitionSeconds = 0.45f;
	/** Cap concurrent fade groups; excess splits fall back to exclusive handoff. */
	int32 MaxConcurrentTransitionGroups = 24;

	static FPlanetTerrainNextRuntimeSettings Read();
};

namespace PlanetTerrainNextSettings
{
/** Writes a CVar with console priority so it cannot be shadowed by lower-priority code writes. */
SINGULARITYPLANETTERRAINNEXT_API void SetCVar(const TCHAR* Name, const FString& Value);
SINGULARITYPLANETTERRAINNEXT_API FString GetCVarString(const TCHAR* Name);
}
