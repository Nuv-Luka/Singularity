#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextDetailTile.h"
#include "PlanetTerrainNextGeology.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextTileId.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor StageADescriptor(int64 Seed = 7)
{
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.PlanetId = 101;
	Descriptor.GeneratorVersion = 1;
	Descriptor.Seed = Seed;
	Descriptor.RadiusMeters = 1737000.0;
	return Descriptor;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR13GeologyDeterminismTest,
	"Singularity.PlanetTerrainNext.R13.StageA.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR13GeologyDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Descriptor = StageADescriptor();
	const FPlanetTerrainNextGeologyModel A(Descriptor);
	const FPlanetTerrainNextGeologyModel B(Descriptor);
	TestEqual(TEXT("stable geology identity"), A.GetIdentityHash(), B.GetIdentityHash());
	TestEqual(TEXT("stable event count"), A.GetEvents().Num(), B.GetEvents().Num());
	for (int32 Index = 0; Index < A.GetEvents().Num(); ++Index)
	{
		const FPlanetTerrainNextGeologyEvent& Left = A.GetEvents()[Index];
		const FPlanetTerrainNextGeologyEvent& Right = B.GetEvents()[Index];
		TestEqual(TEXT("event id"), Left.Id, Right.Id);
		TestTrue(TEXT("event center deterministic"), Left.CenterUnit.Equals(Right.CenterUnit, 1.0e-12));
		TestEqual(TEXT("event seed deterministic"), Left.LocalSeed, Right.LocalSeed);
	}

	const FPlanetTerrainNextSurfaceSample SampleA = A.SampleSurface(FVector3d(0.3, -0.4, 0.85).GetSafeNormal(), 25.0);
	const FPlanetTerrainNextSurfaceSample SampleB = B.SampleSurface(FVector3d(0.3, -0.4, 0.85).GetSafeNormal(), 25.0);
	TestEqual(TEXT("sample material deterministic"), SampleA.MaterialId, SampleB.MaterialId);
	TestEqual(TEXT("sample mare deterministic"), SampleA.MareWeight, SampleB.MareWeight);
	TestEqual(TEXT("sample age deterministic"), SampleA.ExposureAgeMyr, SampleB.ExposureAgeMyr);

	const FPlanetTerrainNextGeologyModel OtherSeed(StageADescriptor(8));
	TestNotEqual(TEXT("different seed changes geology identity"), A.GetIdentityHash(), OtherSeed.GetIdentityHash());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR13GeologySphericalHelpersTest,
	"Singularity.PlanetTerrainNext.R13.StageA.SphericalHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR13GeologySphericalHelpersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FVector3d A = FVector3d::UnitX();
	const FVector3d B = FVector3d::UnitY();
	const double QuarterArc = PlanetTerrainNextGeology::GreatCircleDistanceMeters(A, B, 1000.0);
	TestTrue(TEXT("great-circle quarter arc"), FMath::IsNearlyEqual(QuarterArc, UE_DOUBLE_PI * 500.0, 1.0e-9));

	FVector3d East;
	FVector3d North;
	PlanetTerrainNextGeology::BuildTangentFrame(FVector3d(0.0, 0.0, 1.0), East, North);
	TestTrue(TEXT("east tangent"), FMath::Abs(FVector3d::DotProduct(East, FVector3d::UnitZ())) < 1.0e-12);
	TestTrue(TEXT("north tangent"), FMath::Abs(FVector3d::DotProduct(North, FVector3d::UnitZ())) < 1.0e-12);
	TestTrue(TEXT("orthogonal tangent frame"), FMath::Abs(FVector3d::DotProduct(East, North)) < 1.0e-12);
	TestTrue(TEXT("wrapped angle chooses shortest path"), FMath::Abs(PlanetTerrainNextGeology::WrappedAngleRadians(3.1, -3.1)) < 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR13GeologyLegacyIdentityTest,
	"Singularity.PlanetTerrainNext.R13.StageA.LegacyIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR13GeologyLegacyIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FPlanetTerrainNextDescriptor Legacy = StageADescriptor();
	Legacy.bEnableGeologicalBiomes = false;
	const FPlanetTerrainNextHeightField LegacyField(Legacy);
	const FVector3d Direction = FVector3d(0.2, 0.5, 0.84).GetSafeNormal();
	const double LegacyHeight = LegacyField.HeightAt(Direction, 0.0);

	FPlanetTerrainNextDescriptor PresetChanged = Legacy;
	PresetChanged.GeologyPreset = 1;
	const FPlanetTerrainNextHeightField PresetField(PresetChanged);
	TestEqual(TEXT("legacy geometry stays unchanged when geology is off"), LegacyHeight, PresetField.HeightAt(Direction, 0.0));
	TestNotEqual(TEXT("preset changes invalidate cache identity"), LegacyField.GetIdentityHash(), PresetField.GetIdentityHash());
	const FPlanetTerrainNextNodeId Node(0, 3, 2, 1);
	const FPlanetTerrainNextTileId LegacyTile = FPlanetTerrainNextTileId::Make(
		Legacy.PlanetId, Legacy.Seed, Legacy.GeneratorVersion, LegacyField.GetIdentityHash(), Node);
	const FPlanetTerrainNextTileId PresetTile = FPlanetTerrainNextTileId::Make(
		PresetChanged.PlanetId, PresetChanged.Seed, PresetChanged.GeneratorVersion, PresetField.GetIdentityHash(), Node);
	TestNotEqual(TEXT("height tile key includes geology identity"), LegacyTile, PresetTile);
	const FPlanetTerrainNextDetailTileId LegacyDetail = FPlanetTerrainNextDetailTileId::MakeRoot(
		Legacy.PlanetId, LegacyField.GetIdentityHash(), 0, 64);
	const FPlanetTerrainNextDetailTileId PresetDetail = FPlanetTerrainNextDetailTileId::MakeRoot(
		PresetChanged.PlanetId, PresetField.GetIdentityHash(), 0, 64);
	TestFalse(TEXT("detail tile key includes geology identity"), LegacyDetail == PresetDetail);

	const FPlanetTerrainNextSurfaceSample Sample = LegacyField.SampleGeology(Direction, 10.0);
	const double WeightSum = Sample.HighlandWeight + Sample.MareWeight + Sample.EjectaWeight + Sample.ImpactMeltWeight;
	TestTrue(TEXT("weights are normalized"), FMath::IsNearlyEqual(WeightSum, 1.0, 1.0e-12));
	TestTrue(TEXT("weights are finite"), FMath::IsFinite(WeightSum) && FMath::IsFinite(Sample.ExposureAgeMyr));
	return true;
}

#endif
