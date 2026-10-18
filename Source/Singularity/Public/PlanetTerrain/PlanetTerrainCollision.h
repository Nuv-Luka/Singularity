#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainTile.h"

/** Bilinear sample of fine/coarse height grids with geomorph (matches drawn surface). */
bool PlanetTerrainSampleTileHeight(
	const FPlanetTerrainTileData& Tile,
	const FVector3d& UnitDir,
	float Morph,
	double& OutHeightM);

/** Approximate surface normal from tile heights. */
bool PlanetTerrainSampleTileNormal(
	const FPlanetTerrainTileData& Tile,
	const FVector3d& UnitDir,
	float Morph,
	FVector3d& OutNormal);

/**
 * Replace child's HeightCoarseM with bilinear samples of the parent's *displayed*
 * surface (fine/coarse + ParentMorph). Makes the child's Morph=1 pose match the
 * parent mesh instead of only a band-limited continuous field.
 */
void PlanetTerrainResampleCoarseFromParent(
	FPlanetTerrainTileData& Child,
	const FPlanetTerrainTileData& Parent,
	float ParentMorph);
