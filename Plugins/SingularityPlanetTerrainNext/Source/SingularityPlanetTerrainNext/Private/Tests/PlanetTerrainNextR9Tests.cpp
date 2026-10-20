#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextDetailTile.h"
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
	D.PlanetId = 9;
	D.GeneratorVersion = 1;
	return D;
}

FVector3d WorldM(const FPlanetTerrainNextPatchData& Patch, int32 Vertex)
{
	return Patch.OriginM + FVector3d(Patch.VerticesCm[Vertex]) / PlanetTerrainNext::CmPerMeter;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR91MorphUvPrecisionTest,
	"Singularity.PlanetTerrainNext.R9.MorphUvPrecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR91MorphUvPrecisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDesc());
	double WorstDecodeM = 0.0;
	double MaxAbsSeenCm = 0.0;
	bool bSawCoarseOverflowRisk = false;

	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (const int32 Level : {0, 1, 2, 3, 8})
		{
			const uint32 Mid = (1u << Level) / 2;
			FPlanetTerrainNextPatchData Patch;
			Build(Field, FPlanetTerrainNextNodeId(static_cast<uint8>(Face), static_cast<uint8>(Level), Mid, Mid), 10.0, Patch);
			TestTrue(TEXT("patch built"), Patch.VerticesCm.Num() == VertexCount);

			MaxAbsSeenCm = FMath::Max(MaxAbsSeenCm, Patch.MaxAbsCoarseDeltaCm);
			if (Level >= 1 && Patch.MaxAbsCoarseDeltaCm > 65504.0)
			{
				bSawCoarseOverflowRisk = true;
			}

			const float Scale = static_cast<float>(FMath::Max(Patch.MaxAbsCoarseDeltaCm, 1.0));
			for (int32 Index = 0; Index < VertexCount; ++Index)
			{
				const FVector2D& A = Patch.UV1[Index];
				const FVector2D& B = Patch.UV2[Index];
				TestTrue(TEXT("UV1 finite"), FMath::IsFinite(A.X) && FMath::IsFinite(A.Y));
				TestTrue(TEXT("UV2 finite"), FMath::IsFinite(B.X) && FMath::IsFinite(B.Y));
				TestTrue(TEXT("normalized |UV1| <= 1.01"), FMath::Abs(A.X) <= 1.01f && FMath::Abs(A.Y) <= 1.01f);
				TestTrue(TEXT("normalized |UV2.x| <= 1.01"), FMath::Abs(B.X) <= 1.01f);

				const FVector Expected = FMath::Lerp(Patch.VerticesCm[Index], Patch.CoarseVerticesCm[Index], 1.0f);
				const FVector Decoded(
					Patch.VerticesCm[Index].X + A.X * Scale,
					Patch.VerticesCm[Index].Y + A.Y * Scale,
					Patch.VerticesCm[Index].Z + B.X * Scale);
				const double ErrM = (Expected - Decoded).Size() / PlanetTerrainNext::CmPerMeter;
				WorstDecodeM = FMath::Max(WorstDecodeM, ErrM);
			}
		}
	}

	AddInfo(FString::Printf(TEXT("MaxAbsCoarseDeltaCm seen=%.1f (F16 max~65504) overflowRisk=%s worstDecode=%.6f m"),
		MaxAbsSeenCm, bSawCoarseOverflowRisk ? TEXT("yes") : TEXT("no"), WorstDecodeM));
	TestTrue(TEXT("raw deltas at L1+ can exceed F16 (documents why normalize)"), bSawCoarseOverflowRisk || MaxAbsSeenCm > 1000.0);
	TestTrue(TEXT("float decode of normalized UV < 1 cm"), WorstDecodeM * PlanetTerrainNext::CmPerMeter < 1.0);

	// F16 quantization bound: ULP(1)≈0.001 → error_cm ≈ 0.001 * Scale. Scale>1000 ⇒ >1cm.
	const double F16UlpNearOne = 1.0 / 1024.0;
	const double QuantErrorAtMaxAbsCm = F16UlpNearOne * MaxAbsSeenCm;
	AddInfo(FString::Printf(TEXT("F16×Scale quantization at max |Δ|: ≈%.1f cm (CPU morph required above ~1000 cm scale)"),
		QuantErrorAtMaxAbsCm));
	TestTrue(TEXT("coarse |Δ| implies unsafe F16×Scale quantization"), QuantErrorAtMaxAbsCm > 1.0 || MaxAbsSeenCm <= 1000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR91SkirtWindingTest,
	"Singularity.PlanetTerrainNext.R9.SkirtWinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR91SkirtWindingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDesc());
	const TArray<int32>& Idx = Indices();
	const int32 GridTris = GridN * GridN * 2;
	const int32 SkirtTris = 4 * GridN * 2;

	for (int32 Face = 0; Face < 6; ++Face)
	{
		FPlanetTerrainNextPatchData Patch;
		Build(Field, FPlanetTerrainNextNodeId(static_cast<uint8>(Face), 0, 0, 0), 50.0, Patch);

		bool bSkirtsOutward = true;
		for (int32 Tri = 0; Tri < SkirtTris && bSkirtsOutward; ++Tri)
		{
			const int32 Base = (GridTris + Tri) * 3;
			const FVector3d A = WorldM(Patch, Idx[Base + 0]);
			const FVector3d B = WorldM(Patch, Idx[Base + 1]);
			const FVector3d C = WorldM(Patch, Idx[Base + 2]);
			const FVector3d Front = -FVector3d::CrossProduct(B - A, C - A);
			const FVector3d Mid = (A + B + C) / 3.0;
			// Skirt hangs inward; front should still have a positive component away from center
			// along the skirt mid's radial (same UE convention as grid tris).
			bSkirtsOutward = FVector3d::DotProduct(Front.GetSafeNormal(), Mid.GetSafeNormal()) > -0.15;
		}
		TestTrue(FString::Printf(TEXT("face %d skirt fronts roughly outward"), Face), bSkirtsOutward);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR91FiniteAttributesTest,
	"Singularity.PlanetTerrainNext.R9.FiniteAttributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR91FiniteAttributesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDesc());
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (const int32 Level : {0, 1})
		{
			FPlanetTerrainNextPatchData Patch;
			Build(Field, FPlanetTerrainNextNodeId(static_cast<uint8>(Face), static_cast<uint8>(Level), 0, 0), 25.0, Patch);
			for (int32 Index = 0; Index < VertexCount; ++Index)
			{
				const FVector& V = Patch.VerticesCm[Index];
				const FVector& C = Patch.CoarseVerticesCm[Index];
				const FVector& N = Patch.Normals[Index];
				TestTrue(TEXT("vertex finite"), FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z));
				TestTrue(TEXT("coarse finite"), FMath::IsFinite(C.X) && FMath::IsFinite(C.Y) && FMath::IsFinite(C.Z));
				TestTrue(TEXT("normal finite"), FMath::IsFinite(N.X) && FMath::IsFinite(N.Y) && FMath::IsFinite(N.Z));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR93DetailBakeDeterminismTest,
	"Singularity.PlanetTerrainNext.R9.DetailBakeDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR93DetailBakeDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField Field(MoonDesc());
	const FPlanetTerrainNextDetailTileId Id = FPlanetTerrainNextDetailTileId::MakeRoot(
		MoonDesc().PlanetId, Field.GetIdentityHash(), 0, 64);

	FPlanetTerrainNextDetailTile A;
	FPlanetTerrainNextDetailTile B;
	PlanetTerrainNextDetailTile::Bake(Field, Id, A);
	PlanetTerrainNextDetailTile::Bake(Field, Id, B);

	TestTrue(TEXT("bake A valid"), A.bValid);
	TestTrue(TEXT("bake B valid"), B.bValid);
	TestEqual(TEXT("albedo size"), A.Albedo.Num(), 64 * 64);
	TestEqual(TEXT("normal size"), A.Normal.Num(), 64 * 64);
	TestTrue(TEXT("albedo identical"), A.Albedo == B.Albedo);
	TestTrue(TEXT("normal identical"), A.Normal == B.Normal);

	int32 NonFlatNormals = 0;
	int32 AlbedoVariance = 0;
	const FColor FirstAlbedo = A.Albedo[0];
	for (int32 Index = 0; Index < A.Normal.Num(); ++Index)
	{
		const FColor& N = A.Normal[Index];
		if (FMath::Abs(static_cast<int32>(N.R) - 128) > 2 || FMath::Abs(static_cast<int32>(N.G) - 128) > 2)
		{
			++NonFlatNormals;
		}
		if (A.Albedo[Index] != FirstAlbedo)
		{
			++AlbedoVariance;
		}
	}
	TestTrue(TEXT("some non-flat tangent normals"), NonFlatNormals > 16);
	TestTrue(TEXT("albedo not constant"), AlbedoVariance > 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextR93DetailFaceUvMappingTest,
	"Singularity.PlanetTerrainNext.R9.DetailFaceUvMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextR93DetailFaceUvMappingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// Patch UV [0,1] → FaceUV = Offset + UV * Scale for any node (root and children).
	const FPlanetTerrainNextNodeId Root(2, 0, 0, 0);
	TestEqual(TEXT("root scale"), Root.Size(), 1.0);
	TestEqual(TEXT("root U0"), Root.U0(), 0.0);
	TestEqual(TEXT("root V0"), Root.V0(), 0.0);

	const FPlanetTerrainNextNodeId Child = Root.Child(3); // high X, high Y
	TestEqual(TEXT("child level"), static_cast<int32>(Child.Level), 1);
	TestEqual(TEXT("child scale"), Child.Size(), 0.5);
	TestEqual(TEXT("child U0"), Child.U0(), 0.5);
	TestEqual(TEXT("child V0"), Child.V0(), 0.5);

	const FVector2D PatchUV(0.25, 0.75);
	const FVector2D FaceUV(
		Child.U0() + PatchUV.X * Child.Size(),
		Child.V0() + PatchUV.Y * Child.Size());
	TestEqual(TEXT("mapped U"), FaceUV.X, 0.625);
	TestEqual(TEXT("mapped V"), FaceUV.Y, 0.875);
	TestTrue(TEXT("inside child"), Child.ContainsUV(FaceUV.X, FaceUV.Y));
	return true;
}

#endif
