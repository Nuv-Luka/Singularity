#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextQuadtree.h"

class FPlanetTerrainNextHeightField;
struct FPlanetTerrainNextHeightTile;

/**
 * CPU-built patch. Shared topology (grid + skirts) so pooled sections refill in place.
 *
 * VerticesCm = fine surface, relative to OriginM.
 * Coarse is the bilinear reconstruction of the parent patch's vertex grid (roots: coarse = fine),
 * so at Morph = 1 a child edge lies on the parent edge (exact T-junction).
 *
 * GPU packing (ProceduralMesh UV channels):
 *   UV0 = patch UV
 *   UV1 = CoarseOffset.xy (cm)   where CoarseOffset = Coarse - Fine
 *   UV2 = (CoarseOffset.z, MaxAbsOffsetCm)
 *   VertexColor.rgb = CoarseNormal * 0.5 + 0.5
 */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextPatchData
{
	FPlanetTerrainNextNodeId Id;
	FVector3d OriginM = FVector3d::ZeroVector;
	TArray<FVector> VerticesCm;
	TArray<FVector> CoarseVerticesCm;
	TArray<FVector> Normals;
	TArray<FVector> CoarseNormals;
	TArray<FVector> Tangents;
	TArray<FVector2D> UVs;
	TArray<FVector2D> UV1;
	TArray<FVector2D> UV2;
	TArray<FLinearColor> VertexColors;
	double MinHeightM = 0.0;
	double MaxHeightM = 0.0;
	double SkirtDepthM = 0.0;
	double MaxAbsCoarseDeltaCm = 0.0;
};

namespace PlanetTerrainNextPatch
{
inline constexpr int32 GridN = 16;
inline constexpr int32 Row = GridN + 1;
inline constexpr int32 GridVertexCount = Row * Row;
inline constexpr int32 SkirtVertexCount = 4 * Row;
inline constexpr int32 VertexCount = GridVertexCount + SkirtVertexCount;

/** Shared index buffer: grid triangles, then two-sided skirt quads. */
SINGULARITYPLANETTERRAINNEXT_API const TArray<int32>& Indices();

SINGULARITYPLANETTERRAINNEXT_API double VertexSpacingMeters(double RadiusM, int32 Level);
SINGULARITYPLANETTERRAINNEXT_API double MinWavelengthMeters(double RadiusM, int32 Level);
SINGULARITYPLANETTERRAINNEXT_API FVector3d VertexDirection(const FPlanetTerrainNextNodeId& Id, int32 I, int32 J);

/**
 * Skirt drop (metres). Uses 2·GE+1 but capped so coarse LODs (L0/L1) don't hang
 * multi-kilometre two-sided curtains that look like inverted faces from orbit.
 */
SINGULARITYPLANETTERRAINNEXT_API double RecommendedSkirtDepthM(double GeometricErrorM, double VertexSpacingM);

/**
 * Morph weight from projected pixel error (DSE convention adapted to SSE).
 * 0 = full fine detail, 1 = exactly the parent-compatible coarse surface.
 * Morph = 1 - smoothstep(Target, Target * EndFactor, PixelError).
 */
SINGULARITYPLANETTERRAINNEXT_API float MorphFromPixelError(double PixelError, double TargetPixelError, double EndFactor = 2.0);

SINGULARITYPLANETTERRAINNEXT_API void Build(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextNodeId& Id,
	double SkirtDepthM,
	FPlanetTerrainNextPatchData& Out);

/** Build mesh positions from a canonical height tile (once-per-residency CPU fallback). */
SINGULARITYPLANETTERRAINNEXT_API void BuildFromTile(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextHeightTile& Tile,
	double SkirtDepthM,
	FPlanetTerrainNextPatchData& Out);

/**
 * Max |Fine - Coarse| and edge T-junction residual against the parent mesh edge.
 * OutMaxMidpointGapM is the gap between an odd edge midpoint's coarse position and the
 * linear parent edge (should be ~0). OutMaxFineParentGapM is fine-vs-parent-edge at Morph=0.
 */
SINGULARITYPLANETTERRAINNEXT_API void MeasureParentEdgeGaps(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextNodeId& ChildId,
	double& OutMaxMidpointGapM,
	double& OutMaxFineParentGapM);
}
