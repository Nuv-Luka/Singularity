#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextLodFade.h"
#include "PlanetTerrainNextPatchMesh.h"

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

#endif
