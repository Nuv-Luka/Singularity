#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextTypes.h"

class APlanetTerrainNextMoon;
class UWorld;

enum class EPlanetTerrainNextSpawnPlacement : uint8
{
	InFrontOfCamera = 0,
	OneAuAlongX = 1,
	WorldOrigin = 2,
	Custom = 3
};

struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextSpawnRequest
{
	FPlanetTerrainNextDescriptor Descriptor;
	EPlanetTerrainNextSpawnPlacement Placement = EPlanetTerrainNextSpawnPlacement::InFrontOfCamera;
	FVector CustomCenterUu = FVector::ZeroVector;
	/** Camera-to-center distance as a multiple of radius. Used by InFrontOfCamera. */
	double DistanceRadii = 3.0;
	/** Switch singularity.TerrainRenderer.Mode to Next so the quadtree terrain draws. */
	bool bSelectNextRenderer = true;
	/** Keep the R1 static shell instead of starting the R2 quadtree terrain. */
	bool bStaticShellOnly = false;
};

struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextSpawn
{
	static APlanetTerrainNextMoon* Spawn(UWorld* World, const FPlanetTerrainNextSpawnRequest& Request);
	static int32 DestroyAll(UWorld* World);
	static APlanetTerrainNextMoon* FindNearest(UWorld* World, const FVector& FromUu);
	static FVector ResolveCenter(UWorld* World, const FPlanetTerrainNextSpawnRequest& Request);

	/** Altitude (m) of WorldPointUu above the nearest Next moon's surface. False when there is none. */
	static bool GetNearestSurfaceAltitude(UWorld* World, const FVector& WorldPointUu, double& OutAltitudeM);

	/**
	 * Teleports the player above the nearest moon at the given altitude, keeping the current
	 * sub-camera point, looking along the surface pitched 20 degrees down. Requests a fly speed
	 * proportional to altitude.
	 */
	static bool PlaceCameraAtAltitude(UWorld* World, double AltitudeM);

	/** One-slot mailbox read by the fly pawn each tick. Speeds are in uu/s. */
	static void RequestFlySpeed(double SpeedUuPerSec);
	static bool ConsumeRequestedFlySpeed(double& OutSpeedUuPerSec);
};
