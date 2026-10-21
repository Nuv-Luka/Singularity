#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextTypes.h"

/** Stable schema version for the immutable geology catalog and its cache identity. */
constexpr uint32 PlanetTerrainNextGeologySchemaVersion = 1;

enum class EPlanetTerrainNextGeologyEventType : uint8
{
	AncientBasin = 0,
	BasaltEpisode,
	Impact,
	RaySource,
};

/** Generation presets are continuous composition controls, not per-pixel biome enums. */
enum class EPlanetTerrainNextGeologyPreset : uint8
{
	LunarMixedSurface = 0,
	LunarYoungMare,
	LunarAncientHighland,
};

struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextGeologySettings
{
	bool bEnableGeologicalBiomes = false;
	EPlanetTerrainNextGeologyPreset Preset = EPlanetTerrainNextGeologyPreset::LunarMixedSurface;
	uint32 Version = PlanetTerrainNextGeologySchemaVersion;
	int32 BasinCount = 4;
	int32 ImpactCount = 56;
	double OldestAgeMyrBP = 4200.0;
	double YoungestAgeMyrBP = 5.0;

	static FPlanetTerrainNextGeologySettings FromDescriptor(const FPlanetTerrainNextDescriptor& Descriptor);
	uint64 GetStableHash() const;
};

/** One deterministic geological event. The catalog is generated once and never mutated. */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextGeologyEvent
{
	uint32 Id = 0;
	EPlanetTerrainNextGeologyEventType Type = EPlanetTerrainNextGeologyEventType::Impact;
	FVector3d CenterUnit = FVector3d::UnitZ();
	double RadiusMeters = 0.0;
	double AgeMyrBP = 0.0;
	double ReliefMeters = 0.0;
	double RimMeters = 0.0;
	double InfluenceRadiusMeters = 0.0;
	/** Approximate post-flood fill elevation for a basin-local lava episode. */
	double FillHeightMeters = 0.0;
	/** Width of the anti-aliased shoreline transition in meters. */
	double ShorelineWidthMeters = 0.0;
	/** Opt-in impact morphology parameters, evaluated oldest to youngest. */
	double BowlDepthMeters = 0.0;
	double RimHeightMeters = 0.0;
	double CentralPeakMeters = 0.0;
	float CompositionStrength = 0.0f;
	uint64 LocalSeed = 0;
};

/** Camera-independent material/age sample reserved for geology and diagnostics. */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextSurfaceSample
{
	double HeightOffsetM = 0.0;
	double HighlandWeight = 1.0;
	double MareWeight = 0.0;
	double EjectaWeight = 0.0;
	double ImpactMeltWeight = 0.0;
	double Freshness = 0.0;
	double ExposureAgeMyr = 4200.0;
	double RayIntensity = 0.0;
	int32 MaterialId = 0;

	void NormalizeWeights();
};

namespace PlanetTerrainNextGeology
{
/** Stable great-circle distance in meters. */
SINGULARITYPLANETTERRAINNEXT_API double GreatCircleDistanceMeters(
	const FVector3d& AUnit,
	const FVector3d& BUnit,
	double RadiusMeters);

/** Builds a stable east/north frame at a unit direction, including pole fallback. */
SINGULARITYPLANETTERRAINNEXT_API void BuildTangentFrame(
	const FVector3d& CenterUnit,
	FVector3d& OutEast,
	FVector3d& OutNorth);

/** Shortest signed angular difference in radians. */
SINGULARITYPLANETTERRAINNEXT_API double WrappedAngleRadians(double A, double B);
}

/** Immutable, seeded event catalog and bounded spatial index for future geology stages. */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextGeologyModel
{
public:
	explicit FPlanetTerrainNextGeologyModel(const FPlanetTerrainNextDescriptor& Descriptor);

	uint64 GetIdentityHash() const { return IdentityHash; }
	const FPlanetTerrainNextGeologySettings& GetSettings() const { return Settings; }
	const TArray<FPlanetTerrainNextGeologyEvent>& GetEvents() const { return Events; }

	/** Deterministic sample of continuous composition and surface-age fields. */
	FPlanetTerrainNextSurfaceSample SampleSurface(
		const FVector3d& UnitDirection,
		double SampleFootprintMeters) const;

	/** Number of indexed candidates considered for a direction; useful for diagnostics. */
	int32 CountCandidates(const FVector3d& UnitDirection) const;

private:
	static constexpr int32 SpatialBinsPerFace = 16;

	void GenerateEvents(const FPlanetTerrainNextDescriptor& Descriptor);
	void AddEventToSpatialIndex(int32 EventIndex);
	void GatherCandidateIndices(const FVector3d& UnitDirection, TArray<int32, TInlineAllocator<64>>& OutIndices) const;

	FPlanetTerrainNextGeologySettings Settings;
	uint64 IdentityHash = 0;
	double RadiusMeters = 0.0;
	TArray<FPlanetTerrainNextGeologyEvent> Events;
	TArray<TArray<int32>> SpatialBins;
};
