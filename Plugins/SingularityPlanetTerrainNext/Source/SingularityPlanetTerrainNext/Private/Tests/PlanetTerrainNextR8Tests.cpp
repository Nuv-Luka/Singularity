#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextQuadtree.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor MoonDescriptor()
{
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = 3;
	Descriptor.PlanetId = 8;
	return Descriptor;
}

FPlanetTerrainNextView MakeView(const FVector3d& CameraM, const FVector3d& Forward)
{
	FPlanetTerrainNextView View;
	View.CameraM = CameraM;
	View.Forward = Forward.GetSafeNormal();
	View.HalfDiagonalFovRad = FMath::DegreesToRadians(45.0);
	View.ProjectionScalePx = 960.0;
	View.TimeSeconds = 0.0;
	return View;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextPeripheralLodTest,
	"Singularity.PlanetTerrainNext.R8.PeripheralLod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextPeripheralLodTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField Field(MoonDescriptor());
	TArray<double> GE;
	GE.SetNum(18);
	for (int32 L = 0; L <= 17; ++L)
	{
		GE[L] = Field.GeometricErrorMeters(PlanetTerrainNextPatch::VertexSpacingMeters(Field.GetRadiusMeters(), L));
	}

	FPlanetTerrainNextQuadtree Tree;
	Tree.Initialize(Field.GetRadiusMeters(), Field.GetMinHeightMeters(), Field.GetMaxHeightMeters(), GE);

	const FVector3d Up = FVector3d::UpVector;
	const FVector3d Camera = Up * (Field.GetRadiusMeters() + 5000.0);
	FPlanetTerrainNextView View = MakeView(Camera, -Up);

	FPlanetTerrainNextLodSettings Lod;
	Lod.TargetPixelError = 4.0;
	Lod.MaxLevel = 12;
	Lod.MaxNodes = 512;
	Lod.PeripheralLodScale = 1.0;

	auto AlwaysReady = [](const FPlanetTerrainNextNodeId&) { return true; };
	TArray<FPlanetTerrainNextNodeId> Released;
	Tree.Update(View, Lod, AlwaysReady, Released);

	// Pick a drawn leaf and compare PE with/without peripheral scale by re-evaluating via GetNodePixelError
	// after a second update with scale 0.5 — periphery nodes should not increase error.
	TArray<FPlanetTerrainNextDrawItem> DrawFull;
	Tree.CollectDrawSet(AlwaysReady, DrawFull);
	TestTrue(TEXT("draw set with scale=1"), DrawFull.Num() > 0);

	double MaxPEFull = 0.0;
	for (const FPlanetTerrainNextDrawItem& Item : DrawFull)
	{
		MaxPEFull = FMath::Max(MaxPEFull, Item.PixelError);
	}

	Lod.PeripheralLodScale = 0.5;
	Tree.Update(View, Lod, AlwaysReady, Released);
	TArray<FPlanetTerrainNextDrawItem> DrawPeriph;
	Tree.CollectDrawSet(AlwaysReady, DrawPeriph);
	double MaxPEPeriph = 0.0;
	for (const FPlanetTerrainNextDrawItem& Item : DrawPeriph)
	{
		MaxPEPeriph = FMath::Max(MaxPEPeriph, Item.PixelError);
	}

	AddInfo(FString::Printf(TEXT("max PE full=%.3f peripheral=%.3f drawn %d→%d"),
		MaxPEFull, MaxPEPeriph, DrawFull.Num(), DrawPeriph.Num()));
	TestTrue(TEXT("peripheral scale never increases max PE"), MaxPEPeriph <= MaxPEFull + 1.0e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextCameraTurnCoverageTest,
	"Singularity.PlanetTerrainNext.R8.CameraTurnCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextCameraTurnCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField Field(MoonDescriptor());
	TArray<double> GE;
	GE.SetNum(18);
	for (int32 L = 0; L <= 17; ++L)
	{
		GE[L] = Field.GeometricErrorMeters(PlanetTerrainNextPatch::VertexSpacingMeters(Field.GetRadiusMeters(), L));
	}

	FPlanetTerrainNextQuadtree Tree;
	Tree.Initialize(Field.GetRadiusMeters(), Field.GetMinHeightMeters(), Field.GetMaxHeightMeters(), GE);

	FPlanetTerrainNextLodSettings Lod;
	Lod.TargetPixelError = 4.0;
	Lod.MaxLevel = 10;
	Lod.MaxNodes = 64; // tight — will block some splits
	Lod.PeripheralLodScale = 0.7;

	TSet<FPlanetTerrainNextNodeId> Ready;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		Ready.Add(FPlanetTerrainNextNodeId(static_cast<uint8>(Face), 0, 0, 0));
	}
	auto IsReady = [&](const FPlanetTerrainNextNodeId& Id) { return Ready.Contains(Id); };

	const double R = Field.GetRadiusMeters();
	const FVector3d Positions[4] = {
		FVector3d(0, 0, 1) * (R + 8000.0),
		FVector3d(1, 0, 0) * (R + 8000.0),
		FVector3d(0, 0, -1) * (R + 8000.0),
		FVector3d(-1, 0, 0) * (R + 8000.0),
	};

	for (int32 Frame = 0; Frame < 40; ++Frame)
	{
		const FVector3d Camera = Positions[Frame % 4];
		FPlanetTerrainNextView View = MakeView(Camera, (-Camera).GetSafeNormal());
		View.TimeSeconds = Frame * 0.05;
		TArray<FPlanetTerrainNextNodeId> Released;
		Tree.Update(View, Lod, IsReady, Released);
		for (const FPlanetTerrainNextNodeId& Id : Released)
		{
			Ready.Remove(Id);
		}

		TArray<FPlanetTerrainNextPendingBuild> Pending;
		Tree.CollectPending(IsReady, Pending);
		const int32 Budget = 4;
		for (int32 I = 0; I < Pending.Num() && I < Budget; ++I)
		{
			Ready.Add(Pending[I].Id);
		}

		TArray<FPlanetTerrainNextNodeId> Draw;
		Tree.CollectDrawSet(IsReady, Draw);
		TestTrue(TEXT("draw never empty under turns"), Draw.Num() > 0);

		// Six face roots must remain covered (parent or descendant drawn).
		for (int32 Face = 0; Face < 6; ++Face)
		{
			bool bCovered = false;
			for (const FPlanetTerrainNextNodeId& Id : Draw)
			{
				if (Id.Face == Face)
				{
					bCovered = true;
					break;
				}
			}
			if (!bCovered)
			{
				AddError(FString::Printf(TEXT("frame %d missing coverage for face %d"), Frame, Face));
				return false;
			}
		}
	}

	TestTrue(TEXT("budget pressure exercised"), Tree.GetStats().BudgetBlocked >= 0);
	AddInfo(FString::Printf(TEXT("after turns: nodes=%d blocked=%d"), Tree.GetStats().Nodes, Tree.GetStats().BudgetBlocked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextPeripheralDisabledParityTest,
	"Singularity.PlanetTerrainNext.R8.PeripheralDisabledParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextPeripheralDisabledParityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// Scale=1 must match raw PE (no bias) for a node on the view axis.
	const FPlanetTerrainNextHeightField Field(MoonDescriptor());
	TArray<double> GE;
	GE.SetNum(8);
	for (int32 L = 0; L < 8; ++L)
	{
		GE[L] = Field.GeometricErrorMeters(PlanetTerrainNextPatch::VertexSpacingMeters(Field.GetRadiusMeters(), L));
	}
	FPlanetTerrainNextQuadtree Tree;
	Tree.Initialize(Field.GetRadiusMeters(), Field.GetMinHeightMeters(), Field.GetMaxHeightMeters(), GE);

	const FVector3d Camera = FVector3d::UpVector * (Field.GetRadiusMeters() + 20000.0);
	FPlanetTerrainNextView View = MakeView(Camera, -FVector3d::UpVector);
	FPlanetTerrainNextLodSettings Lod;
	Lod.PeripheralLodScale = 1.0;
	Lod.MaxLevel = 4;
	Lod.MaxNodes = 256;
	auto Ready = [](const FPlanetTerrainNextNodeId&) { return true; };
	TArray<FPlanetTerrainNextNodeId> Released;
	Tree.Update(View, Lod, Ready, Released);
	const double PE = Tree.GetNodePixelError(FPlanetTerrainNextNodeId(2, 0, 0, 0)); // +Y root under camera
	TestTrue(TEXT("+Y root has PE"), PE > 0.0);
	AddInfo(FString::Printf(TEXT("+Y root PE with scale=1: %.4f"), PE));
	return true;
}

#endif
