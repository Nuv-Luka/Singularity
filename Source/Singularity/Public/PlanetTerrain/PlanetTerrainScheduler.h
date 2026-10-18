#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainTile.h"
#include "PlanetTerrain/PlanetTerrainHeight.h"
#include "PlanetTerrain/PlanetTerrainStats.h"
#include <atomic>

DECLARE_DELEGATE_OneParam(FOnPlanetTerrainTileReady, TSharedPtr<FPlanetTerrainTileData>);

struct FPlanetTerrainJob
{
	FPlanetTerrainTileKey Key;
	double U0 = 0, V0 = 0, U1 = 1, V1 = 1;
	int32 Resolution = 32;
	EPlanetTerrainJobPriority Priority = EPlanetTerrainJobPriority::ScreenError;
	uint32 Epoch = 0;
	/** Screen-space error estimate used for sorting within the same priority band. */
	float ScreenErrorPx = 0.0f;
	bool bCancelled = false;
};

/**
 * Preemptive tile baker (streaming v2).
 * - Stable CPU cache identity ignores GenerationEpoch
 * - SyncDesiredSet prunes obsolete queued work and reprioritizes
 * - Running jobs check cancel flag / epoch before upload
 */
class FPlanetTerrainScheduler : public TSharedFromThis<FPlanetTerrainScheduler, ESPMode::ThreadSafe>
{
public:
	FPlanetTerrainScheduler();
	~FPlanetTerrainScheduler();

	void SetPlanet(const FRockyPlanetDescriptor& Descriptor);
	void SetEpoch(uint32 Epoch);
	void SetMaxConcurrent(int32 N) { MaxConcurrent = FMath::Clamp(N, 1, 12); }
	void SetForceCpu(bool bIn) { bForceCpu = bIn; }

	void Enqueue(const FPlanetTerrainJob& Job);

	/**
	 * Replace the desired work set for this frame: prune obsolete queued jobs,
	 * coalesce duplicates, enqueue missing, raise priorities of survivors.
	 */
	void SyncDesiredSet(const TArray<FPlanetTerrainJob>& Desired);

	void Tick(float DeltaSeconds);
	void CancelStale(uint32 LivingEpoch);

	void Shutdown();
	bool IsShutdown() const { return bShutdown.load(); }

	int32 GetQueueLength() const;
	int32 GetActiveWorkers() const { return Inflight.load(); }
	double GetLastBakeMs() const { return LastBakeMs; }
	int32 GetCacheHits() const { return CacheHits; }
	int32 GetCacheMisses() const { return CacheMisses; }
	const FPlanetTerrainStats& GetStats() const { return Stats; }
	FPlanetTerrainStats& GetStatsMutable() { return Stats; }

	TSharedPtr<FPlanetTerrainTileData> FindCached(const FPlanetTerrainTileKey& Key) const;
	void PutCache(const TSharedPtr<FPlanetTerrainTileData>& Tile);
	void EvictToBudget(int64 MaxBytes, const TSet<FPlanetTerrainTileKey>& PinKeys);

	FOnPlanetTerrainTileReady OnTileReady;

	/** Cache lookup ignoring GenerationEpoch (canonical tile identity). */
	static FPlanetTerrainTileKey MakeCacheKey(const FPlanetTerrainTileKey& Key);

	/** Diagnostic: stop launching new bakes globally (shell/parents must remain). */
	static void SetGlobalPauseGeneration(bool bPause);
	static bool IsGlobalPauseGeneration();

private:
	FRockyPlanetDescriptor Planet;
	TSharedPtr<FPlanetTerrainHeight, ESPMode::ThreadSafe> HeightField;
	std::atomic<uint32> CurrentEpoch{0};
	std::atomic<int32> Inflight{0};
	std::atomic<bool> bShutdown{false};
	int32 MaxConcurrent = 8;
	bool bForceCpu = false; // GpuFloat bake — much faster than double CPU
	double LastBakeMs = 0.0;
	int32 CacheHits = 0;
	int32 CacheMisses = 0;
	FPlanetTerrainStats Stats;

	TArray<FPlanetTerrainJob> Queue;
	TMap<FPlanetTerrainTileKey, TSharedPtr<FPlanetTerrainTileData>> Cache;
	TSet<FPlanetTerrainTileKey> InflightKeys;
	TMap<FPlanetTerrainTileKey, EPlanetTerrainJobPriority> InflightPriorities;
	TSet<FPlanetTerrainTileKey> CancelledKeys;
	mutable FCriticalSection QueueMutex;
	mutable FCriticalSection CacheMutex;
	mutable FCriticalSection ReadyMutex;
	mutable FCriticalSection InflightMutex;
	TArray<TSharedPtr<FPlanetTerrainTileData>> ReadyList;

	void LaunchNext();
	void SortQueue();
	void HandleBakeComplete(TSharedPtr<FPlanetTerrainTileData> Tile, double Ms, uint32 JobEpoch, FPlanetTerrainJob Job);
	bool IsJobCancelled(const FPlanetTerrainJob& Job) const;
};
