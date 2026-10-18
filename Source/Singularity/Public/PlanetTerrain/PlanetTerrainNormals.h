#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainHeight.h"

/**
 * Halo / spherical finite-difference normals in planet-local space.
 * Samples heights outside the tile so equal-LOD borders share the same gradient.
 */
struct FPlanetTerrainNormals
{
	/**
	 * OutNormals: Resolution+1 squared. HeightAtDir must return displayed height (metres)
	 * for any unit direction (including off-tile).
	 */
	static void ComputeFromHeightField(
		const FPlanetTerrainHeight& Height,
		uint8 Face,
		double U0, double V0, double U1, double V1,
		int32 Resolution,
		TFunctionRef<double(const FVector3d& UnitDir)> HeightAtDir,
		TArray<FVector>& OutNormals);

	/** Convenience: fine band-limited height (Morph = 0). */
	static void ComputeFine(
		const FPlanetTerrainHeight& Height,
		uint8 Face,
		double U0, double V0, double U1, double V1,
		int32 Resolution,
		double SampleSpacingM,
		TArray<FVector>& OutNormals);

	/** Displayed height = lerp(fine, coarse, Morph) via EvaluateHeight2. */
	static void ComputeMorphed(
		const FPlanetTerrainHeight& Height,
		uint8 Face,
		double U0, double V0, double U1, double V1,
		int32 Resolution,
		double FineSpacingM,
		double CoarseSpacingM,
		float Morph,
		TArray<FVector>& OutNormals);
};
