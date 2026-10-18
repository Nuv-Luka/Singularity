#pragma once

#include "CoreMinimal.h"
#include "Pug/PugCore.h"

/** Simulation position relative to a planet body (metres, double). */
struct FPlanetRelativePosition
{
	uint64 PlanetID = 0;
	FVector3d LocalMeters = FVector3d::ZeroVector;
};

/** Deterministic rocky-body terrain descriptor (v0.1 moon baseline). */
struct FRockyPlanetDescriptor
{
	uint64 PlanetID = 0;
	uint64 TerrainSeed = 0;

	double RadiusMeters = 1.737e6; // lunar-scale default
	double MassKg = 7.342e22;

	double SilicateFraction = 0.92;
	double MetalFraction = 0.06;
	double IceFraction = 0.02;

	double SurfaceAgeGyr = 4.2;

	float CraterDensity = 0.72f;
	float ReliefAmplitude = 1.0f;
	float Roughness = 0.55f;

	uint32 GeneratorVersion = 1;
};

/** Stable tile identity. GenerationEpoch invalidates jobs only — never changes physical seed. */
struct FPlanetTerrainTileKey
{
	uint64 PlanetID = 0;
	uint32 GenerationEpoch = 0;
	uint8 Face = 0;
	uint8 Level = 0;
	uint32 X = 0;
	uint32 Y = 0;

	bool operator==(const FPlanetTerrainTileKey& Other) const
	{
		return PlanetID == Other.PlanetID
			&& GenerationEpoch == Other.GenerationEpoch
			&& Face == Other.Face
			&& Level == Other.Level
			&& X == Other.X
			&& Y == Other.Y;
	}

	friend uint32 GetTypeHash(const FPlanetTerrainTileKey& Key)
	{
		uint64 H = Key.PlanetID;
		H ^= (static_cast<uint64>(Key.GenerationEpoch) << 1);
		H ^= (static_cast<uint64>(Key.Face) << 32);
		H ^= (static_cast<uint64>(Key.Level) << 40);
		H ^= (static_cast<uint64>(Key.X) << 8);
		H ^= static_cast<uint64>(Key.Y) << 24;
		return GetTypeHash(H);
	}
};

enum class EPlanetTerrainTileState : uint8
{
	Unrequested,
	Queued,
	Generating,
	ReadyForUpload,
	Resident,
	Evictable
};

enum class EPlanetTerrainJobPriority : uint8
{
	/** Roots / draw leaves without mesh — never cancel; prevents holes. */
	MandatoryCoverage = 0,
	Underfoot = 1,
	Collision = 2,
	PredictedCorridor = 3,
	ScreenError = 4,
	Speculative = 5
};

/** Why a node was not refined / not drawn (debug). */
enum class EPlanetTerrainRejectReason : uint8
{
	None = 0,
	Frustum,
	Horizon,
	Budget,
	Scheduler,
	NotRequested
};

/** Why a GPU mesh / CPU tile is retained (residency debug). */
enum class EPlanetTerrainPinReason : uint8
{
	None = 0,
	VisibleDraw,
	CollisionCritical,
	MandatoryRoot,
	DescentCorridor,
	LandingSite,
	RecentGrace,
	CpuCacheOnly
};

/** Build Ancient Airless Rocky Moon preset from a moon (or planet) record. */
inline FRockyPlanetDescriptor MakeAncientAirlessRockyMoon(
	uint64 BodyId,
	double RadiusEarth,
	double MassEarth)
{
	FRockyPlanetDescriptor D;
	D.PlanetID = BodyId;
	D.TerrainSeed = PugDeriveObjectSeed(BodyId, 1ull, "terrain");
	D.RadiusMeters = FMath::Max(1.0e5, RadiusEarth * PugEarthRadiusM);
	D.MassKg = FMath::Max(1.0e18, MassEarth * 5.972e24);
	D.SilicateFraction = 0.90;
	D.MetalFraction = 0.08;
	D.IceFraction = 0.02;
	D.SurfaceAgeGyr = 4.1;
	// Composition → plausible knobs (approximate, not geological simulation).
	const float Silicate = static_cast<float>(D.SilicateFraction);
	const float Age = static_cast<float>(FMath::Clamp(D.SurfaceAgeGyr / 4.5, 0.2, 1.2));
	D.CraterDensity = FMath::Clamp(0.45f + 0.40f * Age * Silicate, 0.2f, 0.95f);
	D.ReliefAmplitude = FMath::Clamp(0.7f + 0.35f * Silicate - 0.2f * static_cast<float>(D.IceFraction), 0.4f, 1.4f);
	D.Roughness = FMath::Clamp(0.35f + 0.4f * Silicate, 0.2f, 0.9f);
	D.GeneratorVersion = 1;
	return D;
}

/** Cube UV ∈ [0,1] → unit direction (DSE FacePoint equivalent). */
inline FVector3d PlanetTerrainFacePoint(uint8 Face, double U, double V)
{
	const double A = U * 2.0 - 1.0;
	const double B = V * 2.0 - 1.0;
	FVector3d P;
	switch (Face)
	{
	case 0: P = FVector3d(1.0, B, -A); break;
	case 1: P = FVector3d(-1.0, B, A); break;
	case 2: P = FVector3d(A, 1.0, -B); break;
	case 3: P = FVector3d(A, -1.0, B); break;
	case 4: P = FVector3d(A, B, 1.0); break;
	default: P = FVector3d(-A, B, -1.0); break;
	}
	return P.GetSafeNormal();
}
