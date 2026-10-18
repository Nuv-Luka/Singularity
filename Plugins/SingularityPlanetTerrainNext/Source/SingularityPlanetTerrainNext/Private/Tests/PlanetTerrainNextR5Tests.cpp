#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextTileCache.h"
#include "PlanetTerrainNextTilePlanner.h"
#include "PlanetTerrainNextTileScheduler.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor MoonDescriptor(int64 Seed = 1)
{
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = Seed;
	Descriptor.PlanetId = 42;
	Descriptor.GeneratorVersion = 1;
	return Descriptor;
}

FPlanetTerrainNextTileId MakeId(
	const FPlanetTerrainNextHeightField& Field,
	const FPlanetTerrainNextDescriptor& Desc,
	const FPlanetTerrainNextNodeId& Node)
{
	return FPlanetTerrainNextTileId::Make(Desc.PlanetId, Desc.Seed, Desc.GeneratorVersion, Field.GetIdentityHash(), Node);
}

bool WaitForDrain(
	FPlanetTerrainNextTileScheduler& Scheduler,
	FPlanetTerrainNextTileCache& Cache,
	const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& Field,
	int32 ExpectedInserts,
	double TimeoutSeconds = 5.0)
{
	auto NeverPinned = [](const FPlanetTerrainNextTileId&) { return false; };
	const double Start = FPlatformTime::Seconds();
	int32 Inserted = 0;
	while ((FPlatformTime::Seconds() - Start) < TimeoutSeconds)
	{
		Scheduler.Pump(Field);
		Inserted += Scheduler.Drain(Cache, 1, NeverPinned);
		if (Inserted >= ExpectedInserts && Scheduler.GetStats().Baking == 0 && Scheduler.GetStats().Queued == 0)
		{
			return true;
		}
		FPlatformProcess::Sleep(0.01f);
	}
	return Inserted >= ExpectedInserts;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextPlannerPriorityTest,
	"Singularity.PlanetTerrainNext.R5.PlannerPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextPlannerPriorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	const FPlanetTerrainNextHeightField Field(Desc);

	TArray<FPlanetTerrainNextPendingBuild> Pending;
	{
		FPlanetTerrainNextPendingBuild A;
		A.Id = FPlanetTerrainNextNodeId(0, 0, 0, 0);
		A.Priority = 1.0e12 + 10.0; // visible root
		Pending.Add(A);
	}
	{
		FPlanetTerrainNextPendingBuild B;
		B.Id = FPlanetTerrainNextNodeId(0, 3, 1, 1);
		B.Priority = 1.0e12 + 100.0; // visible high error
		Pending.Add(B);
	}
	{
		FPlanetTerrainNextPendingBuild C;
		C.Id = FPlanetTerrainNextNodeId(1, 2, 0, 0);
		C.Priority = 5.0; // offscreen
		Pending.Add(C);
	}

	FPlanetTerrainNextTilePlanner Planner;
	TArray<FPlanetTerrainNextTileRequest> Out;
	Planner.Plan(
		Pending,
		[&](const FPlanetTerrainNextNodeId& Node) { return MakeId(Field, Desc, Node); },
		4.0,
		10,
		4,
		Out);

	TestTrue(TEXT("emits at least pending"), Out.Num() >= Pending.Num());
	TestTrue(TEXT("coverage/refine before prefetch"), Out[0].Class <= EPlanetTerrainNextRequestClass::Frustum);
	TestTrue(TEXT("priorities sorted descending"), Out[0].Priority >= Out.Last().Priority);
	TestTrue(TEXT("prefetch siblings bounded"), Planner.GetStats().Prefetch <= 4 + 1);

	// Dedupe: same node twice → one request.
	const FPlanetTerrainNextPendingBuild Dup = Pending[0];
	Pending.Add(Dup);
	TArray<FPlanetTerrainNextTileRequest> Deduped;
	Planner.Plan(
		Pending,
		[&](const FPlanetTerrainNextNodeId& Node) { return MakeId(Field, Desc, Node); },
		4.0,
		11,
		0,
		Deduped);
	TSet<FPlanetTerrainNextTileId> Keys;
	for (const FPlanetTerrainNextTileRequest& R : Deduped)
	{
		TestFalse(TEXT("no duplicate tile ids"), Keys.Contains(R.TileId));
		Keys.Add(R.TileId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextSchedulerDedupeCancelTest,
	"Singularity.PlanetTerrainNext.R5.SchedulerDedupeCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextSchedulerDedupeCancelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	auto Field = MakeShared<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>(Desc);
	FPlanetTerrainNextTileCache Cache(64);
	FPlanetTerrainNextTileScheduler Scheduler;
	Scheduler.Reset(Field->GetIdentityHash());
	Scheduler.SetBudgets(2, 32);
	Scheduler.BeginFrame(1);

	auto MakeReq = [&](const FPlanetTerrainNextNodeId& Node, EPlanetTerrainNextRequestClass Class, double Pri)
	{
		FPlanetTerrainNextTileRequest R;
		R.NodeId = Node;
		R.TileId = MakeId(*Field, Desc, Node);
		R.Class = Class;
		R.Priority = Pri;
		R.bRequiredForCoverage = Class <= EPlanetTerrainNextRequestClass::Frustum;
		R.DeadlineFrame = 5;
		return R;
	};

	TArray<FPlanetTerrainNextTileRequest> First;
	First.Add(MakeReq(FPlanetTerrainNextNodeId(0, 1, 0, 0), EPlanetTerrainNextRequestClass::Coverage, 1000));
	First.Add(MakeReq(FPlanetTerrainNextNodeId(0, 1, 0, 0), EPlanetTerrainNextRequestClass::Coverage, 2000)); // dedupe raise
	First.Add(MakeReq(FPlanetTerrainNextNodeId(0, 2, 0, 0), EPlanetTerrainNextRequestClass::Prefetch, 10));
	Scheduler.SetDesired(First);
	TestEqual(TEXT("dedupe keeps one coverage job"), Scheduler.GetStats().Queued, 2);
	TestTrue(TEXT("dedupe counted"), Scheduler.GetStats().Deduped >= 1);

	// Camera turn: drop prefetch, keep coverage.
	TArray<FPlanetTerrainNextTileRequest> Turn;
	Turn.Add(MakeReq(FPlanetTerrainNextNodeId(0, 1, 0, 0), EPlanetTerrainNextRequestClass::Coverage, 3000));
	Turn.Add(MakeReq(FPlanetTerrainNextNodeId(1, 1, 0, 0), EPlanetTerrainNextRequestClass::VisibleRefine, 2500));
	Scheduler.BeginFrame(2);
	Scheduler.SetDesired(Turn);
	TestTrue(TEXT("prefetch cancelled on turn"), Scheduler.GetStats().CancelledThisFrame >= 1);
	TestTrue(TEXT("new visible refine queued"), Scheduler.IsInFlight(MakeId(*Field, Desc, FPlanetTerrainNextNodeId(1, 1, 0, 0))));

	Scheduler.Pump(Field);
	TestTrue(TEXT("workers capped"), Scheduler.GetStats().Baking <= 2);
	TestTrue(TEXT("async completes"), WaitForDrain(Scheduler, Cache, Field, 1));
	TestNotNull(TEXT("coverage tile cached"), Cache.Find(MakeId(*Field, Desc, FPlanetTerrainNextNodeId(0, 1, 0, 0))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextSchedulerStaleGenerationTest,
	"Singularity.PlanetTerrainNext.R5.SchedulerStaleGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextSchedulerStaleGenerationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	auto Field = MakeShared<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>(Desc);
	FPlanetTerrainNextTileCache Cache(32);
	FPlanetTerrainNextTileScheduler Scheduler;
	Scheduler.Reset(Field->GetIdentityHash());
	Scheduler.SetBudgets(4, 16);
	Scheduler.BeginFrame(1);

	FPlanetTerrainNextTileRequest Req;
	Req.NodeId = FPlanetTerrainNextNodeId(2, 1, 0, 0);
	Req.TileId = MakeId(*Field, Desc, Req.NodeId);
	Req.Class = EPlanetTerrainNextRequestClass::Frustum;
	Req.Priority = 100;
	Req.bRequiredForCoverage = true;
	TArray<FPlanetTerrainNextTileRequest> Desired;
	Desired.Add(Req);
	Scheduler.SetDesired(Desired);
	Scheduler.Pump(Field);

	// Planet version change mid-flight — completions must not attach.
	Scheduler.Reset(Field->GetIdentityHash() ^ 0xDEADBEEFull);
	Scheduler.BeginFrame(2);
	auto NeverPinned = [](const FPlanetTerrainNextTileId&) { return false; };
	const double Start = FPlatformTime::Seconds();
	while ((FPlatformTime::Seconds() - Start) < 2.0)
	{
		Scheduler.Drain(Cache, 2, NeverPinned);
		FPlatformProcess::Sleep(0.01f);
		if (Scheduler.GetStats().StaleCompletions > 0)
		{
			break;
		}
	}
	// Either stale counted or job was wiped by Reset before finish — either way cache must stay empty for that key.
	TestNull(TEXT("stale bake not inserted under new generation"), Cache.Find(Req.TileId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextSchedulerWorkerStressTest,
	"Singularity.PlanetTerrainNext.R5.SchedulerWorkerStress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextSchedulerWorkerStressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	auto Field = MakeShared<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>(Desc);
	FPlanetTerrainNextTileCache Cache(128);

	for (const int32 Workers : {2, 4, 6})
	{
		FPlanetTerrainNextTileScheduler Scheduler;
		Scheduler.Reset(Field->GetIdentityHash());
		Scheduler.SetBudgets(Workers, 64);
		Scheduler.BeginFrame(1);

		TArray<FPlanetTerrainNextTileRequest> Desired;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			FPlanetTerrainNextTileRequest R;
			R.NodeId = FPlanetTerrainNextNodeId(static_cast<uint8>(Index % 6), 1, Index % 2, Index / 6);
			R.TileId = MakeId(*Field, Desc, R.NodeId);
			R.Class = EPlanetTerrainNextRequestClass::Frustum;
			R.Priority = 100.0 - Index;
			R.bRequiredForCoverage = true;
			Desired.Add(R);
		}
		Scheduler.SetDesired(Desired);
		Scheduler.Pump(Field);
		TestTrue(
			*FString::Printf(TEXT("workers=%d never exceed budget"), Workers),
			Scheduler.GetStats().Baking <= Workers);

		TestTrue(
			*FString::Printf(TEXT("workers=%d drain"), Workers),
			WaitForDrain(Scheduler, Cache, Field, 8));
	}
	return true;
}

#endif
