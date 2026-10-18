#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextTypes.generated.h"

/**
 * Immutable physical description of one airless rocky moon.
 * R1 uses the radius only. Mass, seed, and height bound are stored so later
 * milestones do not invent a second descriptor.
 *
 * Units: meters and kilograms. Render conversion is 1 uu = 1 cm, actor scale = 1.
 */
USTRUCT(BlueprintType)
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextDescriptor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int64 PlanetId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 GeneratorVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int64 Seed = 1;

	/** Mean radius in meters. 1,737 km lunar test radius → 173,700,000 uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	double RadiusMeters = 1737000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	double MassKg = 7.342e22;

	/**
	 * Proven maximum |relief| above or below RadiusMeters.
	 * R1 is a perfect sphere, so this stays 0. Later tiles add it to bounds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	double HeightBoundMeters = 0.0;

	/** Cells along one edge of each cube face. R1 static shell only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1", ClampMax = "64"))
	int32 CellsPerEdge = 16;
};
