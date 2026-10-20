#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextDiag.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextLodFade.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor MoonDescR10()
{
	FPlanetTerrainNextDescriptor D;
	D.Seed = 3;
	D.PlanetId = 10;
	D.GeneratorVersion = 1;
	return D;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR101FlatGeometryTest,
	"Singularity.PlanetTerrainNext.R10.FlatGeometryL0L1L2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR101FlatGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextDescriptor Desc = MoonDescR10();

	TestEqual(TEXT("index buffer version"), GetIndexBufferVersion(), 3);
	TestEqual(TEXT("grid tri count"), Indices().Num() / 3, GridN * GridN * 2 + 4 * GridN * 2);

	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (const int32 Level : {0, 1, 2})
		{
			const uint32 Edge = 1u << Level;
			for (uint32 Y = 0; Y < Edge; ++Y)
			{
				for (uint32 X = 0; X < Edge; ++X)
				{
					const FPlanetTerrainNextNodeId Id(static_cast<uint8>(Face), static_cast<uint8>(Level), X, Y);
					FPlanetTerrainNextPatchData Patch;
					BuildFlat(Desc.RadiusMeters, Id, 0.0, Patch);
					TestEqual(TEXT("vertex count"), Patch.VerticesCm.Num(), VertexCount);

					const FPlanetTerrainNextPatchValidation V = PlanetTerrainNextDiag::ValidatePatch(Patch, false);
					TestTrue(
						FString::Printf(TEXT("flat F%d L%d (%u,%u) ok inv=%d deg=%d inward=%d nan=%d"),
							Face, Level, X, Y, V.InvalidIndexCount, V.DegenerateTriangleCount,
							V.InwardFacingGridCount, V.NonFiniteVertexCount),
						V.bOk);
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR101HeightGeometryTest,
	"Singularity.PlanetTerrainNext.R10.HeightGeometryL0L1L2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR101HeightGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDescR10());

	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (const int32 Level : {0, 1, 2})
		{
			const uint32 Mid = (1u << Level) / 2;
			FPlanetTerrainNextPatchData Patch;
			Build(Field, FPlanetTerrainNextNodeId(static_cast<uint8>(Face), static_cast<uint8>(Level), Mid, Mid), 0.0, Patch);
			const FPlanetTerrainNextPatchValidation V = PlanetTerrainNextDiag::ValidatePatch(Patch, false);
			TestTrue(
				FString::Printf(TEXT("height F%d L%d ok inv=%d deg=%d inward=%d nan=%d"),
					Face, Level, V.InvalidIndexCount, V.DegenerateTriangleCount,
					V.InwardFacingGridCount, V.NonFiniteVertexCount),
				V.bOk);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR101DiagCollectNodesTest,
	"Singularity.PlanetTerrainNext.R10.DiagCollectNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR101DiagCollectNodesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextDiagSettings S;
	S.Mode = EPlanetTerrainNextDiagMode::SinglePatch;
	S.Level = 1;
	S.Face = 3;
	TArray<FPlanetTerrainNextNodeId> Nodes;
	PlanetTerrainNextDiag::CollectNodes(S, Nodes);
	TestEqual(TEXT("single patch count"), Nodes.Num(), 1);
	TestEqual(TEXT("single face"), static_cast<int32>(Nodes[0].Face), 3);
	TestEqual(TEXT("single level"), static_cast<int32>(Nodes[0].Level), 1);

	S.Mode = EPlanetTerrainNextDiagMode::SixFaces;
	S.Level = 1;
	PlanetTerrainNextDiag::CollectNodes(S, Nodes);
	TestEqual(TEXT("six faces L1 = 6*4"), Nodes.Num(), 24);

	S.Level = 2;
	PlanetTerrainNextDiag::CollectNodes(S, Nodes);
	TestEqual(TEXT("six faces L2 = 6*16"), Nodes.Num(), 96);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR102LodFadeComplementTest,
	"Singularity.PlanetTerrainNext.R10.LodFadeComplement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR102LodFadeComplementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextNodeId Parent(1, 0, 0, 0);
	TSet<FPlanetTerrainNextNodeId> Desired;
	for (int32 C = 0; C < 4; ++C)
	{
		Desired.Add(Parent.Child(C));
	}
	TestTrue(TEXT("desired has children"), PlanetTerrainNextLodFade::DesiredHasAllChildren(Desired, Parent));

	FPlanetTerrainNextLodFade Fade;
	Fade.Parent = Parent;
	Fade.State = EPlanetTerrainNextLodFadeState::FadingToChildren;
	Fade.Progress = 0.0f;
	Fade.DurationSeconds = 0.5f;
	Fade.Direction = 1;

	TArray<FPlanetTerrainNextLodFade> Fades;
	Fades.Add(Fade);
	TSet<FPlanetTerrainNextNodeId> Render;
	PlanetTerrainNextLodFade::BuildRenderSet(Desired, Fades, Render);
	TestTrue(TEXT("render keeps parent during fade"), Render.Contains(Parent));
	TestEqual(TEXT("render has parent+4 children"), Render.Num(), 5);

	TestFalse(TEXT("not complete at start"), PlanetTerrainNextLodFade::TickFade(Fades[0], 0.1f));
	TestTrue(TEXT("progress advanced"), Fades[0].Progress > 0.15f && Fades[0].Progress < 0.25f);

	// Finish fade
	while (!PlanetTerrainNextLodFade::TickFade(Fades[0], 0.1f))
	{
	}
	TestEqual(TEXT("progress done"), Fades[0].Progress, 1.0f);
	TestEqual(TEXT("state stable"), static_cast<int32>(Fades[0].State), static_cast<int32>(EPlanetTerrainNextLodFadeState::Stable));

	// Reverse mid progress
	FPlanetTerrainNextLodFade Rev;
	Rev.Parent = Parent;
	Rev.State = EPlanetTerrainNextLodFadeState::FadingToChildren;
	Rev.Progress = 0.6f;
	Rev.DurationSeconds = 0.4f;
	Rev.Direction = -1;
	Rev.State = EPlanetTerrainNextLodFadeState::FadingToParent;
	PlanetTerrainNextLodFade::TickFade(Rev, 0.1f);
	TestTrue(TEXT("reverse decreases"), Rev.Progress < 0.6f);
	return true;
}

#endif
