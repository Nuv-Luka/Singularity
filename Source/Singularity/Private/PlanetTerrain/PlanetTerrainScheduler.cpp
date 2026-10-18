#include "PlanetTerrain/PlanetTerrainScheduler.h"
#include "PlanetTerrain/PlanetTerrainGpuBake.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
std::atomic<bool> GPlanetTerrainPauseGeneration{false};
}

void FPlanetTerrainScheduler::SetGlobalPauseGeneration(bool bPause)
{
	GPlanetTerrainPauseGeneration.store(bPause);
}

bool FPlanetTerrainScheduler::IsGlobalPauseGeneration()
{
	return GPlanetTerrainPauseGeneration.load();
}

FPlanetTerrainScheduler::FPlanetTerrainScheduler() = default;

FPlanetTerrainScheduler::~FPlanetTerrainScheduler()
{
	Shutdown();
}

FPlanetTerrainTileKey FPlanetTerrainScheduler::MakeCacheKey(const FPlanetTerrainTileKey& Key)
{
	FPlanetTerrainTileKey K = Key;
	K.GenerationEpoch = 0;
	return K;
}

void FPlanetTerrainScheduler::Shutdown()
{
	bShutdown.store(true);
	{
		FScopeLock Lock(&QueueMutex);
		Queue.Reset();
	}
	{
		FScopeLock ReadyLock(&ReadyMutex);
		ReadyList.Reset();
	}
	{
		FScopeLock InflightLock(&InflightMutex);
		InflightKeys.Reset();
		InflightPriorities.Reset();
		CancelledKeys.Reset();
	}
	OnTileReady.Unbind();
	CurrentEpoch.fetch_add(1);
}

void FPlanetTerrainScheduler::SetEpoch(uint32 Epoch)
{
	CurrentEpoch.store(Epoch);
}

void FPlanetTerrainScheduler::SetPlanet(const FRockyPlanetDescriptor& Descriptor)
{
	const bool bSamePlanet = Planet.PlanetID == Descriptor.PlanetID
		&& Planet.TerrainSeed == Descriptor.TerrainSeed
		&& Planet.GeneratorVersion == Descriptor.GeneratorVersion;
	Planet = Descriptor;
	HeightField = MakeShared<FPlanetTerrainHeight, ESPMode::ThreadSafe>(Descriptor);
	if (!bSamePlanet)
	{
		FScopeLock Lock(&CacheMutex);
		Cache.Reset();
	}
}

void FPlanetTerrainScheduler::SortQueue()
{
	Queue.Sort([](const FPlanetTerrainJob& A, const FPlanetTerrainJob& B)
	{
		if (A.Priority != B.Priority)
		{
			return static_cast<uint8>(A.Priority) < static_cast<uint8>(B.Priority);
		}
		// Higher screen error first within the same band.
		return A.ScreenErrorPx > B.ScreenErrorPx;
	});
}

bool FPlanetTerrainScheduler::IsJobCancelled(const FPlanetTerrainJob& Job) const
{
	if (Job.bCancelled || Job.Epoch != CurrentEpoch.load())
	{
		return true;
	}
	FScopeLock InflightLock(&InflightMutex);
	return CancelledKeys.Contains(MakeCacheKey(Job.Key));
}

void FPlanetTerrainScheduler::Enqueue(const FPlanetTerrainJob& Job)
{
	// Additive enqueue — does NOT prune the desired set (use SyncDesiredSet for that).
	if (bShutdown.load())
	{
		return;
	}
	const FPlanetTerrainTileKey CK = MakeCacheKey(Job.Key);
	{
		FScopeLock Lock(&CacheMutex);
		if (const TSharedPtr<FPlanetTerrainTileData>* Found = Cache.Find(CK))
		{
			if (Found->IsValid() && (*Found)->State >= EPlanetTerrainTileState::ReadyForUpload)
			{
				++CacheHits;
				++Stats.CacheHits;
				(*Found)->LastUseSeconds = FPlatformTime::Seconds();
				FScopeLock ReadyLock(&ReadyMutex);
				ReadyList.Add(*Found);
				return;
			}
		}
	}
	FScopeLock Lock(&QueueMutex);
	for (FPlanetTerrainJob& J : Queue)
	{
		if (MakeCacheKey(J.Key) == CK)
		{
			if (static_cast<uint8>(Job.Priority) < static_cast<uint8>(J.Priority)
				|| Job.ScreenErrorPx > J.ScreenErrorPx)
			{
				J.Priority = Job.Priority;
				J.ScreenErrorPx = Job.ScreenErrorPx;
				J.Epoch = Job.Epoch;
			}
			SortQueue();
			return;
		}
	}
	{
		FScopeLock InflightLock(&InflightMutex);
		if (InflightKeys.Contains(CK))
		{
			CancelledKeys.Remove(CK);
			return;
		}
	}
	Queue.Add(Job);
	++Stats.JobsEnqueued;
	SortQueue();
}

