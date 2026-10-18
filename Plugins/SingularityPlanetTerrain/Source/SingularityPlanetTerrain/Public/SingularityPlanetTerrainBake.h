#pragma once

#include "CoreMinimal.h"

/** Uniforms mirrored by PlanetTerrainHeightBakeCS / EvaluateHeightGpuFloat. */
struct FSingularityPlanetTerrainBakeUniforms
{
	float AmplitudeM = 1.0f;
	float RadiusM = 1.0e6f;
	float ContinentFreq = 1.4f;
	float MountainFreq = 4.0f;
	float DetailFreq = 36.0f;
	float CraterFreq0 = 18.0f;
	float CraterDensity = 0.7f;
	float CraterWeight = 0.55f;
	float MountainWeight = 0.35f;
	uint32 NoiseSeed = 0;
};

/**
 * Synchronous GPU height bake (game thread). Writes fine/coarse metres into OutFine/OutCoarse
 * with (Resolution+1)^2 samples. Returns false if RHI/NullRHI/shader unavailable.
 */
SINGULARITYPLANETTERRAIN_API bool SingularityPlanetTerrainBakeHeights(
	const FSingularityPlanetTerrainBakeUniforms& Uniforms,
	uint8 Face,
	float U0, float V0, float U1, float V1,
	int32 Resolution,
	float FineSpacingM,
	float CoarseSpacingM,
	TArray<float>& OutFine,
	TArray<float>& OutCoarse);

SINGULARITYPLANETTERRAIN_API bool SingularityPlanetTerrainIsGpuBakeAvailable();
