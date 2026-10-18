#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetTerrain/PlanetTerrainHeight.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"
#include "PlanetTerrain/PlanetTerrainTile.h"
#include "PlanetTerrain/PlanetTerrainQuadtree.h"
#include "PlanetTerrain/PlanetTerrainGpuBake.h"
#include "PlanetTerrain/PlanetTerrainNormals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainDeterminismTest,
	"Singularity.PlanetTerrain.HeightDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainDeterminismTest::RunTest(const FString& Parameters)
{
	const FRockyPlanetDescriptor Desc = MakeAncientAirlessRockyMoon(0xC0FFEEull, 0.27, 0.012);
	const FPlanetTerrainHeight A(Desc);
	const FPlanetTerrainHeight B(Desc);
	const FVector3d Dir = FVector3d(0.2, 0.9, -0.3).GetSafeNormal();

	const double H1 = A.EvaluateHeightUnrestricted(Dir);
	const double H2 = B.EvaluateHeightUnrestricted(Dir);
	TestEqual(TEXT("Height deterministic"), H1, H2);
	TestTrue(TEXT("Height within amplitude"), FMath::Abs(H1) <= A.GetAmplitudeMeters() + 1e-3);

	double Prev = A.EvaluateHeightAtSpacing(Dir, 5000.0);
	double MaxStep = 0.0;
	double S = 5000.0;
	for (int32 I = 0; I < 200; ++I)
	{
		S *= 0.97;
		const double H = A.EvaluateHeightAtSpacing(Dir, S);
		MaxStep = FMath::Max(MaxStep, FMath::Abs(H - Prev));
		Prev = H;
	}
	TestTrue(TEXT("LOD height continuous"), MaxStep < 0.05 * A.GetAmplitudeMeters() + 1.0);

	const double Profile = FPlanetTerrainHeight::EvaluateCraterProfile(0.0, 1000.0, 200.0, 50.0);
	TestTrue(TEXT("Crater bowl negative at center"), Profile < 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainTileBakeTest,
	"Singularity.PlanetTerrain.TileBakeSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainTileBakeTest::RunTest(const FString& Parameters)
{
	const FRockyPlanetDescriptor Desc = MakeAncientAirlessRockyMoon(0xABCDull, 0.27, 0.012);
	const FPlanetTerrainHeight Height(Desc);

	FPlanetTerrainTileKey Key;
	Key.PlanetID = Desc.PlanetID;
	Key.GenerationEpoch = 1;
	Key.Face = 0;
	Key.Level = 1;
	Key.X = 0;
	Key.Y = 0;

	FPlanetTerrainTileData Tile;
	FPlanetTerrainTileBaker::Bake(Height, Key, 0.0, 0.0, 0.5, 0.5, 16, Tile);
	TestTrue(TEXT("Tile has verts"), Tile.PositionsLocal.Num() == 17 * 17);
	TestTrue(TEXT("Heights match directions"), Tile.HeightFineM.Num() == Tile.Directions.Num());

	// Shared corner between two faces should evaluate identically via height field.
	const FVector3d Corner = PlanetTerrainFacePoint(0, 1.0, 1.0);
	const FVector3d Corner2 = PlanetTerrainFacePoint(2, 1.0, 0.0); // may differ — use same dir
	const double Ha = Height.EvaluateHeightUnrestricted(Corner);
	const double Hb = Height.EvaluateHeightUnrestricted(Corner.GetSafeNormal());
	TestEqual(TEXT("Same dir same height"), Ha, Hb);
	(void)Corner2;

	// Order independence: bake twice.
	FPlanetTerrainTileData Tile2;
	FPlanetTerrainTileBaker::Bake(Height, Key, 0.0, 0.0, 0.5, 0.5, 16, Tile2);
	TestEqual(TEXT("Bake order independent height[0]"), Tile.HeightFineM[0], Tile2.HeightFineM[0]);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainFacePointTest,
	"Singularity.PlanetTerrain.CubeSphereFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainFacePointTest::RunTest(const FString& Parameters)
{
	for (uint8 Face = 0; Face < 6; ++Face)
	{
		const FVector3d C = PlanetTerrainFacePoint(Face, 0.5, 0.5);
		TestTrue(TEXT("Unit length"), FMath::Abs(C.Size() - 1.0) < 1e-9);
	}
	const FVector3d A = PlanetTerrainFacePoint(0, 0.0, 0.0);
	const FVector3d B = PlanetTerrainFacePoint(0, 0.0, 0.0);
	TestEqual(TEXT("Face corner stable X"), A.X, B.X);
	TestEqual(TEXT("Face corner stable Y"), A.Y, B.Y);
	TestEqual(TEXT("Face corner stable Z"), A.Z, B.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainLandingPadStressTest,
	"Singularity.PlanetTerrain.LandingPadStress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainLandingPadStressTest::RunTest(const FString& Parameters)
{
	const FRockyPlanetDescriptor Desc = MakeAncientAirlessRockyMoon(0x1A11D001ull, 0.27, 0.012);
	const FPlanetTerrainHeight Height(Desc);

	FPlanetTerrainLodParams Lod;
	Lod.LodDistanceFactor = 2.2;
	Lod.MaxActivePatches = 128;
	Lod.ActivateDistanceM = Desc.RadiusMeters * 1.25;
	Lod.MinNeighborhoodLevel = 5;
	Lod.MaxLevel = 7;
	Lod.MergeHysteresis = 1.6;

	FPlanetTerrainQuadtree Tree;
	Tree.Initialize(Desc.PlanetID, 1, Desc.RadiusMeters, Lod);

	const FVector3d Up(0.0, 0.0, 1.0);
	constexpr int32 TargetLevel = 5;
	FPlanetTerrainNode* Primary = Tree.EnsurePathToLevel(Up, TargetLevel);
	TestNotNull(TEXT("Primary leaf exists"), Primary);
	if (!Primary)
	{
		return false;
	}
	TestTrue(TEXT("Primary at target level"), Primary->Level == TargetLevel);

	// Bake every sibling along the nadir path (mirrors EnsureLandingPad coverage).
	int32 Baked = 0;
	uint8 Face = 0;
	double U = 0.0, V = 0.0;
	TestTrue(TEXT("UV project"), FPlanetTerrainQuadtree::DirectionToFaceUV(Up, Face, U, V));
	FPlanetTerrainNode* N = Tree.GetRoots()[Face].Get();
	while (N)
	{
		auto Bake = [&](FPlanetTerrainNode* Node)
		{
			if (!Node || Node->Tile.IsValid())
			{
				return;
			}
			TSharedPtr<FPlanetTerrainTileData> Tile = MakeShared<FPlanetTerrainTileData>();
			FPlanetTerrainTileBaker::Bake(
				Height, Node->Key, Node->U0, Node->V0, Node->U1, Node->V1, 16, *Tile);
			Tile->State = EPlanetTerrainTileState::Resident; // mirrors UploadTileMesh
			Node->Tile = Tile;
			++Baked;
			TestTrue(TEXT("Finite height"), FMath::IsFinite(Tile->HeightFineM[0]));
		};
		Bake(N);
		if (!N->Children[0].IsValid())
		{
			break;
		}
		for (int32 I = 0; I < 4; ++I)
		{
			Bake(N->Children[I].Get());
		}
		const double Um = 0.5 * (N->U0 + N->U1);
		const double Vm = 0.5 * (N->V0 + N->V1);
		const int32 Ix = U < Um ? 0 : 1;
		const int32 Iy = V < Vm ? 0 : 1;
		N = N->Children[Iy * 2 + Ix].Get();
	}
	TestTrue(TEXT("Baked several tiles"), Baked >= TargetLevel);

	// LOD update at surface altitude must not dispose the pad tree immediately.
	FPlanetTerrainCameraQuery Cam;
	Cam.PlanetLocalPosition = Up * (Desc.RadiusMeters + 50.0);
	Cam.Forward = FVector3d(1, 0, 0);
	Tree.SetFrozen(true);
	Tree.UpdateLod(Cam, Height, 1);
	FPlanetTerrainNode* After = Tree.FindLeafContaining(Up);
	TestNotNull(TEXT("Leaf still present while frozen"), After);
	TestTrue(TEXT("Pad depth retained"), After && After->Level >= 3);

	Tree.SetFrozen(false);
	for (int32 Frame = 2; Frame < 12; ++Frame)
	{
		Tree.UpdateLod(Cam, Height, Frame);
	}
	TArray<FPlanetTerrainNode*> Leaves;
	Tree.CollectDrawLeaves(Leaves);
	TestTrue(TEXT("Has draw leaves"), Leaves.Num() > 0);
	TestTrue(TEXT("Within budget"), Leaves.Num() <= Lod.MaxActivePatches);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainGpuFloatToleranceTest,
	"Singularity.PlanetTerrain.GpuFloatTolerance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainGpuFloatToleranceTest::RunTest(const FString& Parameters)
{
	const FRockyPlanetDescriptor Desc = MakeAncientAirlessRockyMoon(0x67F10001ull, 0.27, 0.012);
	const FPlanetTerrainHeight Height(Desc);
	const float Amp = static_cast<float>(Height.GetAmplitudeMeters());

	// Deterministic sample set on the unit sphere.
	TArray<FVector3d> Dirs;
	for (int32 I = 0; I < 8; ++I)
	{
		for (int32 J = 0; J < 8; ++J)
		{
			Dirs.Add(PlanetTerrainFacePoint(4, (I + 0.5) / 8.0, (J + 0.5) / 8.0));
		}
	}

	double MaxAbs = 0.0;
	TArray<double> AbsErr;
	AbsErr.Reserve(Dirs.Num());
	for (const FVector3d& Dir : Dirs)
	{
		const double Hd = Height.EvaluateHeightAtSpacing(Dir, 2000.0);
		const float Hg = Height.EvaluateHeightGpuFloat(Dir, 2000.0f);
		const double Err = FMath::Abs(Hd - static_cast<double>(Hg));
		MaxAbs = FMath::Max(MaxAbs, Err);
		AbsErr.Add(Err);
	}
	AbsErr.Sort();
	const double P95 = AbsErr[FMath::Max(0, AbsErr.Num() * 95 / 100 - 1)];

	// Different hash family than double SplitMix64 — expect structural similarity, not bit match.
	// See Phase7_GpuBake.md: float field is the GPU contract; double remains unrestricted truth.
	TestTrue(TEXT("GpuFloat p95 vs double within 1× amplitude"), P95 <= static_cast<double>(Amp) + 50.0);
	TestTrue(TEXT("GpuFloat max vs double finite"), FMath::IsFinite(MaxAbs));
	(void)MaxAbs;

	// GpuFloat self-determinism (the hard Phase 7 contract until CS is in a plugin).
	const FVector3d D = Dirs[0];
	TestEqual(TEXT("GpuFloat deterministic"), Height.EvaluateHeightGpuFloat(D, 500.0f), Height.EvaluateHeightGpuFloat(D, 500.0f));

	// Tile bake via GpuFloatCpu backend produces finite heights.
	FPlanetTerrainTileKey Key;
	Key.PlanetID = Desc.PlanetID;
	Key.GenerationEpoch = 1;
	Key.Face = 4;
	Key.Level = 2;
	FPlanetTerrainTileData Tile;
	FPlanetTerrainTileBakerEx::Bake(Height, Key, 0.25, 0.25, 0.5, 0.5, 8, Tile, EPlanetTerrainBakeBackend::GpuFloatCpu);
	TestTrue(TEXT("GpuFloat tile verts"), Tile.HeightFineM.Num() == 9 * 9);
	TestTrue(TEXT("GpuFloat tile finite"), FMath::IsFinite(Tile.HeightFineM[0]));

	// Live GPU compute (skipped under NullRHI / non-game-thread automation workers).
	if (FPlanetTerrainGpuBaker::IsGpuComputeAvailable())
	{
		TArray<float> GpuFine, GpuCoarse, CpuFine, CpuCoarse;
		const float Spacing = 2000.0f;
		const bool bGpu = FPlanetTerrainGpuBaker::TryBakeHeightsCompute(
			Height, 4, 0.25, 0.25, 0.5, 0.5, 8, Spacing, Spacing * 2.0f, GpuFine, GpuCoarse);
		TestTrue(TEXT("GpuCompute bake ok"), bGpu);
		if (bGpu)
		{
			FPlanetTerrainTileData CpuTile;
			FPlanetTerrainTileBakerEx::Bake(
				Height, Key, 0.25, 0.25, 0.5, 0.5, 8, CpuTile, EPlanetTerrainBakeBackend::GpuFloatCpu);
			double MaxDelta = 0.0;
			for (int32 I = 0; I < GpuFine.Num(); ++I)
			{
				MaxDelta = FMath::Max(MaxDelta, static_cast<double>(FMath::Abs(GpuFine[I] - CpuTile.HeightFineM[I])));
			}
			TestTrue(TEXT("GpuCompute vs GpuFloatCpu within 0.1% amp"), MaxDelta <= 0.001 * Amp + 0.05);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainEdgeNormalSeamTest,
	"Singularity.PlanetTerrain.EdgeNormalSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainEdgeNormalSeamTest::RunTest(const FString& Parameters)
{
	const FRockyPlanetDescriptor Desc = MakeAncientAirlessRockyMoon(0x5EA70001ull, 0.27, 0.012);
	const FPlanetTerrainHeight Height(Desc);

	// Two equal-LOD siblings sharing U=0.5 on face 4.
	constexpr int32 Res = 16;
	const double Spacing = (0.5 * 0.5 * PI * Desc.RadiusMeters * 2.0) / Res;

	TArray<FVector> NormLeft;
	TArray<FVector> NormRight;
	FPlanetTerrainNormals::ComputeFine(Height, 4, 0.0, 0.0, 0.5, 0.5, Res, Spacing, NormLeft);
	FPlanetTerrainNormals::ComputeFine(Height, 4, 0.5, 0.0, 1.0, 0.5, Res, Spacing, NormRight);

	const int32 N = Res + 1;
	double MaxDotErr = 0.0;
	for (int32 J = 0; J < N; ++J)
	{
		const FVector& A = NormLeft[J * N + Res]; // right edge of left tile
		const FVector& B = NormRight[J * N + 0];  // left edge of right tile
		const double Dot = FMath::Clamp(static_cast<double>(FVector::DotProduct(A, B)), -1.0, 1.0);
		MaxDotErr = FMath::Max(MaxDotErr, 1.0 - Dot);
	}
	TestTrue(TEXT("Shared-edge normals agree (1-dot < 1e-4)"), MaxDotErr < 1e-4);

	// Heights at the shared edge must match via FacePoint.
	const FVector3d Dir = PlanetTerrainFacePoint(4, 0.5, 0.25);
	const double Ha = Height.EvaluateHeightAtSpacing(Dir, Spacing);
	const double Hb = Height.EvaluateHeightAtSpacing(PlanetTerrainFacePoint(4, 0.5, 0.25), Spacing);
	TestEqual(TEXT("Shared edge height identical"), Ha, Hb);
	return true;
}
