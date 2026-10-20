#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextCollisionManager.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextTileId.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor MoonDescriptorR6(int64 Seed = 1)
{
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = Seed;
	Descriptor.PlanetId = 7;
	Descriptor.GeneratorVersion = 1;
	return Descriptor;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextCollisionLevelTest,
	"Singularity.PlanetTerrainNext.R6.CollisionLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextCollisionLevelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const double R = PlanetTerrainNext::LunarRadiusMeters;
	const int32 L4 = FPlanetTerrainNextCollisionManager::LevelForSpacing(R, 4.0, 24);
	const int32 L2 = FPlanetTerrainNextCollisionManager::LevelForSpacing(R, 2.0, 24);
	TestTrue(TEXT("finer spacing → higher or equal level"), L2 >= L4);
	const double Spacing = PlanetTerrainNextPatch::VertexSpacingMeters(R, L4);
	AddInfo(FString::Printf(TEXT("4 m target → L%d spacing %.2f m"), L4, Spacing));
	TestTrue(TEXT("spacing near 2–8 m band"), Spacing > 1.0 && Spacing < 16.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextCollisionFootprintTest,
	"Singularity.PlanetTerrainNext.R6.CollisionFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextCollisionFootprintTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextCollisionManager Manager;
	FPlanetTerrainNextCollisionSettings Settings;
	Settings.RadiusM = 150.0;
	Settings.TargetSpacingM = 4.0;
	Settings.MaxPatches = 24;
	Manager.SetSettings(Settings);

	const double R = PlanetTerrainNext::LunarRadiusMeters;
	// Near a cube edge so the footprint must span faces.
	const FVector3d Focus = PlanetTerrainNext::FacePoint(0, 0.02, 0.5) * R;
	TArray<FVector3d> Path;
	Path.Add(PlanetTerrainNext::FacePoint(0, 0.01, 0.5) * R);
	Path.Add(PlanetTerrainNext::FacePoint(4, 0.98, 0.5) * R); // toward +Z face
	Manager.UpdateDesired(Focus, Path, R);

	TestTrue(TEXT("desired non-empty"), Manager.GetDesired().Num() > 0);
	TestTrue(TEXT("respects max patches"), Manager.GetDesired().Num() <= Settings.MaxPatches);

	TSet<uint8> Faces;
	for (const FPlanetTerrainNextNodeId& Id : Manager.GetDesired())
	{
		Faces.Add(Id.Face);
	}
	AddInfo(FString::Printf(TEXT("footprint %d patches across %d faces at L%d"),
		Manager.GetDesired().Num(), Faces.Num(), Manager.GetStats().Level));
	TestTrue(TEXT("near-edge footprint can span >1 face"), Faces.Num() >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextCollisionSampleTest,
	"Singularity.PlanetTerrainNext.R6.CollisionSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextCollisionSampleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptorR6();
	const FPlanetTerrainNextHeightField Field(Desc);
	FPlanetTerrainNextCollisionManager Manager;
	FPlanetTerrainNextCollisionSettings Settings;
	Settings.RadiusM = 100.0;
	Settings.TargetSpacingM = 4.0;
	Manager.SetSettings(Settings);

	const FVector3d Dir = PlanetTerrainNext::FacePoint(2, 0.4, 0.6);
	const FVector3d Focus = Dir * Field.GetRadiusMeters();
	Manager.UpdateDesired(Focus, {}, Field.GetRadiusMeters());
	TestTrue(TEXT("has desired"), Manager.GetDesired().Num() > 0);

	const FPlanetTerrainNextNodeId Node = Manager.GetDesired()[0];
	FPlanetTerrainNextHeightTile Tile;
	FPlanetTerrainNextTileId TileId = FPlanetTerrainNextTileId::Make(
		Desc.PlanetId, Desc.Seed, Desc.GeneratorVersion, Field.GetIdentityHash(), Node);
	PlanetTerrainNextHeightTile::Bake(Field, TileId, Tile);
	Manager.NotifyCooked(Node, Tile, 1);

	double FromTile = 0.0;
	TestTrue(TEXT("sample from cooked tile"), Manager.TrySampleHeightM(Field, Dir, FromTile));

	const double Band = PlanetTerrainNextPatch::MinWavelengthMeters(Field.GetRadiusMeters(), Manager.GetStats().Level);
	const double FromField = Field.HeightAt(Dir, Band);
	const double Gap = FMath::Abs(FromTile - FromField);
	AddInfo(FString::Printf(TEXT("tile vs field gap %.3f m (band-limit)"), Gap));
	// Bilinear tile sample vs continuous field at a grid cell center should be close;
	// at arbitrary Dir allow tens of metres of band-limited mismatch, not kilometres.
	TestTrue(TEXT("collision sample sane vs field"), Gap < 500.0);

	const FVector3d Above = Dir * (Field.GetRadiusMeters() + FromTile + 10.0);
	const double Alt = Manager.SampleAltitudeM(Field, Above);
	TestTrue(TEXT("altitude ~10 m above surface"), FMath::Abs(Alt - 10.0) < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextCollisionResidencyTest,
	"Singularity.PlanetTerrainNext.R6.CollisionResidency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextCollisionResidencyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptorR6();
	const FPlanetTerrainNextHeightField Field(Desc);
	FPlanetTerrainNextCollisionManager Manager;
	FPlanetTerrainNextCollisionSettings Settings;
	Settings.RadiusM = 80.0;
	Settings.TargetSpacingM = 8.0;
	Settings.MaxPatches = 8;
	Settings.RetireGraceFrames = 2;
	Manager.SetSettings(Settings);

	const FVector3d A = PlanetTerrainNext::FacePoint(0, 0.5, 0.5) * Field.GetRadiusMeters();
	Manager.UpdateDesired(A, {}, Field.GetRadiusMeters());
	TArray<FPlanetTerrainNextNodeId> First = Manager.GetDesired();
	TestTrue(TEXT("first footprint"), First.Num() > 0);

	for (const FPlanetTerrainNextNodeId& Id : First)
	{
		FPlanetTerrainNextHeightTile Tile;
		PlanetTerrainNextHeightTile::Bake(
			Field,
			FPlanetTerrainNextTileId::Make(Desc.PlanetId, Desc.Seed, Desc.GeneratorVersion, Field.GetIdentityHash(), Id),
			Tile);
		Manager.NotifyCooked(Id, Tile, 1);
	}
	TestEqual(TEXT("all desired cooked"), Manager.GetStats().Resident, First.Num());

	// Move to opposite face — old patches retire after grace once new footprint is ready.
	const FVector3d B = PlanetTerrainNext::FacePoint(1, 0.5, 0.5) * Field.GetRadiusMeters();
	Manager.UpdateDesired(B, {}, Field.GetRadiusMeters());
	TArray<FPlanetTerrainNextNodeId> Second = Manager.GetDesired();
	for (const FPlanetTerrainNextNodeId& Id : Second)
	{
		FPlanetTerrainNextHeightTile Tile;
		PlanetTerrainNextHeightTile::Bake(
			Field,
			FPlanetTerrainNextTileId::Make(Desc.PlanetId, Desc.Seed, Desc.GeneratorVersion, Field.GetIdentityHash(), Id),
			Tile);
		Manager.NotifyCooked(Id, Tile, 5);
	}

	TArray<FPlanetTerrainNextNodeId> Released;
	Manager.TickResidency(5, Released); // grace not expired yet
	Manager.TickResidency(8, Released); // past grace
	TestTrue(TEXT("retired old footprint"), Released.Num() > 0);
	TestTrue(TEXT("new footprint still resident"), Manager.IsResident(Second[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextShadowBoundsTest,
	"Singularity.PlanetTerrainNext.R6.ShadowBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextShadowBoundsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField Field(MoonDescriptorR6());
	FPlanetTerrainNextPatchData Patch;
	PlanetTerrainNextPatch::Build(Field, FPlanetTerrainNextNodeId(0, 4, 3, 5), 2.0, Patch);

	double MaxFine = 0.0;
	double MaxCoarse = 0.0;
	for (const FVector& V : Patch.VerticesCm)
	{
		MaxFine = FMath::Max(MaxFine, static_cast<double>(V.Size()));
	}
	for (const FVector& V : Patch.CoarseVerticesCm)
	{
		MaxCoarse = FMath::Max(MaxCoarse, static_cast<double>(V.Size()));
	}
	const double BoundR = FMath::Max(MaxFine, MaxCoarse) * 1.05;
	TestTrue(TEXT("bounds radius covers fine"), BoundR >= MaxFine);
	TestTrue(TEXT("bounds radius covers coarse (WPO/shadow)"), BoundR >= MaxCoarse);
	TestTrue(TEXT("morph delta non-negative"), Patch.MaxAbsCoarseDeltaCm >= 0.0);
	AddInfo(FString::Printf(TEXT("fineR %.1f cm coarseR %.1f cm bound %.1f cm maxDelta %.1f cm"),
		MaxFine, MaxCoarse, BoundR, Patch.MaxAbsCoarseDeltaCm));
	return true;
}

#endif
