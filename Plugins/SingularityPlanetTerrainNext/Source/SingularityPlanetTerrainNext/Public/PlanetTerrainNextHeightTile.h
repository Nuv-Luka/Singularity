#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextTileId.h"

class FPlanetTerrainNextHeightField;

/**
 * CPU height tile: one texel per patch grid vertex.
 * R = fine height (m), G = parent-compatible coarse height (m).
 * Built once per residency; never depends on the camera.
 */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextHeightTile
{
	FPlanetTerrainNextTileId Id;
	TArray<float> FineHeightM;
	TArray<float> CoarseHeightM;
	double MinHeightM = 0.0;
	double MaxHeightM = 0.0;
	double RadiusM = 0.0;

	int32 Resolution() const { return Id.Resolution; }
	int32 NumTexels() const { return FineHeightM.Num(); }
	bool IsValid() const { return FineHeightM.Num() > 0 && FineHeightM.Num() == CoarseHeightM.Num(); }
};

namespace PlanetTerrainNextHeightTile
{
/** GridN+1 — one sample per patch vertex, matching PlanetTerrainNextPatch::Row. */
inline constexpr int32 DefaultResolution = 17;

SINGULARITYPLANETTERRAINNEXT_API void Bake(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextTileId& Id,
	FPlanetTerrainNextHeightTile& Out);

/** Pack RG float texels for atlas upload (R=fine, G=coarse, B=0, A=1). */
SINGULARITYPLANETTERRAINNEXT_API void PackRgbaFloat32(const FPlanetTerrainNextHeightTile& Tile, TArray<FLinearColor>& Out);
}
