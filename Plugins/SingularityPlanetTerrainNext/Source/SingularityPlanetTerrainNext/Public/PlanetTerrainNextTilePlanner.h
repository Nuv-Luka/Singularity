#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextQuadtree.h"
#include "PlanetTerrainNextTileId.h"

enum class EPlanetTerrainNextRequestClass : uint8
{
	/** Missing coarse coverage — never starve these. */
	Coverage = 0,
	/** Visible high screen-space error. */
	VisibleRefine = 1,
	/** Visible but lower urgency. */
	Frustum = 2,
	/** Bounded speculative warmup (neighbors). */
	Prefetch = 3,
};

struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextTileRequest
{
	FPlanetTerrainNextTileId TileId;
	FPlanetTerrainNextNodeId NodeId;
	double Priority = 0.0;
	EPlanetTerrainNextRequestClass Class = EPlanetTerrainNextRequestClass::Frustum;
	/** Soft deadline frame; overdue coverage work stays critical. */
	int64 DeadlineFrame = 0;
	bool bRequiredForCoverage = false;
};

struct FPlanetTerrainNextPlannerStats
{
	int32 Coverage = 0;
	int32 VisibleRefine = 0;
	int32 Frustum = 0;
	int32 Prefetch = 0;
	int32 Emitted = 0;
};

/**
 * Turns quadtree pending builds into a prioritized desired set.
 * Coverage vs refinement are separate: frustum culling never punches holes.
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextTilePlanner
{
public:
	/**
	 * @param Pending          Nodes the tree wants built (already priority-sorted).
	 * @param TargetPixelError LOD target — used to split VisibleRefine vs Frustum.
	 * @param Frame            Current frame (deadlines).
	 * @param MaxPrefetch      Cap on speculative neighbor requests (0 disables).
	 */
	void Plan(
		const TArray<FPlanetTerrainNextPendingBuild>& Pending,
		TFunctionRef<FPlanetTerrainNextTileId(const FPlanetTerrainNextNodeId&)> MakeTileId,
		double TargetPixelError,
		int64 Frame,
		int32 MaxPrefetch,
		TArray<FPlanetTerrainNextTileRequest>& OutRequests);

	const FPlanetTerrainNextPlannerStats& GetStats() const { return Stats; }

private:
	FPlanetTerrainNextPlannerStats Stats;
};
