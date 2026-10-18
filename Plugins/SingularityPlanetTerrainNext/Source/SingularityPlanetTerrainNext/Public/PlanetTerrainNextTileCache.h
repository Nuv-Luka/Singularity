#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextHeightTile.h"

class FPlanetTerrainNextHeightField;

struct FPlanetTerrainNextTileCacheStats
{
	int32 Entries = 0;
	int32 Hits = 0;
	int32 Misses = 0;
	int32 Evictions = 0;
	int32 BakesThisFrame = 0;
	double LastBakeMs = 0.0;
	double P95BakeMs = 0.0;
};

/**
 * Canonical CPU height-tile cache. Separate from GPU slot residency.
 * Identity excludes camera epoch. Eviction never drops tiles that are GPU-pinned
 * (caller passes a predicate); otherwise LRU by last touch frame.
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextTileCache
{
public:
	explicit FPlanetTerrainNextTileCache(int32 MaxEntries = 1024);

	void SetMaxEntries(int32 MaxEntries);
	int32 GetMaxEntries() const { return MaxEntries; }

	/** Returns a stable pointer until the next mutating call that may evict. */
	const FPlanetTerrainNextHeightTile* GetOrBake(
		const FPlanetTerrainNextHeightField& HeightField,
		const FPlanetTerrainNextTileId& Id,
		int64 Frame,
		TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned);

	/**
	 * Insert a worker-baked tile. Rejects empty tiles. Touches LRU.
	 * Returns false if the cache is full of pinned entries.
	 */
	bool Insert(
		FPlanetTerrainNextHeightTile&& Tile,
		int64 Frame,
		TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned);

	const FPlanetTerrainNextHeightTile* Find(const FPlanetTerrainNextTileId& Id) const;
	void Touch(const FPlanetTerrainNextTileId& Id, int64 Frame);
	void Remove(const FPlanetTerrainNextTileId& Id);
	void Clear();
	void BeginFrame();

	const FPlanetTerrainNextTileCacheStats& GetStats() const { return Stats; }
	int32 Num() const { return Entries.Num(); }

private:
	struct FEntry
	{
		FPlanetTerrainNextHeightTile Tile;
		int64 LastUsedFrame = 0;
	};

	bool EvictOne(int64 Frame, TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned);
	void RecordBakeTime(double Ms);

	int32 MaxEntries = 1024;
	TMap<FPlanetTerrainNextTileId, FEntry> Entries;
	FPlanetTerrainNextTileCacheStats Stats;
	TArray<double> BakeTimesMs;
	int32 BakeTimeCursor = 0;
};
