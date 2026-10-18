#pragma once

#include "CoreMinimal.h"

/**
 * Immutable render-thread snapshot of one planet-local atmosphere.
 * World positions on CPU are UE centimeters; GPU work is planet-relative kilometers.
 */
struct FSingularityAtmospherePlanetProxy
{
	uint64 BodyId = 0;
	FVector3d PlanetCenterWorldCm = FVector3d::ZeroVector;
	double VisualBottomRadiusKm = 6371.0;
	double VisualTopRadiusKm = 6471.0;
	double VisualKmToWorldCm = 100000.0;

	FVector3f RayleighScatteringPerKm = FVector3f::ZeroVector;
	float RayleighScaleHeightKm = 8.0f;
	FVector3f MieScatteringPerKm = FVector3f::ZeroVector;
	FVector3f MieExtinctionPerKm = FVector3f::ZeroVector;
	float MieScaleHeightKm = 1.2f;
	FVector3f AbsorptionPerKm = FVector3f::ZeroVector;
	float MieAnisotropyG = 0.8f;
	FVector3f GroundAlbedo = FVector3f(0.3f, 0.3f, 0.3f);
	float GroundBounce = 0.04f;
	float DensityScale = 1.0f;

	FVector3f DirectionToStar = FVector3f(0.0f, 0.0f, 1.0f);
	FVector3f StarIlluminanceLinear = FVector3f(1.0f, 1.0f, 1.0f);
	uint32 AtmosphereProfileHash = 0;
};

/** Camera basis for ray reconstruction (game thread → render thread). */
struct FSingularityAtmosphereCameraProxy
{
	FVector3d CameraWorldCm = FVector3d::ZeroVector;
	FVector3f Forward = FVector3f(1.0f, 0.0f, 0.0f);
	FVector3f Right = FVector3f(0.0f, 1.0f, 0.0f);
	FVector3f Up = FVector3f(0.0f, 0.0f, 1.0f);
	float TanHalfFovY = 0.5f;
	float Aspect = 1.777f;
};

/** Per-view immutable snapshot consumed on the render thread (no UObject*). */
struct FSingularityAtmosphereViewSnapshot
{
	TArray<FSingularityAtmospherePlanetProxy> Planets;
	FSingularityAtmosphereCameraProxy Camera;
	float Fade = 0.0f;
	bool bActive = false;
};

/** Stable hash of optical radii/coefficients for LUT cache keys (implemented in SingularityAtmosphereLuts.cpp). */
SINGULARITYATMOSPHERE_API uint32 ComputeSingularityAtmosphereProfileHash(const FSingularityAtmospherePlanetProxy& Planet);
