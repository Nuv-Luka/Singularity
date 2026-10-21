#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextGeology.h"
#include "PlanetTerrainNextTypes.h"

/** Height split by layer, in meters relative to the base radius. */
struct FPlanetTerrainNextHeightComponents
{
	double Macro = 0.0;
	double Craters = 0.0;
	double Roughness = 0.0;
	double Geology = 0.0;

	double Total() const
	{
		return Macro + Craters + Roughness + Geology;
	}
};

struct FPlanetTerrainNextCrater
{
	FVector3d Center = FVector3d::UnitZ();
	double RadiusM = 0.0;
	double DepthM = 0.0;
	double RimM = 0.0;
	double CosInfluence = 1.0;
};

/**
 * Canonical height for one airless moon.
 *
 * Pure: the result depends only on the descriptor (seed, version, radius),
 * the unit direction, and MinResolvedWavelengthM. No camera, tile, or frame state.
 *
 * Band limit follows DeepSpaceEngine PlanetTerrain.OctavesFor: a feature of
 * wavelength L is fully present when L >= 2 * MinResolvedWavelengthM and absent
 * when L <= MinResolvedWavelengthM, with a smoothstep in between. Pass 0 for full detail.
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextHeightField
{
public:
	explicit FPlanetTerrainNextHeightField(const FPlanetTerrainNextDescriptor& Descriptor);

	double HeightAt(const FVector3d& UnitDirection, double MinResolvedWavelengthM) const;
	FPlanetTerrainNextHeightComponents EvaluateComponents(const FVector3d& UnitDirection, double MinResolvedWavelengthM) const;
	FPlanetTerrainNextSurfaceSample SampleGeology(const FVector3d& UnitDirection, double SampleFootprintMeters) const;

	/** Bowl + rim of one crater, band-limited. Used by tests to track a single crater across LODs. */
	double CraterContribution(int32 CraterIndex, const FVector3d& UnitDirection, double MinResolvedWavelengthM) const;

	/**
	 * Estimated max deviation between a mesh with this vertex spacing and the
	 * full-resolution surface: curvature sag + removed/under-sampled noise + crater
	 * interpolation. Used for screen-space error only; not a proven bound.
	 */
	double GeometricErrorMeters(double VertexSpacingM) const;

	double GetRadiusMeters() const { return RadiusM; }
	double GetMinHeightMeters() const { return MinHeightM; }
	double GetMaxHeightMeters() const { return MaxHeightM; }
	double GetHeightBoundMeters() const { return FMath::Max(-MinHeightM, MaxHeightM); }
	const TArray<FPlanetTerrainNextCrater>& GetCraters() const { return Craters; }
	const FPlanetTerrainNextGeologyModel& GetGeologyModel() const { return *GeologyModel; }

	/** Seed + generator version + radius. Cache identity must include this. */
	uint64 GetIdentityHash() const { return IdentityHash; }

	static double BandWeight(double WavelengthM, double MinResolvedWavelengthM);

private:
	struct FOctave
	{
		double WavelengthM = 0.0;
		double AmplitudeM = 0.0;
		double Frequency = 0.0;
		FVector3d Offset = FVector3d::ZeroVector;
		uint64 Seed = 0;
	};

	double SumOctaves(const TArray<FOctave>& Octaves, const FVector3d& Direction, double MinResolvedWavelengthM) const;
	double CraterProfile(const FPlanetTerrainNextCrater& Crater, const FVector3d& Direction, double Weight, double& OutBowl, double& OutRim) const;

	double RadiusM = 0.0;
	uint64 Seed = 0;
	uint64 IdentityHash = 0;
	TSharedPtr<FPlanetTerrainNextGeologyModel, ESPMode::ThreadSafe> GeologyModel;
	TArray<FOctave> MacroOctaves;
	TArray<FOctave> RoughOctaves;
	TArray<FPlanetTerrainNextCrater> Craters;
	double MinHeightM = 0.0;
	double MaxHeightM = 0.0;
};
