#pragma once

#include "CoreMinimal.h"

/**
 * One cube face of the R1 static shell.
 * PositionsMeters are planet-local meters (double).
 * VerticesCm are actor-local centimeters (UE5 FVector, double under LWC).
 * The actor origin is the moon center. Actor scale stays 1.
 */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextFaceGeometry
{
	TArray<FVector3d> PositionsMeters;
	TArray<FVector> VerticesCm;
	TArray<FVector> Normals;
	TArray<int32> Triangles;
	TArray<FVector2D> UVs;
};

/** Build one face. CellsPerEdge is clamped to [1, 64]. No height displacement. */
SINGULARITYPLANETTERRAINNEXT_API void BuildPlanetTerrainNextFaceMesh(
	int32 Face,
	int32 CellsPerEdge,
	double RadiusMeters,
	FPlanetTerrainNextFaceGeometry& OutGeometry);
