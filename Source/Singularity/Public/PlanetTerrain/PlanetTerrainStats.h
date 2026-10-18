#pragma once

#include "CoreMinimal.h"
#include <atomic>

/**
 * Lightweight counters + Insights-friendly scopes for terrain streaming v2.
 * Use TRACE_CPUPROFILER_EVENT_SCOPE in hot paths; read counters from HUD.
 */
struct FPlanetTerrainStats
{
	std::atomic<int32> JobsEnqueued{0};
	std::atomic<int32> JobsCancelledQueued{0};
	std::atomic<int32> JobsCancelledRunning{0};
	std::atomic<int32> JobsCompleted{0};
	std::atomic<int32> JobsObsoleteDiscarded{0};
	std::atomic<int32> CacheHits{0};
	std::atomic<int32> CacheMisses{0};
	std::atomic<int32> UploadsCreate{0};
	std::atomic<int32> UploadsUpdate{0};
	std::atomic<int32> UploadsSkippedBudget{0};
	std::atomic<int32> TilesFrustumCulled{0};
	std::atomic<int32> TilesHorizonCulled{0};
	std::atomic<int32> SplitsScreenError{0};
	std::atomic<int32> BudgetExhausted{0};
	std::atomic<double> LastBakeMs{0.0};
	std::atomic<double> LastUploadMs{0.0};
	std::atomic<double> FrameUploadMs{0.0};

	void ResetFrame()
	{
		FrameUploadMs.store(0.0);
		UploadsSkippedBudget.store(0);
	}

	FString HudLine() const
	{
		return FString::Printf(
			TEXT("enq=%d cancelQ=%d cancelR=%d done=%d obsolete=%d hit/miss=%d/%d create/upd=%d/%d cullF/H=%d/%d"),
			JobsEnqueued.load(), JobsCancelledQueued.load(), JobsCancelledRunning.load(),
			JobsCompleted.load(), JobsObsoleteDiscarded.load(),
			CacheHits.load(), CacheMisses.load(),
			UploadsCreate.load(), UploadsUpdate.load(),
			TilesFrustumCulled.load(), TilesHorizonCulled.load());
	}
};
