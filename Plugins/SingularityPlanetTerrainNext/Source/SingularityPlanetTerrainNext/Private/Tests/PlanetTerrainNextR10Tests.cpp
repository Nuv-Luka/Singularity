#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextDiag.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor MoonDesc()
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
	const FPlanetTerrainNextDescriptor Desc = MoonDesc();

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
	const FPlanetTerrainNextHeightField Field(MoonDesc());

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

#endif
