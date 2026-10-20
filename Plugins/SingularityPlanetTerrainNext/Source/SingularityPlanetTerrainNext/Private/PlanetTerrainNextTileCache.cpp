#include "PlanetTerrainNextTileCache.h"

#include "PlanetTerrainNextDiskTileStore.h"
#include "PlanetTerrainNextHeightField.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FPlanetTerrainNextTileCache::FPlanetTerrainNextTileCache(int32 InMaxEntries)
	: MaxEntries(FMath::Max(InMaxEntries, 1))
{
}

void FPlanetTerrainNextTileCache::SetMaxEntries(int32 InMaxEntries)
{
	MaxEntries = FMath::Max(InMaxEntries, 1);
	while (Entries.Num() > MaxEntries)
	{
		if (!EvictOne(TNumericLimits<int64>::Max(), [](const FPlanetTerrainNextTileId&) { return false; }))
		{
			break;
		}
	}
}

void FPlanetTerrainNextTileCache::BeginFrame()
{
	Stats.BakesThisFrame = 0;
	Stats.Hits = 0;
	Stats.Misses = 0;
	Stats.Evictions = 0;
	Stats.DiskHits = 0;
	Stats.DiskMisses = 0;
	Stats.DiskWrites = 0;
	if (Disk.IsValid())
	{
		Disk->BeginFrame();
	}
}

const FPlanetTerrainNextHeightTile* FPlanetTerrainNextTileCache::Find(const FPlanetTerrainNextTileId& Id) const
{
	if (const FEntry* Found = Entries.Find(Id))
	{
		return &Found->Tile;
	}
	return nullptr;
}

void FPlanetTerrainNextTileCache::Touch(const FPlanetTerrainNextTileId& Id, int64 Frame)
{
	if (FEntry* Found = Entries.Find(Id))
	{
		Found->LastUsedFrame = Frame;
	}
}

void FPlanetTerrainNextTileCache::Remove(const FPlanetTerrainNextTileId& Id)
{
	Entries.Remove(Id);
	Stats.Entries = Entries.Num();
}

void FPlanetTerrainNextTileCache::Clear()
{
	Entries.Reset();
	Stats.Entries = 0;
}

bool FPlanetTerrainNextTileCache::EvictOne(int64 Frame, TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned)
{
	(void)Frame;
	FPlanetTerrainNextTileId OldestKey;
	int64 OldestFrame = TNumericLimits<int64>::Max();
	bool bFound = false;
	for (const TPair<FPlanetTerrainNextTileId, FEntry>& Pair : Entries)
	{
		if (IsPinned(Pair.Key))
		{
			continue;
		}
		if (Pair.Value.LastUsedFrame < OldestFrame)
		{
			OldestFrame = Pair.Value.LastUsedFrame;
			OldestKey = Pair.Key;
			bFound = true;
		}
	}
	if (!bFound)
	{
		return false;
	}
	Entries.Remove(OldestKey);
	++Stats.Evictions;
	Stats.Entries = Entries.Num();
	return true;
}

void FPlanetTerrainNextTileCache::RecordBakeTime(double Ms)
{
	Stats.LastBakeMs = Ms;
	constexpr int32 Samples = 256;
	if (BakeTimesMs.Num() < Samples)
	{
		BakeTimesMs.Add(Ms);
	}
	else
	{
		BakeTimesMs[BakeTimeCursor] = Ms;
		BakeTimeCursor = (BakeTimeCursor + 1) % Samples;
	}
	TArray<double> Sorted = BakeTimesMs;
	Sorted.Sort();
	Stats.P95BakeMs = Sorted[FMath::Clamp(FMath::CeilToInt(0.95 * Sorted.Num()) - 1, 0, Sorted.Num() - 1)];
}

void FPlanetTerrainNextTileCache::WriteThroughDisk(const FPlanetTerrainNextHeightTile& Tile)
{
	if (!Disk.IsValid() || !Disk->IsEnabled())
	{
		return;
	}
	// Bootstrap-only path: initial roots may persist synchronously while the
	// runtime refinement path below always uses the asynchronous queue.
	if (Disk->Save(Tile))
	{
		++Stats.DiskWrites;
	}
}

const FPlanetTerrainNextHeightTile* FPlanetTerrainNextTileCache::GetOrBake(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextTileId& Id,
	int64 Frame,
	TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_TileCache_GetOrBake_GameThread);
	if (FEntry* Found = Entries.Find(Id))
	{
		Found->LastUsedFrame = Frame;
		++Stats.Hits;
		Stats.Entries = Entries.Num();
		return &Found->Tile;
	}

	++Stats.Misses;
	while (Entries.Num() >= MaxEntries)
	{
		if (!EvictOne(Frame, IsPinned))
		{
			break;
		}
	}
	if (Entries.Num() >= MaxEntries)
	{
		return nullptr;
	}

	FEntry Entry;
	Entry.LastUsedFrame = Frame;

	if (Disk.IsValid() && Disk->IsEnabled())
	{
		if (Disk->TryLoad(Id, Entry.Tile))
		{
			++Stats.DiskHits;
			FEntry& Stored = Entries.Add(Id, MoveTemp(Entry));
			Stats.Entries = Entries.Num();
			return &Stored.Tile;
		}
		++Stats.DiskMisses;
	}

	const double Start = FPlatformTime::Seconds();
	PlanetTerrainNextHeightTile::Bake(HeightField, Id, Entry.Tile);
	RecordBakeTime((FPlatformTime::Seconds() - Start) * 1000.0);
	++Stats.BakesThisFrame;
	WriteThroughDisk(Entry.Tile);

	FEntry& Stored = Entries.Add(Id, MoveTemp(Entry));
	Stats.Entries = Entries.Num();
	return &Stored.Tile;
}

bool FPlanetTerrainNextTileCache::Insert(
	FPlanetTerrainNextHeightTile&& Tile,
	int64 Frame,
	TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_TileCache_Insert_GameThread);
	if (!Tile.IsValid())
	{
		return false;
	}
	const FPlanetTerrainNextTileId Id = Tile.Id;
	if (FEntry* Found = Entries.Find(Id))
	{
		Found->LastUsedFrame = Frame;
		Found->Tile = MoveTemp(Tile);
		if (Disk.IsValid() && Disk->IsEnabled())
		{
			Disk->EnqueueSave(Found->Tile);
			++Stats.DiskWrites;
		}
		++Stats.Hits;
		Stats.Entries = Entries.Num();
		return true;
	}

	while (Entries.Num() >= MaxEntries)
	{
		if (!EvictOne(Frame, IsPinned))
		{
			break;
		}
	}
	if (Entries.Num() >= MaxEntries)
	{
		return false;
	}

	if (Disk.IsValid() && Disk->IsEnabled())
	{
		Disk->EnqueueSave(Tile);
		++Stats.DiskWrites;
	}

	FEntry Entry;
	Entry.LastUsedFrame = Frame;
	Entry.Tile = MoveTemp(Tile);
	++Stats.BakesThisFrame;
	Entries.Add(Id, MoveTemp(Entry));
	Stats.Entries = Entries.Num();
	return true;
}
