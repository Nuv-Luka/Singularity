#include "PlanetTerrainNextTileId.h"

FPlanetTerrainNextTileId FPlanetTerrainNextTileId::Make(
	int64 InPlanetId,
	int64 InSeed,
	int32 InGeneratorVersion,
	uint64 InParameterHash,
	const FPlanetTerrainNextNodeId& InNode,
	int32 InResolution)
{
	FPlanetTerrainNextTileId Id;
	Id.PlanetId = InPlanetId;
	Id.Seed = InSeed;
	Id.GeneratorVersion = InGeneratorVersion;
	Id.ParameterHash = InParameterHash;
	Id.Node = InNode;
	Id.Resolution = InResolution;
	Id.DataFormat = 1;
	return Id;
}
