#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextTileId.h"

class UTexture2D;
struct FPlanetTerrainNextHeightTile;

struct FPlanetTerrainNextGpuPoolStats
{
	int32 Capacity = 0;
	int32 Resident = 0;
	int32 Allocations = 0;
	int32 Evictions = 0;
	int32 UploadsThisFrame = 0;
	int32 AllocFailed = 0;
	int32 Hits = 0;
};

/**
 * Fixed-capacity residency pool for height tiles.
 *
 * R4: logical slots + pin/evict only. Real D3D12 atlas upload is deferred (CreateTransient
 * float + UpdateTextureRegions fatals on this RHI). Mesh morph uses CPU/UV packing.
 *
 * Never overwrites a slot pinned this frame (Draw / Coverage / Collision).
 * If every slot is pinned, Allocate returns INDEX_NONE and the caller keeps the parent.
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextGpuSlotPool
{
public:
	FPlanetTerrainNextGpuSlotPool() = default;
	~FPlanetTerrainNextGpuSlotPool();

	void Initialize(UObject* Outer, int32 Capacity, int32 TileResolution);
	void Shutdown();
	bool IsInitialized() const { return Capacity > 0; }

	void BeginFrame(int64 Frame);
	void ClearPins();
	void Pin(const FPlanetTerrainNextTileId& Id, EPlanetTerrainNextPinReason Reason);
	void Unpin(const FPlanetTerrainNextTileId& Id, EPlanetTerrainNextPinReason Reason);
	bool IsPinned(const FPlanetTerrainNextTileId& Id) const;

	/** Touch LRU; returns slot or INDEX_NONE. */
	int32 TryGet(const FPlanetTerrainNextTileId& Id);
	/**
	 * Reserve a slot for Id, evicting an unpinned LRU resident if needed.
	 * Returns INDEX_NONE when the pool is full of pinned slots.
	 */
	int32 Allocate(const FPlanetTerrainNextTileId& Id);
	void Release(const FPlanetTerrainNextTileId& Id);

	/** Upload tile texels into an allocated slot. Counts against UploadsThisFrame. */
	bool Upload(int32 Slot, const FPlanetTerrainNextHeightTile& Tile);

	FIntPoint TileOrigin(int32 Slot) const;
	UTexture2D* GetAtlas() const { return Atlas; }
	int32 GetCapacity() const { return Capacity; }
	int32 GetTileResolution() const { return TileResolution; }
	int32 GetPerRow() const { return PerRow; }
	const FPlanetTerrainNextGpuPoolStats& GetStats() const { return Stats; }

private:
	struct FSlot
	{
		FPlanetTerrainNextTileId Key;
		uint8 PinMask = 0;
		int64 LastUsedFrame = -1;
		bool bOccupied = false;
		bool bUploaded = false;
	};

	int32 EvictOldestUnpinned();
	void FreeSlot(int32 Slot);

	UTexture2D* Atlas = nullptr;
	TArray<FSlot> Slots;
	TArray<int32> FreeList;
	TMap<FPlanetTerrainNextTileId, int32> KeyToSlot;
	int32 Capacity = 0;
	int32 TileResolution = 17;
	int32 PerRow = 1;
	int64 CurrentFrame = 0;
	FPlanetTerrainNextGpuPoolStats Stats;
};
