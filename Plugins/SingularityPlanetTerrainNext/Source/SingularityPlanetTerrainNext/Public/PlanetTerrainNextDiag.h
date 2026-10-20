#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextQuadtree.h"

class UMaterialInterface;
struct FPlanetTerrainNextPatchData;

enum class EPlanetTerrainNextDiagMode : int32
{
	SinglePatch = 0,
	SixFaces = 1
};

/** Snapshot of singularity.TerrainNext.Diag.* CVars. */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextDiagSettings
{
	bool bEnable = false;
	EPlanetTerrainNextDiagMode Mode = EPlanetTerrainNextDiagMode::SixFaces;
	int32 Level = 0;
	int32 Face = 0;
	/** 0 = radius-only sphere; 1 = full HeightField. */
	bool bHeight = false;
	bool bTwoSided = true;
	/** Always ClearMeshSection + Create (never UpdateMeshSection). */
	bool bForceFreshSection = true;

	static FPlanetTerrainNextDiagSettings Read();

	/** Stable key for rebuild-on-change. */
	uint32 GetRebuildKey() const;
};

struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextPatchValidation
{
	int32 TriangleCount = 0;
	int32 GridTriangleCount = 0;
	int32 InvalidIndexCount = 0;
	int32 NonFiniteVertexCount = 0;
	int32 DegenerateTriangleCount = 0;
	int32 InwardFacingGridCount = 0;
	bool bOk = false;
};

namespace PlanetTerrainNextDiag
{
/** Versioned unlit diagnostic material (Color + TwoSided). */
SINGULARITYPLANETTERRAINNEXT_API UMaterialInterface* GetOrCreateMaterial(bool bTwoSided);

/**
 * Validate CPU patch geometry against shared Indices().
 * Grid tris must face outward from OriginM + vertex (planet-centered).
 */
SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextPatchValidation ValidatePatch(
	const FPlanetTerrainNextPatchData& Patch,
	bool bValidateSkirts = false);

/** Collect node ids for the current diag mode (no streaming). */
SINGULARITYPLANETTERRAINNEXT_API void CollectNodes(
	const FPlanetTerrainNextDiagSettings& Settings,
	TArray<FPlanetTerrainNextNodeId>& OutNodes);
}
