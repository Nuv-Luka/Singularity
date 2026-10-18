#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextTilePlanner.h"

class FPlanetTerrainNextTileCache;

enum class EPlanetTerrainNextJobState : uint8
{
	Queued = 0,
	Baking = 1,
	Ready = 2,
	Cancelled = 3,
};

struct FPlanetTerrainNextSchedulerStats
{
	int32 Queued = 0;
	int32 Baking = 0;
	int32 Ready = 0;
	int32 StartedThisFrame = 0;
	int32 CompletedThisFrame = 0;
	int32 CancelledThisFrame = 0;
	int32 StaleCompletions = 0;
	int32 Deduped = 0;
	int32 MaxWorkers = 0;
	double LastBakeMs = 0.0;
	double P95BakeMs = 0.0;
	double MaxQueueWaitFrames = 0.0;
};

/**
 * Bounded async height-tile bake queue. Workers only touch pure HeightField + tile bytes.
 * Game thread inserts into the CPU cache and builds meshes. No UObjects on workers.
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextTileScheduler
{
public:
	FPlanetTerrainNextTileScheduler();
	~FPlanetTerrainNextTileScheduler();

	void Reset(uint64 Generation);
	void BeginFrame(int64 Frame);
	void SetBudgets(int32 MaxWorkers, int32 MaxQueue);

	/**
	 * Replace the desired set for this frame: dedupe by tile id, raise priority,
	 * cancel queued optional work that fell out of desired. Coverage keys stay until ready.
	 */
	void SetDesired(const TArray<FPlanetTerrainNextTileRequest>& Desired);

	/** Launch bake jobs up to MaxWorkers. */
	void Pump(const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField);

	/** Move finished tiles into the CPU cache. Returns how many were inserted. */
	int32 Drain(
		FPlanetTerrainNextTileCache& Cache,
		int64 Frame,
		TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned);

	bool IsInFlight(const FPlanetTerrainNextTileId& Id) const;
	const FPlanetTerrainNextSchedulerStats& GetStats() const { return Stats; }

private:
	struct FJob
	{
		FPlanetTerrainNextTileRequest Request;
		EPlanetTerrainNextJobState State = EPlanetTerrainNextJobState::Queued;
		int64 EnqueuedFrame = 0;
		uint64 Generation = 0;
		TSharedPtr<FPlanetTerrainNextHeightTile, ESPMode::ThreadSafe> Result;
		TAtomic<bool> bCancel{false};
		TAtomic<bool> bDone{false};
		double BakeMs = 0.0;
	};

	void StartJob(const TSharedPtr<FJob>& Job, const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField);
	void RecordBakeTime(double Ms);
	void RebuildQueueOrder();

	uint64 Generation = 0;
	int64 CurrentFrame = 0;
	int32 MaxWorkers = 4;
	int32 MaxQueue = 256;
	TMap<FPlanetTerrainNextTileId, TSharedPtr<FJob>> Jobs;
	TArray<FPlanetTerrainNextTileId> QueueOrder;
	FPlanetTerrainNextSchedulerStats Stats;
	TArray<double> BakeTimesMs;
	int32 BakeTimeCursor = 0;
};