void FPlanetTerrainScheduler::SyncDesiredSet(const TArray<FPlanetTerrainJob>& Desired)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_SyncDesiredSet);
	if (bShutdown.load())
	{
		return;
	}

	TMap<FPlanetTerrainTileKey, FPlanetTerrainJob> DesiredMap;
	DesiredMap.Reserve(Desired.Num());
	for (const FPlanetTerrainJob& J : Desired)
	{
		const FPlanetTerrainTileKey CK = MakeCacheKey(J.Key);
		if (const FPlanetTerrainJob* Existing = DesiredMap.Find(CK))
		{
			if (static_cast<uint8>(J.Priority) < static_cast<uint8>(Existing->Priority)
				|| J.ScreenErrorPx > Existing->ScreenErrorPx)
			{
				DesiredMap[CK] = J;
			}
		}
		else
		{
			DesiredMap.Add(CK, J);
		}
	}

	// Serve cache hits; dedupe ReadyList so we don't flood the game thread.
	TArray<FPlanetTerrainJob> NeedBake;
	NeedBake.Reserve(DesiredMap.Num());
	{
		FScopeLock Lock(&CacheMutex);
		for (const auto& Pair : DesiredMap)
		{
			if (const TSharedPtr<FPlanetTerrainTileData>* Found = Cache.Find(Pair.Key))
			{
				if (Found->IsValid() && (*Found)->State >= EPlanetTerrainTileState::ReadyForUpload)
				{
					++CacheHits;
					++Stats.CacheHits;
					(*Found)->LastUseSeconds = FPlatformTime::Seconds();
					FScopeLock ReadyLock(&ReadyMutex);
					bool bDup = false;
					for (const TSharedPtr<FPlanetTerrainTileData>& R : ReadyList)
					{
						if (R.IsValid() && MakeCacheKey(R->Key) == Pair.Key)
						{
							bDup = true;
							break;
						}
					}
					if (!bDup)
					{
						ReadyList.Add(*Found);
					}
					continue;
				}
			}
			++CacheMisses;
			++Stats.CacheMisses;
			NeedBake.Add(Pair.Value);
		}
	}

	{
		FScopeLock Lock(&QueueMutex);
		// Drop obsolete queued jobs; keep/reprioritize survivors.
		TArray<FPlanetTerrainJob> Kept;
		Kept.Reserve(Queue.Num());
		for (FPlanetTerrainJob& J : Queue)
		{
			const FPlanetTerrainTileKey CK = MakeCacheKey(J.Key);
			if (const FPlanetTerrainJob* Want = DesiredMap.Find(CK))
			{
				J.Priority = Want->Priority;
				J.ScreenErrorPx = Want->ScreenErrorPx;
				J.Epoch = Want->Epoch;
				J.bCancelled = false;
				Kept.Add(J);
			}
			else
			{
				++Stats.JobsCancelledQueued;
			}
		}
		Queue = MoveTemp(Kept);

		TSet<FPlanetTerrainTileKey> QueuedOrInflight;
		for (const FPlanetTerrainJob& J : Queue)
		{
			QueuedOrInflight.Add(MakeCacheKey(J.Key));
		}
		{
			FScopeLock InflightLock(&InflightMutex);
			for (const FPlanetTerrainTileKey& K : InflightKeys)
			{
				QueuedOrInflight.Add(MakeCacheKey(K));
			}
			// Revalidate critical work: only Level-0 MandatoryCoverage stays uncancellable
			// when absent from Desired. Stale Underfoot/Collision/landing jobs demote.
			for (const FPlanetTerrainTileKey& K : InflightKeys)
			{
				const FPlanetTerrainTileKey CK = MakeCacheKey(K);
				if (DesiredMap.Contains(CK))
				{
					CancelledKeys.Remove(CK);
					if (const FPlanetTerrainJob* Want = DesiredMap.Find(CK))
					{
						InflightPriorities.Add(CK, Want->Priority); // live demotion/promotion
					}
					continue;
				}
				const EPlanetTerrainJobPriority* Pri = InflightPriorities.Find(CK);
				const EPlanetTerrainJobPriority P = Pri ? *Pri : EPlanetTerrainJobPriority::ScreenError;
				// Peak/emergency-repair: never cancel MandatoryCoverage…Collision mid-bake.
				const bool bCoverageCritical = CK.Level <= 1
					|| static_cast<uint8>(P) <= static_cast<uint8>(EPlanetTerrainJobPriority::Collision);
				if (bCoverageCritical)
				{
					continue;
				}
				CancelledKeys.Add(CK);
				++Stats.JobsCancelledRunning;
			}
		}

		for (const FPlanetTerrainJob& J : NeedBake)
		{
			const FPlanetTerrainTileKey CK = MakeCacheKey(J.Key);
			if (QueuedOrInflight.Contains(CK))
			{
				continue;
			}
			Queue.Add(J);
			QueuedOrInflight.Add(CK);
			++Stats.JobsEnqueued;
		}
		SortQueue();
	}
}

