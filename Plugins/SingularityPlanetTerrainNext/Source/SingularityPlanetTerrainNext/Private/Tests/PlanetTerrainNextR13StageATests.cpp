#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextCoordinates.h"
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

FPlanetTerrainNextDescriptor StageBDescriptor(int64 Seed = 7)
{
	FPlanetTerrainNextDescriptor Descriptor = StageADescriptor(Seed);
	Descriptor.bEnableGeologicalBiomes = true;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR13GeologyMaterialFieldTest,
	"Singularity.PlanetTerrainNext.R13.StageB.MaterialField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR13GeologyMaterialFieldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Descriptor = StageBDescriptor();
	const FPlanetTerrainNextHeightField Field(Descriptor);
	const FPlanetTerrainNextGeologyModel& Model = Field.GetGeologyModel();
	bool bHasBasalt = false;
	for (const FPlanetTerrainNextGeologyEvent& Event : Model.GetEvents())
	{
		bHasBasalt |= Event.Type == EPlanetTerrainNextGeologyEventType::BasaltEpisode;
	}
	TestTrue(TEXT("catalog contains basalt episodes"), bHasBasalt);

	int32 BasinIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Model.GetEvents().Num(); ++Index)
	{
		if (Model.GetEvents()[Index].Type == EPlanetTerrainNextGeologyEventType::AncientBasin)
		{
			BasinIndex = Index;
			break;
		}
	}
	TestTrue(TEXT("catalog has an ancient basin"), BasinIndex != INDEX_NONE);
	if (BasinIndex == INDEX_NONE)
	{
		return false;
	}

	const FVector3d BasinDirection = Model.GetEvents()[BasinIndex].CenterUnit;
	const FPlanetTerrainNextSurfaceSample Basin = Field.SampleGeology(BasinDirection, 100.0);
	const double BasinWeightSum = Basin.HighlandWeight + Basin.MareWeight + Basin.EjectaWeight + Basin.ImpactMeltWeight;
	TestTrue(TEXT("basin produces mare composition"), Basin.MareWeight > 0.20);
	TestTrue(TEXT("basin produces macro relief"), Basin.HeightOffsetM < 0.0);
	TestTrue(TEXT("basin composition is normalized"), FMath::IsNearlyEqual(BasinWeightSum, 1.0, 1.0e-9));
	TestTrue(TEXT("basin sample is finite"), FMath::IsFinite(Basin.HeightOffsetM) && FMath::IsFinite(Basin.ExposureAgeMyr));
	const FPlanetTerrainNextHeightField LegacyField(StageADescriptor());
	TestTrue(TEXT("height path includes opt-in geology"), FMath::Abs(Field.HeightAt(BasinDirection, 0.0) - LegacyField.HeightAt(BasinDirection, 0.0)) > 0.1);

	FPlanetTerrainNextDetailTile Detail;
	const FPlanetTerrainNextDetailTileId Id = FPlanetTerrainNextDetailTileId::MakeRoot(
		Descriptor.PlanetId, Field.GetIdentityHash(), 0, 16);
	PlanetTerrainNextDetailTile::Bake(Field, Id, Detail);
	TestTrue(TEXT("geological detail tile is valid"), Detail.bValid);
	TestEqual(TEXT("geological detail tile texel count"), Detail.Albedo.Num(), 16 * 16);
	if (Detail.Albedo.Num() > 0)
	{
		int32 Distinct = 0;
		for (int32 Index = 1; Index < Detail.Albedo.Num(); ++Index)
		{
			Distinct += Detail.Albedo[Index] != Detail.Albedo[0] ? 1 : 0;
		}
		TestTrue(TEXT("geological albedo has spatial variation"), Distinct > 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR13GeologySeamTest,
	"Singularity.PlanetTerrainNext.R13.StageB.SphericalSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR13GeologySeamTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextGeologyModel Model(StageBDescriptor());
	const TArray<double> EdgeSamples = {0.13, 0.37, 0.63, 0.87};
	for (double U : EdgeSamples)
	{
		const FVector3d Face0 = PlanetTerrainNext::FacePoint(0, U, 1.0);
		const FVector3d Face2 = PlanetTerrainNext::FacePoint(2, 1.0, U);
		TestTrue(TEXT("cube-face edge directions agree"), Face0.Equals(Face2, 1.0e-12));
		const FPlanetTerrainNextSurfaceSample A = Model.SampleSurface(Face0, 10.0);
		const FPlanetTerrainNextSurfaceSample B = Model.SampleSurface(Face2, 10.0);
		TestTrue(TEXT("edge heights agree"), FMath::IsNearlyEqual(A.HeightOffsetM, B.HeightOffsetM, 1.0e-9));
		TestTrue(TEXT("edge mare weights agree"), FMath::IsNearlyEqual(A.MareWeight, B.MareWeight, 1.0e-12));
		TestTrue(TEXT("edge samples finite"), FMath::IsFinite(A.MareWeight) && FMath::IsFinite(B.MareWeight));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR13GeologyBakeBudgetTest,
	"Singularity.PlanetTerrainNext.R13.StageB.BakeBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR13GeologyBakeBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Descriptor = StageBDescriptor();
	const FPlanetTerrainNextHeightField Field(Descriptor);
	const FPlanetTerrainNextDetailTileId Id = FPlanetTerrainNextDetailTileId::MakeRoot(
		Descriptor.PlanetId, Field.GetIdentityHash(), 0, 16);

	double TotalMs = 0.0;
	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		FPlanetTerrainNextDetailTile Tile;
		const double Start = FPlatformTime::Seconds();
		PlanetTerrainNextDetailTile::Bake(Field, Id, Tile);
		TotalMs += (FPlatformTime::Seconds() - Start) * 1000.0;
		TestTrue(TEXT("geological bake valid"), Tile.bValid);
	}
	AddInfo(FString::Printf(TEXT("Stage B geological detail bake (16x16): average %.3f ms"), TotalMs / 3.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR13GeologyMorphologyTest,
	"Singularity.PlanetTerrainNext.R13.StageC.Morphology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR13GeologyMorphologyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextGeologyModel Model(StageBDescriptor());
	const FPlanetTerrainNextGeologyEvent* LargestImpact = nullptr;
	for (const FPlanetTerrainNextGeologyEvent& Event : Model.GetEvents())
	{
		if (Event.Type == EPlanetTerrainNextGeologyEventType::Impact
			&& (!LargestImpact || Event.RadiusMeters > LargestImpact->RadiusMeters))
		{
			LargestImpact = &Event;
		}
	}
	TestTrue(TEXT("catalog contains impact morphology"), LargestImpact != nullptr);
	if (!LargestImpact)
	{
		return false;
	}

	const FPlanetTerrainNextSurfaceSample Center = Model.SampleSurface(LargestImpact->CenterUnit, 100.0);
	const FPlanetTerrainNextSurfaceSample Rim = Model.SampleSurface(
		(LargestImpact->CenterUnit + FVector3d::UnitX() * 0.002).GetSafeNormal(), 100.0);
	TestTrue(TEXT("impact sample is finite"), FMath::IsFinite(Center.HeightOffsetM) && FMath::IsFinite(Rim.HeightOffsetM));
	TestTrue(TEXT("impact exposes age field"), Center.ExposureAgeMyr >= 0.0 && Center.ExposureAgeMyr <= 4200.0);
	TestTrue(TEXT("impact morphology changes height"), FMath::Abs(Center.HeightOffsetM - Rim.HeightOffsetM) > 0.01);
	TestTrue(TEXT("impact freshness is bounded"), Center.Freshness >= 0.0 && Center.Freshness <= 1.0);
	return true;
}

#endif
