#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextQuadtree.h"
#include "PlanetTerrainNextTileId.h"

class FPlanetTerrainNextHeightField;

struct FPlanetTerrainNextCollisionSettings
{
	/** Geodesic metres of active collision around the focus (and predicted path). */
	double RadiusM = 200.0;
	/** Target vertex spacing for collision patches (metres). ~2–5 m for landing. */
	double TargetSpacingM = 4.0;
	int32 MaxLevel = 17;
	int32 MaxPatches = 24;
	int32 CooksPerFrame = 2;
	/** Keep retired patches this many frames so the player never falls through mid-swap. */
	int32 RetireGraceFrames = 8;
	int32 PathSamples = 4;
};

struct FPlanetTerrainNextCollisionStats
{
	int32 Desired = 0;
	int32 Resident = 0;
	int32 CookedThisFrame = 0;
	int32 RetiredThisFrame = 0;
	int32 Level = 0;
	double RadiusM = 0.0;
	double SpacingM = 0.0;
	double LastCookMs = 0.0;
	double P95CookMs = 0.0;
};

/**
 * Local collision coverage independent of the visual draw set.
 * Selects a geodesic footprint of quadtree nodes at a spacing-driven level,
 * cooks CPU meshes (game thread) from height tiles, and answers height queries
 * from the canonical field / resident collision tiles (no GPU readback).
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextCollisionManager
{
public:
	void Reset();
	void SetSettings(const FPlanetTerrainNextCollisionSettings& InSettings) { Settings = InSettings; }
	const FPlanetTerrainNextCollisionSettings& GetSettings() const { return Settings; }

	/** Level whose vertex spacing is nearest TargetSpacingM. */
	static int32 LevelForSpacing(double RadiusM, double TargetSpacingM, int32 MaxLevel);

	/**
	 * Rebuild the desired footprint around FocusM (planet-local) and optional
	 * predicted path points (also planet-local metres). Does not cook.
	 */
	void UpdateDesired(
		const FVector3d& FocusM,
		const TArray<FVector3d>& PredictedPathM,
		double PlanetRadiusM);

	const TArray<FPlanetTerrainNextNodeId>& GetDesired() const { return Desired; }
	const TArray<FPlanetTerrainNextNodeId>& GetResident() const { return ResidentOrder; }
	bool IsResident(const FPlanetTerrainNextNodeId& Id) const { return Entries.Contains(Id); }

	/** Mark a node as cooked/resident with its height tile snapshot for queries. */
	void NotifyCooked(const FPlanetTerrainNextNodeId& Id, const FPlanetTerrainNextHeightTile& Tile, int64 Frame);

	/** Drop retired residents past grace once replacements cover the footprint. */
	void TickResidency(int64 Frame, TArray<FPlanetTerrainNextNodeId>& OutReleased);

	/**
	 * Surface height (m above base radius) under Direction.
	 * Prefers bilinear sample of a resident collision tile (fine surface);
	 * falls back to HeightField at the collision band-limit.
	 */
	bool TrySampleHeightM(
		const FPlanetTerrainNextHeightField& HeightField,
		const FVector3d& UnitDirection,
		double& OutHeightM) const;

	/** Altitude of PointM above the collision/fine surface (metres). */
	double SampleAltitudeM(
		const FPlanetTerrainNextHeightField& HeightField,
		const FVector3d& PointM) const;

	const FPlanetTerrainNextCollisionStats& GetStats() const { return Stats; }
	void SetCookedThisFrame(int32 Count) { Stats.CookedThisFrame = Count; }
	void RecordCookTime(double Ms);

private:
	struct FEntry
	{
		FPlanetTerrainNextHeightTile Tile;
		int64 CookedFrame = 0;
		int64 RetireFrame = INDEX_NONE;
		bool bDesired = false;
	};

	void CollectFootprint(
		const FVector3d& FocusDir,
		double AngularRadiusRad,
		int32 Level,
		const TArray<FVector3d>& ExtraDirs,
		TArray<FPlanetTerrainNextNodeId>& Out) const;

	FPlanetTerrainNextCollisionSettings Settings;
	TArray<FPlanetTerrainNextNodeId> Desired;
	TArray<FPlanetTerrainNextNodeId> ResidentOrder;
	TMap<FPlanetTerrainNextNodeId, FEntry> Entries;
	FPlanetTerrainNextCollisionStats Stats;
	TArray<double> CookTimesMs;
	int32 CookTimeCursor = 0;
};
