#include "PlanetTerrainNextTileScheduler.h"

#include "PlanetTerrainNextTileCache.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"

FPlanetTerrainNextTileScheduler::FPlanetTerrainNextTileScheduler() = default;

FPlanetTerrainNextTileScheduler::~FPlanetTerrainNextTileScheduler()
{
	for (TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->bCancel.Store(true);
		}
	}
	Jobs.Reset();
	QueueOrder.Reset();
}

void FPlanetTerrainNextTileScheduler::Reset(uint64 InGeneration)
{
	for (TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->bCancel.Store(true);
		}
	}
	Jobs.Reset();
	QueueOrder.Reset();
	Generation = InGeneration;
	Stats = FPlanetTerrainNextSchedulerStats();
	Stats.MaxWorkers = MaxWorkers;
}

void FPlanetTerrainNextTileScheduler::BeginFrame(int64 Frame)
{
	CurrentFrame = Frame;
	Stats.StartedThisFrame = 0;
	Stats.CompletedThisFrame = 0;
	Stats.CancelledThisFrame = 0;
	Stats.Deduped = 0;
	Stats.MaxWorkers = MaxWorkers;
}

void FPlanetTerrainNextTileScheduler::SetBudgets(int32 InMaxWorkers, int32 InMaxQueue)
{
	MaxWorkers = FMath::Clamp(InMaxWorkers, 1, 16);
	MaxQueue = FMath::Clamp(InMaxQueue, 8, 4096);
	Stats.MaxWorkers = MaxWorkers;
}

void FPlanetTerrainNextTileScheduler::RebuildQueueOrder()
{
	QueueOrder.Reset();
	QueueOrder.Reserve(Jobs.Num());
	for (const TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		if (Pair.Value.IsValid() && Pair.Value->State == EPlanetTerrainNextJobState::Queued)
		{
			QueueOrder.Add(Pair.Key);
		}
	}
	QueueOrder.Sort([this](const FPlanetTerrainNextTileId& A, const FPlanetTerrainNextTileId& B)
	{
		const TSharedPtr<FJob>* JA = Jobs.Find(A);
		const TSharedPtr<FJob>* JB = Jobs.Find(B);
		if (!JA || !JB || !JA->IsValid() || !JB->IsValid())
		{
			return false;
		}
		return (*JA)->Request.Priority > (*JB)->Request.Priority;
	});
}