int32 FPlanetTerrainScheduler::GetQueueLength() const
{
	FScopeLock Lock(&QueueMutex);
	return Queue.Num() + Inflight.load();
}

TSharedPtr<FPlanetTerrainTileData> FPlanetTerrainScheduler::FindCached(const FPlanetTerrainTileKey& Key) const
{
	FScopeLock Lock(&CacheMutex);
	if (const TSharedPtr<FPlanetTerrainTileData>* Found = Cache.Find(MakeCacheKey(Key)))
	{
		return *Found;
	}
	return nullptr;
}

void FPlanetTerrainScheduler::PutCache(const TSharedPtr<FPlanetTerrainTileData>& Tile)
{
	if (!Tile.IsValid() || bShutdown.load())
	{
		return;
	}
	FScopeLock Lock(&CacheMutex);
	FPlanetTerrainTileKey CK = MakeCacheKey(Tile->Key);
	Tile->Key = CK;
	Cache.Add(CK, Tile);
}

void FPlanetTerrainScheduler::EvictToBudget(int64 MaxBytes, const TSet<FPlanetTerrainTileKey>& PinKeys)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_EvictCache);
	FScopeLock Lock(&CacheMutex);
	int64 Bytes = 0;
	for (const auto& Pair : Cache)
	{
		if (Pair.Value.IsValid())
		{
			Bytes += Pair.Value->HeightFineM.Num() * (sizeof(float) * 2 + sizeof(FVector3d));
		}
	}
	if (Bytes <= MaxBytes)
	{
		return;
	}

	TArray<FPlanetTerrainTileKey> Keys;
	Cache.GetKeys(Keys);
	Keys.Sort([&](const FPlanetTerrainTileKey& A, const FPlanetTerrainTileKey& B)
	{
		const TSharedPtr<FPlanetTerrainTileData>& TA = Cache[A];
		const TSharedPtr<FPlanetTerrainTileData>& TB = Cache[B];
		return (TA.IsValid() ? TA->LastUseSeconds : 0.0) < (TB.IsValid() ? TB->LastUseSeconds : 0.0);
	});

	TSet<FPlanetTerrainTileKey> PinCache;
	for (const FPlanetTerrainTileKey& P : PinKeys)
	{
		PinCache.Add(MakeCacheKey(P));
	}

	for (const FPlanetTerrainTileKey& Key : Keys)
	{
		if (Bytes <= MaxBytes)
		{
			break;
		}
		if (PinCache.Contains(Key))
		{
			continue;
		}
		if (TSharedPtr<FPlanetTerrainTileData>* T = Cache.Find(Key))
		{
			if (T->IsValid())
			{
				Bytes -= (*T)->HeightFineM.Num() * (sizeof(float) * 2 + sizeof(FVector3d));
				(*T)->State = EPlanetTerrainTileState::Evictable;
			}
			Cache.Remove(Key);
		}
	}
}

void FPlanetTerrainScheduler::CancelStale(uint32 LivingEpoch)
{
	FScopeLock Lock(&QueueMutex);
	const int32 Before = Queue.Num();
	Queue.RemoveAll([LivingEpoch](const FPlanetTerrainJob& J) { return J.Epoch != LivingEpoch; });
	Stats.JobsCancelledQueued += (Before - Queue.Num());
}

void FPlanetTerrainScheduler::HandleBakeComplete(
	TSharedPtr<FPlanetTerrainTileData> Tile,
	double Ms,
	uint32 JobEpoch,
	FPlanetTerrainJob Job)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_BakeComplete);
	Inflight.fetch_sub(1);
	{
		FScopeLock InflightLock(&InflightMutex);
		const FPlanetTerrainTileKey CK = MakeCacheKey(Job.Key);
		InflightKeys.Remove(CK);
		InflightPriorities.Remove(CK);
	}

	if (bShutdown.load())
	{
		return;
	}

	LastBakeMs = Ms;
	Stats.LastBakeMs.store(Ms);

	const bool bCancelled = IsJobCancelled(Job)
		|| Job.Epoch != CurrentEpoch.load()
		|| JobEpoch != CurrentEpoch.load();
	{
		FScopeLock InflightLock(&InflightMutex);
		CancelledKeys.Remove(MakeCacheKey(Job.Key));
	}

	// Always retain useful canonical height data — cancel only skips mesh upload.
	if (Tile.IsValid())
	{
		PutCache(Tile);
	}

	if (bCancelled || !Tile.IsValid())
	{
		++Stats.JobsObsoleteDiscarded;
		LaunchNext();
		return;
	}

	++Stats.JobsCompleted;
	{
		FScopeLock ReadyLock(&ReadyMutex);
		ReadyList.Add(Tile);
	}
	LaunchNext();
}

