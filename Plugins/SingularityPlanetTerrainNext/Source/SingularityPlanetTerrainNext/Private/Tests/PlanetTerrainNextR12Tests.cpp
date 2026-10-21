#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextQuadtree.h"
#include "PlanetTerrainNextTilePlanner.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR12PredictivePrefetchBoundedTest,
	"Singularity.PlanetTerrainNext.R12.PredictivePrefetchBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR12PredictivePrefetchBoundedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextTilePlanner Planner;
	TArray<FPlanetTerrainNextTileRequest> Requests;
	TArray<FPlanetTerrainNextPendingBuild> Candidates;
	for (uint32 Index = 0; Index < 6; ++Index)
	{
		FPlanetTerrainNextPendingBuild Candidate;
		Candidate.Id = FPlanetTerrainNextNodeId(0, 1, Index & 1u, (Index >> 1u) & 1u);
		Candidate.Priority = 100.0 - static_cast<double>(Index);
		Candidates.Add(Candidate);
	}

	Planner.AppendPredictivePrefetch(
		Candidates,
		[](const FPlanetTerrainNextNodeId& Node)
		{
			return FPlanetTerrainNextTileId::Make(42, 1, 1, 7, Node);
		},
		10,
		3,
		Requests);

	TestEqual(TEXT("predictive requests are capped"), Requests.Num(), 3);
	TSet<FPlanetTerrainNextTileId> Seen;
	for (const FPlanetTerrainNextTileRequest& Request : Requests)
	{
		TestEqual(TEXT("predictive class"), Request.Class, EPlanetTerrainNextRequestClass::Prefetch);
		TestFalse(TEXT("predictive requests are deduplicated"), Seen.Contains(Request.TileId));
		Seen.Add(Request.TileId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR12GuardFrustumDoesNotSplitTest,
	"Singularity.PlanetTerrainNext.R12.GuardFrustumDoesNotSplit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR12GuardFrustumDoesNotSplitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextQuadtree Tree;
	TArray<double> Errors;
	Errors.Init(100.0, 8);
	Tree.Initialize(1000.0, -1.0, 1.0, Errors);

	FPlanetTerrainNextView View;
	View.CameraM = FVector3d(0.0, 0.0, 1200.0);
	View.Forward = FVector3d(0.0, 0.0, -1.0);
	View.HalfDiagonalFovRad = 0.1;

	const FPlanetTerrainNextNodeId FutureChild(4, 1, 0, 0);
	TestTrue(TEXT("guard query sees a future child"), Tree.IsNodeInPreloadFrustum(FutureChild, View, 0.6));

	FPlanetTerrainNextLodSettings Settings;
	Settings.MaxLevel = 1;
	Settings.MaxNodes = 6;
	TArray<FPlanetTerrainNextNodeId> Released;
	Tree.Update(View, Settings, [](const FPlanetTerrainNextNodeId&) { return true; }, Released);
	TestEqual(TEXT("guard query did not allocate quadtree nodes"), Tree.NumNodes(), 6);
	return true;
}

#endif