void FPlanetTerrainNextTileScheduler::SetDesired(const TArray<FPlanetTerrainNextTileRequest>& Desired)
{
	TSet<FPlanetTerrainNextTileId> DesiredKeys;
	DesiredKeys.Reserve(Desired.Num());

	for (const FPlanetTerrainNextTileRequest& Request : Desired)
	{
		DesiredKeys.Add(Request.TileId);
		if (TSharedPtr<FJob>* Existing = Jobs.Find(Request.TileId))
		{
			FJob& Job = **Existing;
			++Stats.Deduped;
			if (Job.State == EPlanetTerrainNextJobState::Queued || Job.State == EPlanetTerrainNextJobState::Cancelled)
			{
				Job.Request = Request;
				Job.State = EPlanetTerrainNextJobState::Queued;
				Job.bCancel.Store(false);
				Job.Generation = Generation;
			}
			else if (Job.State == EPlanetTerrainNextJobState::Baking)
			{
				// Raise priority bookkeeping; cancel flag stays false while desired.
				if (Request.Priority > Job.Request.Priority)
				{
					Job.Request.Priority = Request.Priority;
					Job.Request.Class = Request.Class;
					Job.Request.bRequiredForCoverage = Job.Request.bRequiredForCoverage || Request.bRequiredForCoverage;
				}
				Job.bCancel.Store(false);
			}
			continue;
		}

		if (Jobs.Num() >= MaxQueue && Request.Class == EPlanetTerrainNextRequestClass::Prefetch)
		{
			continue;
		}
		if (Jobs.Num() >= MaxQueue)
		{
			// Drop lowest-priority queued optional to make room for coverage.
			FPlanetTerrainNextTileId Victim;
			double Worst = TNumericLimits<double>::Max();
			bool bFound = false;
			for (const TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
			{
				if (!Pair.Value.IsValid() || Pair.Value->State != EPlanetTerrainNextJobState::Queued)
				{
					continue;
				}
				if (Pair.Value->Request.bRequiredForCoverage)
				{
					continue;
				}
				if (Pair.Value->Request.Priority < Worst)
				{
					Worst = Pair.Value->Request.Priority;
					Victim = Pair.Key;
					bFound = true;
				}
			}
			if (bFound)
			{
				Jobs.Remove(Victim);
				++Stats.CancelledThisFrame;
			}
			else if (Jobs.Num() >= MaxQueue)
			{
				continue;
			}
		}

		TSharedPtr<FJob> Job = MakeShared<FJob>();
		Job->Request = Request;
		Job->State = EPlanetTerrainNextJobState::Queued;
		Job->EnqueuedFrame = CurrentFrame;
		Job->Generation = Generation;
		Jobs.Add(Request.TileId, Job);
	}

	// Cancel queued/baking work that left the desired set (optional only for baking).
	TArray<FPlanetTerrainNextTileId> ToRemove;
	for (TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		FJob& Job = *Pair.Value;
		if (DesiredKeys.Contains(Pair.Key))
		{
			continue;
		}
		if (Job.State == EPlanetTerrainNextJobState::Queued)
		{
			ToRemove.Add(Pair.Key);
			++Stats.CancelledThisFrame;
		}
		else if (Job.State == EPlanetTerrainNextJobState::Baking)
		{
			if (!Job.Request.bRequiredForCoverage)
			{
				Job.bCancel.Store(true);
				Job.State = EPlanetTerrainNextJobState::Cancelled;
				++Stats.CancelledThisFrame;
			}
		}
		else if (Job.State == EPlanetTerrainNextJobState::Cancelled && Job.bDone.Load())
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FPlanetTerrainNextTileId& Key : ToRemove)
	{
		Jobs.Remove(Key);
	}

	RebuildQueueOrder();

	Stats.Queued = 0;
	Stats.Baking = 0;
	Stats.Ready = 0;
	Stats.MaxQueueWaitFrames = 0.0;
	for (const TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}
		switch (Pair.Value->State)
		{
		case EPlanetTerrainNextJobState::Queued:
			++Stats.Queued;
			Stats.MaxQueueWaitFrames = FMath::Max(Stats.MaxQueueWaitFrames, static_cast<double>(CurrentFrame - Pair.Value->EnqueuedFrame));
			break;
		case EPlanetTerrainNextJobState::Baking: ++Stats.Baking; break;
		case EPlanetTerrainNextJobState::Ready: ++Stats.Ready; break;
		default: break;
		}
	}
}

void FPlanetTerrainNextTileScheduler::StartJob(
	const TSharedPtr<FJob>& Job,
	const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField)
{
	if (!Job.IsValid() || !HeightField.IsValid())
	{
		return;
	}
	Job->State = EPlanetTerrainNextJobState::Baking;
	Job->Result = MakeShared<FPlanetTerrainNextHeightTile, ESPMode::ThreadSafe>();
	Job->bDone.Store(false);
	Job->bCancel.Store(false);

	const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe> Field = HeightField;
	const TSharedPtr<FJob> JobPtr = Job;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [JobPtr, Field]()
	{
		if (!JobPtr.IsValid() || !Field.IsValid())
		{
			return;
		}
		if (JobPtr->bCancel.Load())
		{
			JobPtr->bDone.Store(true);
			return;
		}
		const double Start = FPlatformTime::Seconds();
		PlanetTerrainNextHeightTile::Bake(*Field, JobPtr->Request.TileId, *JobPtr->Result);
		JobPtr->BakeMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		JobPtr->bDone.Store(true);
	});
	++Stats.StartedThisFrame;
}