void FPlanetTerrainScheduler::LaunchNext()
{
	if (bShutdown.load())
	{
		return;
	}

	FPlanetTerrainJob Job;
	{
		FScopeLock Lock(&QueueMutex);
		if (Queue.Num() == 0 || Inflight.load() >= MaxConcurrent || !HeightField.IsValid())
		{
			return;
		}
		// Skip cancelled entries at head.
		while (Queue.Num() > 0 && Queue[0].bCancelled)
		{
			Queue.RemoveAt(0);
			++Stats.JobsCancelledQueued;
		}
		if (Queue.Num() == 0 || Inflight.load() >= MaxConcurrent)
		{
			return;
		}
		Job = Queue[0];
		Queue.RemoveAt(0);
		Inflight.fetch_add(1);
		FScopeLock InflightLock(&InflightMutex);
		const FPlanetTerrainTileKey CK = MakeCacheKey(Job.Key);
		InflightKeys.Add(CK);
		InflightPriorities.Add(CK, Job.Priority);
	}

	TSharedPtr<FPlanetTerrainHeight, ESPMode::ThreadSafe> LocalHeight = HeightField;
	const uint32 LivingEpoch = CurrentEpoch.load();
	TSharedPtr<FPlanetTerrainScheduler, ESPMode::ThreadSafe> SelfStrong = AsShared();
	const bool bUseGpuFloat = !bForceCpu;

	Async(EAsyncExecution::ThreadPool, [SelfStrong, Job, LocalHeight, LivingEpoch, bUseGpuFloat]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_WorkerBake);
		const double T0 = FPlatformTime::Seconds();
		TSharedPtr<FPlanetTerrainTileData> Tile;

		if (!SelfStrong->IsShutdown() && !SelfStrong->IsJobCancelled(Job) && LocalHeight.IsValid())
		{
			Tile = MakeShared<FPlanetTerrainTileData>();
			auto ShouldCancel = [SelfStrong, Job]()
			{
				return SelfStrong->IsShutdown() || SelfStrong->IsJobCancelled(Job);
			};
			FPlanetTerrainTileBakerEx::Bake(
				*LocalHeight, Job.Key, Job.U0, Job.V0, Job.U1, Job.V1, Job.Resolution, *Tile,
				bUseGpuFloat ? EPlanetTerrainBakeBackend::GpuFloatCpu : EPlanetTerrainBakeBackend::DoubleCpu,
				ShouldCancel);
			if (Tile->State != EPlanetTerrainTileState::ReadyForUpload)
			{
				Tile.Reset(); // incomplete — do not cache
			}
			else
			{
				Tile->LastUseSeconds = FPlatformTime::Seconds();
				// Completed-but-cancelled still reaches HandleBakeComplete for CPU cache.
			}
		}

		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		AsyncTask(ENamedThreads::GameThread, [SelfStrong, Tile, Ms, LivingEpoch, Job]()
		{
			SelfStrong->HandleBakeComplete(Tile, Ms, LivingEpoch, Job);
		});
	});
}

void FPlanetTerrainScheduler::Tick(float /*DeltaSeconds*/)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_SchedulerTick);
	if (bShutdown.load())
	{
		return;
	}

	TArray<TSharedPtr<FPlanetTerrainTileData>> LocalReady;
	{
		FScopeLock ReadyLock(&ReadyMutex);
		LocalReady = MoveTemp(ReadyList);
		ReadyList.Reset();
	}
	for (const TSharedPtr<FPlanetTerrainTileData>& Tile : LocalReady)
	{
		if (Tile.IsValid() && OnTileReady.IsBound())
		{
			OnTileReady.Execute(Tile);
		}
	}

	// Diagnostic: pause launching new bakes (coverage must remain via shell/parents).
	if (GPlanetTerrainPauseGeneration.load())
	{
		return;
	}

	while (Inflight.load() < MaxConcurrent)
	{
		const int32 Before = Inflight.load();
		LaunchNext();
		if (Inflight.load() == Before)
		{
			break;
		}
	}
}
