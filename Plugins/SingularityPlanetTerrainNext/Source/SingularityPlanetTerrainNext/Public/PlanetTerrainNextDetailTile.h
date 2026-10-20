#pragma once

#include "CoreMinimal.h"

class FPlanetTerrainNextHeightField;

/** Visual-detail tile identity (independent of geometry LOD residency). */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextDetailTileId
{
	int64 PlanetId = 0;
	uint64 GeneratorHash = 0;
	uint8 Face = 0;
	uint8 Level = 0;
	uint32 X = 0;
	uint32 Y = 0;
	uint16 Resolution = 512;
	uint16 FormatVersion = 1;

	static FPlanetTerrainNextDetailTileId MakeRoot(
		int64 PlanetId,
		uint64 GeneratorHash,
		uint8 Face,
		uint16 Resolution = 512);

	bool operator==(const FPlanetTerrainNextDetailTileId& Other) const
	{
		return PlanetId == Other.PlanetId
			&& GeneratorHash == Other.GeneratorHash
			&& Face == Other.Face
			&& Level == Other.Level
			&& X == Other.X
			&& Y == Other.Y
			&& Resolution == Other.Resolution
			&& FormatVersion == Other.FormatVersion;
	}

	friend uint32 GetTypeHash(const FPlanetTerrainNextDetailTileId& Id)
	{
		uint32 H = HashCombine(::GetTypeHash(Id.PlanetId), ::GetTypeHash(Id.GeneratorHash));
		H = HashCombine(H, ::GetTypeHash(static_cast<uint32>(Id.Face) | (static_cast<uint32>(Id.Level) << 8)));
		H = HashCombine(H, ::GetTypeHash(Id.X));
		H = HashCombine(H, ::GetTypeHash(Id.Y));
		return HashCombine(H, ::GetTypeHash(static_cast<uint32>(Id.Resolution) | (static_cast<uint32>(Id.FormatVersion) << 16)));
	}
};

struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextDetailTile
{
	FPlanetTerrainNextDetailTileId Id;
	/** RGBA8 albedo, Resolution². */
	TArray<FColor> Albedo;
	/** RGBA8 tangent-space normal (RGB = n*0.5+0.5), Resolution². */
	TArray<FColor> Normal;
	bool bValid = false;
};

namespace PlanetTerrainNextDetailTile
{
/**
 * Bake one tile from the same HeightField as geometry.
 * MinWavelength is set from texel footprint (not mesh spacing).
 * Safe on worker threads (HeightField is pure).
 */
SINGULARITYPLANETTERRAINNEXT_API void Bake(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextDetailTileId& Id,
	FPlanetTerrainNextDetailTile& Out);
}