void FPlanetTerrainNextTileScheduler::Pump(const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField)
{
	int32 InFlight = 0;
	for (const TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		if (Pair.Value.IsValid() && Pair.Value->State == EPlanetTerrainNextJobState::Baking && !Pair.Value->bDone.Load())
		{
			++InFlight;
		}
	}

	RebuildQueueOrder();
	for (const FPlanetTerrainNextTileId& Key : QueueOrder)
	{
		if (InFlight >= MaxWorkers)
		{
			break;
		}
		TSharedPtr<FJob>* Found = Jobs.Find(Key);
		if (!Found || !Found->IsValid() || (*Found)->State != EPlanetTerrainNextJobState::Queued)
		{
			continue;
		}
		StartJob(*Found, HeightField);
		++InFlight;
	}

	Stats.Baking = 0;
	Stats.Queued = 0;
	for (const TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}
		if (Pair.Value->State == EPlanetTerrainNextJobState::Baking && !Pair.Value->bDone.Load())
		{
			++Stats.Baking;
		}
		else if (Pair.Value->State == EPlanetTerrainNextJobState::Queued)
		{
			++Stats.Queued;
		}
	}
}

int32 FPlanetTerrainNextTileScheduler::Drain(
	FPlanetTerrainNextTileCache& Cache,
	int64 Frame,
	TFunctionRef<bool(const FPlanetTerrainNextTileId&)> IsPinned)
{
	int32 Inserted = 0;
	TArray<FPlanetTerrainNextTileId> ToRemove;
	for (TPair<FPlanetTerrainNextTileId, TSharedPtr<FJob>>& Pair : Jobs)
	{
		FJob& Job = *Pair.Value;
		if (!Job.bDone.Load())
		{
			continue;
		}

		if (Job.Generation != Generation)
		{
			++Stats.StaleCompletions;
			ToRemove.Add(Pair.Key);
			continue;
		}

		if (Job.bCancel.Load() || Job.State == EPlanetTerrainNextJobState::Cancelled)
		{
			// Spec: optional cancelled work may still warm the cache if bake finished.
			if (Job.Result.IsValid() && Job.Result->IsValid())
			{
				if (Cache.Insert(MoveTemp(*Job.Result), Frame, IsPinned))
				{
					++Inserted;
					RecordBakeTime(Job.BakeMs);
				}
			}
			ToRemove.Add(Pair.Key);
			continue;
		}

		if (Job.Result.IsValid() && Job.Result->IsValid())
		{
			if (Job.Result->Id != Job.Request.TileId
				|| Job.Result->Id.ParameterHash != Job.Request.TileId.ParameterHash
				|| Job.Result->Id.Seed != Job.Request.TileId.Seed)
			{
				++Stats.StaleCompletions;
			}
			else if (Cache.Insert(MoveTemp(*Job.Result), Frame, IsPinned))
			{
				++Inserted;
				RecordBakeTime(Job.BakeMs);
				++Stats.CompletedThisFrame;
			}
		}
		ToRemove.Add(Pair.Key);
	}
	for (const FPlanetTerrainNextTileId& Key : ToRemove)
	{
		Jobs.Remove(Key);
	}
	Stats.Ready = 0;
	return Inserted;
}

bool FPlanetTerrainNextTileScheduler::IsInFlight(const FPlanetTerrainNextTileId& Id) const
{
	if (const TSharedPtr<FJob>* Found = Jobs.Find(Id))
	{
		return Found->IsValid()
			&& ((*Found)->State == EPlanetTerrainNextJobState::Queued
				|| (*Found)->State == EPlanetTerrainNextJobState::Baking);
	}
	return false;
}

void FPlanetTerrainNextTileScheduler::RecordBakeTime(double Ms)
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
