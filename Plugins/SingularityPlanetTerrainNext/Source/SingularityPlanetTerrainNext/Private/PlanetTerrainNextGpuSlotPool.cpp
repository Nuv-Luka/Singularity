#include "PlanetTerrainNextGpuSlotPool.h"

#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextLog.h"
#include "Engine/Texture2D.h"

FPlanetTerrainNextGpuSlotPool::~FPlanetTerrainNextGpuSlotPool()
{
	Shutdown();
}

void FPlanetTerrainNextGpuSlotPool::Initialize(UObject* Outer, int32 InCapacity, int32 InTileResolution)
{
	(void)Outer;
	Shutdown();
	Capacity = FMath::Max(InCapacity, 1);
	TileResolution = FMath::Max(InTileResolution, 2);
	PerRow = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Capacity)));

	// R4: logical residency only. Creating a PF_A32B32G32R32F transient + UpdateTextureRegions
	// fatals D3D12 (E_INVALIDARG) — UE keeps a raw pointer to the region and the float
	// CreateTransient path is unsafe on this RHI. Mesh morph uses CPU/UV packing; real atlas
	// upload lands with RDG vertex sampling (R5+).
	Atlas = nullptr;

	Slots.SetNum(Capacity);
	FreeList.Reset();
	for (int32 Index = Capacity - 1; Index >= 0; --Index)
	{
		FreeList.Add(Index);
	}
	KeyToSlot.Reset();
	Stats = FPlanetTerrainNextGpuPoolStats();
	Stats.Capacity = Capacity;
	UE_LOG(LogPlanetTerrainNext, Log,
		TEXT("GpuSlotPool: logical pool %d slots × %d texels (GPU atlas deferred — CPU morph path)"),
		Capacity, TileResolution);
}

void FPlanetTerrainNextGpuSlotPool::Shutdown()
{
	KeyToSlot.Reset();
	FreeList.Reset();
	Slots.Reset();
	Atlas = nullptr;
	Capacity = 0;
}

void FPlanetTerrainNextGpuSlotPool::BeginFrame(int64 Frame)
{
	CurrentFrame = Frame;
	Stats.UploadsThisFrame = 0;
	Stats.Allocations = 0;
	Stats.Evictions = 0;
	Stats.AllocFailed = 0;
	Stats.Hits = 0;
	Stats.Resident = KeyToSlot.Num();
}

void FPlanetTerrainNextGpuSlotPool::ClearPins()
{
	for (FSlot& Slot : Slots)
	{
		Slot.PinMask = 0;
	}
}

void FPlanetTerrainNextGpuSlotPool::Pin(const FPlanetTerrainNextTileId& Id, EPlanetTerrainNextPinReason Reason)
{
	if (const int32* SlotIndex = KeyToSlot.Find(Id))
	{
		Slots[*SlotIndex].PinMask |= static_cast<uint8>(Reason);
	}
}

void FPlanetTerrainNextGpuSlotPool::Unpin(const FPlanetTerrainNextTileId& Id, EPlanetTerrainNextPinReason Reason)
{
	if (const int32* SlotIndex = KeyToSlot.Find(Id))
	{
		Slots[*SlotIndex].PinMask &= ~static_cast<uint8>(Reason);
	}
}

bool FPlanetTerrainNextGpuSlotPool::IsPinned(const FPlanetTerrainNextTileId& Id) const
{
	if (const int32* SlotIndex = KeyToSlot.Find(Id))
	{
		return Slots[*SlotIndex].PinMask != 0;
	}
	return false;
}

int32 FPlanetTerrainNextGpuSlotPool::TryGet(const FPlanetTerrainNextTileId& Id)
{
	if (const int32* SlotIndex = KeyToSlot.Find(Id))
	{
		Slots[*SlotIndex].LastUsedFrame = CurrentFrame;
		++Stats.Hits;
		return *SlotIndex;
	}
	return INDEX_NONE;
}

void FPlanetTerrainNextGpuSlotPool::FreeSlot(int32 SlotIndex)
{
	FSlot& Slot = Slots[SlotIndex];
	if (!Slot.bOccupied)
	{
		return;
	}
	KeyToSlot.Remove(Slot.Key);
	Slot = FSlot();
	FreeList.Add(SlotIndex);
	Stats.Resident = KeyToSlot.Num();
}

int32 FPlanetTerrainNextGpuSlotPool::EvictOldestUnpinned()
{
	int32 Best = INDEX_NONE;
	int64 BestFrame = TNumericLimits<int64>::Max();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FSlot& Slot = Slots[Index];
		if (!Slot.bOccupied || Slot.PinMask != 0)
		{
			continue;
		}
		if (Slot.LastUsedFrame < BestFrame)
		{
			BestFrame = Slot.LastUsedFrame;
			Best = Index;
		}
	}
	if (Best != INDEX_NONE)
	{
		FreeSlot(Best);
		++Stats.Evictions;
	}
	return Best;
}

int32 FPlanetTerrainNextGpuSlotPool::Allocate(const FPlanetTerrainNextTileId& Id)
{
	if (const int32* Existing = KeyToSlot.Find(Id))
	{
		Slots[*Existing].LastUsedFrame = CurrentFrame;
		++Stats.Hits;
		return *Existing;
	}
	if (FreeList.Num() == 0)
	{
		EvictOldestUnpinned();
	}
	if (FreeList.Num() == 0)
	{
		++Stats.AllocFailed;
		return INDEX_NONE;
	}

	const int32 SlotIndex = FreeList.Pop(EAllowShrinking::No);
	FSlot& Slot = Slots[SlotIndex];
	Slot.Key = Id;
	Slot.bOccupied = true;
	Slot.bUploaded = false;
	Slot.PinMask = 0;
	Slot.LastUsedFrame = CurrentFrame;
	KeyToSlot.Add(Id, SlotIndex);
	++Stats.Allocations;
	Stats.Resident = KeyToSlot.Num();
	return SlotIndex;
}

void FPlanetTerrainNextGpuSlotPool::Release(const FPlanetTerrainNextTileId& Id)
{
	if (const int32* SlotIndex = KeyToSlot.Find(Id))
	{
		FreeSlot(*SlotIndex);
	}
}

FIntPoint FPlanetTerrainNextGpuSlotPool::TileOrigin(int32 Slot) const
{
	return FIntPoint((Slot % PerRow) * TileResolution, (Slot / PerRow) * TileResolution);
}

bool FPlanetTerrainNextGpuSlotPool::Upload(int32 SlotIndex, const FPlanetTerrainNextHeightTile& Tile)
{
	if (!Slots.IsValidIndex(SlotIndex) || !Slots[SlotIndex].bOccupied)
	{
		return false;
	}
	if (Tile.Resolution() != TileResolution || !Tile.IsValid())
	{
		UE_LOG(LogPlanetTerrainNext, Warning, TEXT("GpuSlotPool: tile resolution mismatch (%d vs %d)"), Tile.Resolution(), TileResolution);
		return false;
	}

	// Logical upload only — no RHI touch (see Initialize).
	Slots[SlotIndex].bUploaded = true;
	Slots[SlotIndex].LastUsedFrame = CurrentFrame;
	++Stats.UploadsThisFrame;
	return true;
}
