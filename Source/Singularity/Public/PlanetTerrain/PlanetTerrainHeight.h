#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"

/**
 * Deterministic height field over the unit sphere (DeepSpaceEngine PlanetTerrain subset).
 * Height depends only on descriptor + unit direction + sample spacing / detail level.
 */
struct FPlanetTerrainHeight
{
	explicit FPlanetTerrainHeight(const FRockyPlanetDescriptor& InPlanet);

	const FRockyPlanetDescriptor& GetPlanet() const { return Planet; }
	double GetAmplitudeMeters() const { return AmplitudeM; }
	double GetRadiusMeters() const { return Planet.RadiusMeters; }

	/** Full-detail height (metres, signed) — tests / unrestricted collision. */
	double EvaluateHeightUnrestricted(const FVector3d& UnitDirection) const;

	/**
	 * Band-limited height. DetailLevel >= 0 maps to sample spacing ≈ circumference / 2^(DetailLevel+4).
	 * DetailLevel < 0 uses SampleSpacingMeters directly when provided via overload.
	 */
	double EvaluateHeight(const FVector3d& UnitDirection, int32 DetailLevel) const;

	/** Spacing-explicit path (metres between samples; 0 = full detail). */
	double EvaluateHeightAtSpacing(const FVector3d& UnitDirection, double SampleSpacingMeters) const;

	/** Dual fine/coarse for geomorph (coarseSpacing should be ~2× fine). */
	void EvaluateHeight2(
		const FVector3d& UnitDirection,
		double FineSpacingMeters,
		double CoarseSpacingMeters,
		double& OutFine,
		double& OutCoarse) const;

	/**
	 * Float32 field mirrored by PlanetTerrainHeightBakeCS (Phase 7).
	 * Fewer crater octaves + uint32 hash — see DOCS/PlanetTerrain/Phase7_GpuBake.md tolerances.
	 */
	float EvaluateHeightGpuFloat(const FVector3d& UnitDirection, float SampleSpacingMeters) const;
	void EvaluateHeight2GpuFloat(
		const FVector3d& UnitDirection,
		float FineSpacingMeters,
		float CoarseSpacingMeters,
		float& OutFine,
		float& OutCoarse) const;

	/** Packed uniforms for the compute bake (must stay in sync with .ush). */
	void GetGpuBakeUniforms(
		float& OutAmplitudeM,
		float& OutRadiusM,
		float& OutContinentFreq,
		float& OutMountainFreq,
		float& OutDetailFreq,
		float& OutCraterFreq0,
		float& OutCraterDensity,
		float& OutCraterWeight,
		float& OutMountainWeight,
		uint32& OutNoiseSeed) const;

	/** Coarse LOD-stable elevation for surface-anchored split distance. */
	double LodAnchorElevation(const FVector3d& UnitDirection) const;

	/** Simple crater profile for tests / tooling. */
	static double EvaluateCraterProfile(
		double DistanceFromCenter,
		double CraterRadius,
		double CraterDepth,
		double RimHeight);

private:
	FRockyPlanetDescriptor Planet;
	double AmplitudeM = 0.0;
	double ContinentFreq = 1.4;
	double MountainFreq = 4.0;
	double DetailFreq = 36.0;
	double CraterFreq0 = 18.0;
	double CraterDensity = 0.7;
	double CraterWeight = 0.55;
	double MountainWeight = 0.35;
	uint64 NoiseSeed = 0;

	double ValueNoise(double X, double Y, double Z) const;
	double Fbm(const FVector3d& P, double Octaves, double Frequency, double Lacunarity, double Gain) const;
	double Ridged(const FVector3d& P, double Octaves, double Frequency, double Lacunarity, double Gain) const;
	double OctavesFor(double BaseFreq, double SampleSpacing, int32 MaxOctaves) const;
	double LayerGate(double Freq, double SampleSpacing) const;
	double CraterField(const FVector3d& UnitDir, double SampleSpacing) const;
	double CraterLayer(const FVector3d& UnitDir, double Freq, uint64 Salt, double SampleSpacing, double Density) const;
	void CellFeature(int64 Cx, int64 Cy, int64 Cz, uint64 Salt, double& Fx, double& Fy, double& Fz, double& Rand) const;
};
