#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextLodFade.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextQuadtree.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR11ComplementaryMaskTest,
	"Singularity.PlanetTerrainNext.R11.ComplementaryDitherCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR11ComplementaryMaskTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const float Progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		for (const float Noise : {0.0f, 0.13f, 0.37f, 0.5f, 0.73f, 0.99f})
		{
			const bool bParent = PlanetTerrainNextLodFade::EvaluateComplementaryDither(
				Progress, Noise, false);
			const bool bChild = PlanetTerrainNextLodFade::EvaluateComplementaryDither(
				Progress, Noise, true);
			TestTrue(
				FString::Printf(TEXT("progress %.2f noise %.2f has exactly one owner"), Progress, Noise),
				bParent != bChild);
		}
	}

	for (const float Noise : {0.0f, 0.5f, 0.99f})
	{
		TestTrue(TEXT("parent owns transition at progress 0"),
			PlanetTerrainNextLodFade::EvaluateComplementaryDither(0.0f, Noise, false));
		TestTrue(TEXT("child owns transition at progress 1"),
			PlanetTerrainNextLodFade::EvaluateComplementaryDither(1.0f, Noise, true));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR11MergeResidencyTest,
	"Singularity.PlanetTerrainNext.R11.MergeKeepsBothSidesResident",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR11MergeResidencyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextNodeId Parent(2, 3, 4, 5);
	TSet<FPlanetTerrainNextNodeId> Desired;
	Desired.Add(Parent);

	FPlanetTerrainNextLodFade Fade;
	Fade.Parent = Parent;
	Fade.State = EPlanetTerrainNextLodFadeState::FadingToParent;
	Fade.Progress = 1.0f;
	Fade.Direction = -1;
	Fade.DurationSeconds = 0.4f;

	TArray<FPlanetTerrainNextLodFade> Fades;
	Fades.Add(Fade);
	TSet<FPlanetTerrainNextNodeId> Render;
	PlanetTerrainNextLodFade::BuildRenderSet(Desired, Fades, Render);
	TestEqual(TEXT("merge render set keeps parent plus four children"), Render.Num(), 5);
	TestTrue(TEXT("merge render set keeps parent"), Render.Contains(Parent));
	for (int32 Child = 0; Child < 4; ++Child)
	{
		TestTrue(FString::Printf(TEXT("child %d remains resident during reverse fade"), Child),
			Render.Contains(Parent.Child(Child)));
	}

	while (!PlanetTerrainNextLodFade::TickFade(Fades[0], 0.1f))
	{
	}
	TestEqual(TEXT("merge reaches parent endpoint"), Fades[0].Progress, 0.0f);
	TestEqual(TEXT("merge endpoint is stable"),
		static_cast<int32>(Fades[0].State),
		static_cast<int32>(EPlanetTerrainNextLodFadeState::Stable));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR11EdgeBandTest,
	"Singularity.PlanetTerrainNext.R11.EdgeBandConstrainsMixedLodBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR11EdgeBandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = 11;
	Descriptor.PlanetId = 11;
	Descriptor.GeneratorVersion = 1;
	const FPlanetTerrainNextHeightField Field(Descriptor);
	FPlanetTerrainNextPatchData Patch;
	PlanetTerrainNextPatch::Build(Field, FPlanetTerrainNextNodeId(0, 3, 3, 4), 0.0, Patch);

	double MaxEdgeGapCm = 0.0;
	for (int32 J = 0; J <= PlanetTerrainNextPatch::GridN; ++J)
	{
		for (int32 I = 0; I <= PlanetTerrainNextPatch::GridN; ++I)
		{
			if (I != 0 && I != PlanetTerrainNextPatch::GridN
				&& J != 0 && J != PlanetTerrainNextPatch::GridN)
			{
				continue;
			}
			const int32 Index = J * PlanetTerrainNextPatch::Row + I;
			MaxEdgeGapCm = FMath::Max(
				MaxEdgeGapCm,
				static_cast<double>((Patch.VerticesCm[Index] - Patch.CoarseVerticesCm[Index]).Size()));
		}
	}
	AddInfo(FString::Printf(TEXT("edge band max fine/coarse gap %.6f cm"), MaxEdgeGapCm));
	TestTrue(TEXT("levelled child edge is parent-compatible"), MaxEdgeGapCm < 0.01);
	TestEqual(TEXT("edge-band patch has canonical topology"), Patch.VerticesCm.Num(), PlanetTerrainNextPatch::VertexCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR11SpatialFadeTest,
	"Singularity.PlanetTerrainNext.R11.SpatialFade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR11SpatialFadeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextLodFade Fade;
	Fade.State = EPlanetTerrainNextLodFadeState::FadingToChildren;
	Fade.Direction = 1;
	Fade.Progress = 0.0f;
	Fade.bSpatial = true;
	Fade.AnchorDistanceM = 100.0;
	Fade.DistanceSpanM = 20.0;
	const FVector3d Camera(0.0, 0.0, 0.0);
	PlanetTerrainNextLodFade::AdvanceSpatialFade(Fade, 100.0, Camera);
	TestTrue(TEXT("stationary camera does not advance fade"), FMath::IsNearlyEqual(Fade.Progress, 0.0f));
	PlanetTerrainNextLodFade::AdvanceSpatialFade(Fade, 90.0, FVector3d(10.0, 0.0, 0.0));
	const float HalfProgress = Fade.Progress;
	TestTrue(TEXT("camera movement advances spatial fade"), HalfProgress > 0.0f && HalfProgress < 1.0f);
	PlanetTerrainNextLodFade::ReverseSpatialFade(Fade, 90.0, -1);
	PlanetTerrainNextLodFade::AdvanceSpatialFade(Fade, 95.0, FVector3d(15.0, 0.0, 0.0));
	TestTrue(TEXT("camera reversal preserves partial progress"), Fade.Progress > 0.0f && Fade.Progress < 1.0f);

	FPlanetTerrainNextLodFade A = Fade;
	FPlanetTerrainNextLodFade B = Fade;
	A.State = B.State = EPlanetTerrainNextLodFadeState::FadingToChildren;
	A.Direction = B.Direction = 1;
	A.Progress = B.Progress = 0.0f;
	A.AnchorDistanceM = B.AnchorDistanceM = 100.0;
	A.DistanceSpanM = B.DistanceSpanM = 20.0;
	PlanetTerrainNextLodFade::AdvanceSpatialFade(A, 80.0, FVector3d(20.0, 0.0, 0.0));
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		PlanetTerrainNextLodFade::AdvanceSpatialFade(B, 95.0 - Frame * 5.0, FVector3d(5.0 + Frame * 5.0, 0.0, 0.0));
	}
	TestTrue(TEXT("equal camera movement gives frame-rate independent progress"), FMath::IsNearlyEqual(A.Progress, B.Progress, 1.0e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR11NeighborStitchTest,
	"Singularity.PlanetTerrainNext.R11.NeighborStitchAndSkirtAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR11NeighborStitchTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = 17;
	Descriptor.PlanetId = 17;
	const FPlanetTerrainNextHeightField Field(Descriptor);
	const FPlanetTerrainNextNodeId FineId(0, 3, 3, 3);
	FPlanetTerrainNextPatchData Patch;
	PlanetTerrainNextPatch::Build(Field, FineId, 100.0, Patch);
	FPlanetTerrainNextEdgeAdjacency Adjacency[4];
	Adjacency[0].bHasNeighbor = true;
	Adjacency[0].bRequiresStitching = true;
	Adjacency[0].NeighborId = FPlanetTerrainNextNodeId(0, 2, 1, 0);
	Adjacency[0].NeighborEdge = EPlanetTerrainNextPatchEdge::Top;
	PlanetTerrainNextPatch::ApplyNeighborEdgeStitching(Field, Patch, Adjacency);

	for (int32 K = 0; K <= PlanetTerrainNextPatch::GridN; ++K)
	{
		const int32 Top = K;
		const int32 Bottom = PlanetTerrainNextPatch::GridVertexCount + K;
		TestTrue(FString::Printf(TEXT("fine/coarse stitched edge sample %d matches"), K),
			(Patch.VerticesCm[Top] - Patch.CoarseVerticesCm[Top]).Size() < 0.01f);
		const FVector3d Dir = PlanetTerrainNextPatch::VertexDirection(FineId, K, 0);
		const FVector3d ExpectedBottom = FVector3d(Patch.VerticesCm[Top]) / PlanetTerrainNext::CmPerMeter - Dir * Patch.SkirtDepthM;
		const FVector3d ActualBottom = FVector3d(Patch.VerticesCm[Bottom]) / PlanetTerrainNext::CmPerMeter;
		TestTrue(FString::Printf(TEXT("skirt sample %d starts at rendered edge"), K), (ExpectedBottom - ActualBottom).Size() < 0.01);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR11LodQueueRampTest,
	"Singularity.PlanetTerrainNext.R11.LodQueueDistanceRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR11LodQueueRampTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextLodSettings Settings;
	Settings.MaxLevel = 17;
	Settings.QueueDistanceScale = 10.0;
	TestTrue(TEXT("L0 queue multiplier is 1x"), FMath::IsNearlyEqual(Settings.QueueDistanceScaleForLevel(0), 1.0));
	TestTrue(TEXT("L17 queue multiplier is 10x"), FMath::IsNearlyEqual(Settings.QueueDistanceScaleForLevel(17), 10.0));
	TestTrue(TEXT("L8 queue multiplier ramps between endpoints"), FMath::IsNearlyEqual(Settings.QueueDistanceScaleForLevel(8), 1.0 + 9.0 * 8.0 / 17.0));
	TestTrue(TEXT("L17 queue threshold is one tenth of base"), FMath::IsNearlyEqual(Settings.QueueTargetPixelErrorForLevel(17), Settings.TargetPixelError / 10.0));
	return true;
}

#endif
