#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainHeight.h"
#include "PlanetTerrain/PlanetTerrainTile.h"

enum class EPlanetTerrainBakeBackend : uint8
{
	/** Double EvaluateHeight — collision / unrestricted truth. */
	DoubleCpu = 0,
	/** Float field mirrored by the compute shader (CPU evaluate). */
	GpuFloatCpu = 1,
	/** Dispatch PlanetTerrainHeightBakeCS + readback (game/render thread). */
	GpuCompute = 2
};

/**
 * Phase 7 GPU height bake helpers.
 * Prefer GpuFloatCpu on workers; GpuCompute is sync and game-thread oriented.
 */
struct FPlanetTerrainGpuBaker
{
	/** True when a real GPU RHI is present (not NullRHI). */
	static bool IsGpuComputeAvailable();

	/**
	 * Synchronous compute bake of fine/coarse heights (Resolution+1)^2.
	 * Returns false on failure — caller should fall back to GpuFloatCpu.
	 */
	static bool TryBakeHeightsCompute(
		const FPlanetTerrainHeight& Height,
		uint8 Face,
		double U0, double V0, double U1, double V1,
		int32 Resolution,
		float FineSpacingM,
		float CoarseSpacingM,
		TArray<float>& OutFine,
		TArray<float>& OutCoarse);
};

/** Extended tile bake with selectable backend. */
struct FPlanetTerrainTileBakerEx
{
	static void Bake(
		const FPlanetTerrainHeight& Height,
		const FPlanetTerrainTileKey& Key,
		double U0, double V0, double U1, double V1,
		int32 Resolution,
		FPlanetTerrainTileData& Out,
		EPlanetTerrainBakeBackend Backend,
		TFunction<bool()> ShouldCancel = nullptr);
};
