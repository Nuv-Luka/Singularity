#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"
#include "PlanetTerrain/PlanetTerrainHeight.h"

/** CPU-side generated patch data (no UObjects). Grid is (Resolution+1)^2. */
struct FPlanetTerrainTileData
{
	FPlanetTerrainTileKey Key;
	uint8 Face = 0;
	uint8 Level = 0;
	double U0 = 0.0, V0 = 0.0, U1 = 1.0, V1 = 1.0;
	int32 Resolution = 32; // cells per edge → Resolution+1 verts

	TArray<FVector3d> Directions;
	TArray<float> HeightFineM;
	TArray<float> HeightCoarseM;
	TArray<FVector> PositionsLocal; // float relative to TileOrigin
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	FVector3d TileOrigin = FVector3d::ZeroVector; // planet-local metres
	double SampleSpacingM = 1.0;
	double WorldSizeM = 1.0;
	FVector3d CenterLocal = FVector3d::ZeroVector;
	FVector3d CenterSurfaceLocal = FVector3d::ZeroVector;

	EPlanetTerrainTileState State = EPlanetTerrainTileState::Unrequested;
	double LastUseSeconds = 0.0;
	bool bCollisionResident = false;
	EPlanetTerrainPinReason PinReason = EPlanetTerrainPinReason::None;
	/** Last frame this tile was in the draw / corridor set (GPU grace). */
	int32 LastPinnedFrame = -1;
};

struct FPlanetTerrainTileBaker
{
	static constexpr int32 DefaultResolution = 32; // 33×33 verts

	static void Bake(
		const FPlanetTerrainHeight& Height,
		const FPlanetTerrainTileKey& Key,
		double U0, double V0, double U1, double V1,
		int32 Resolution,
		FPlanetTerrainTileData& Out,
		TFunction<bool()> ShouldCancel = nullptr);
};
