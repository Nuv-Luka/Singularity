#pragma once

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"
#include "PlanetTerrainNextQuadtree.h"

/**
 * Canonical tile identity. Must NOT include camera epoch, view direction, or frame index.
 * Matches the rebuild spec: {PlanetID, Seed, GeneratorVersion, ParameterHash, Face, Level, X, Y, Resolution, DataFormat}.
 */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextTileId
{
	int64 PlanetId = 0;
	int64 Seed = 0;
	int32 GeneratorVersion = 0;
	uint64 ParameterHash = 0;
	FPlanetTerrainNextNodeId Node;
	int32 Resolution = 17;
	/** 1 = RG float heights (fine, coarse) in metres. */
	int32 DataFormat = 1;

	FPlanetTerrainNextTileId() = default;

	static FPlanetTerrainNextTileId Make(
		int64 InPlanetId,
		int64 InSeed,
		int32 InGeneratorVersion,
		uint64 InParameterHash,
		const FPlanetTerrainNextNodeId& InNode,
		int32 InResolution = 17);

	bool operator==(const FPlanetTerrainNextTileId& Other) const
	{
		return PlanetId == Other.PlanetId
			&& Seed == Other.Seed
			&& GeneratorVersion == Other.GeneratorVersion
			&& ParameterHash == Other.ParameterHash
			&& Node == Other.Node
			&& Resolution == Other.Resolution
			&& DataFormat == Other.DataFormat;
	}

	bool operator!=(const FPlanetTerrainNextTileId& Other) const
	{
		return !(*this == Other);
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("P%lld S%lld V%d H%016llX %s R%d F%d"),
			PlanetId, Seed, GeneratorVersion, ParameterHash, *Node.ToString(), Resolution, DataFormat);
	}

	friend uint32 GetTypeHash(const FPlanetTerrainNextTileId& Id)
	{
		uint32 H = HashCombine(::GetTypeHash(Id.PlanetId), ::GetTypeHash(Id.Seed));
		H = HashCombine(H, ::GetTypeHash(Id.GeneratorVersion));
		H = HashCombine(H, ::GetTypeHash(Id.ParameterHash));
		H = HashCombine(H, GetTypeHash(Id.Node));
		H = HashCombine(H, ::GetTypeHash(Id.Resolution));
		return HashCombine(H, ::GetTypeHash(Id.DataFormat));
	}
};

enum class EPlanetTerrainNextPinReason : uint8
{
	None = 0,
	Draw = 1 << 0,
	Coverage = 1 << 1,
	Collision = 1 << 2,
};
ENUM_CLASS_FLAGS(EPlanetTerrainNextPinReason)
