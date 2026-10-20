#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextQuadtree.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor MoonDescriptorR3(int64 Seed = 1)
{
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = Seed;
	return Descriptor;
}

FVector3d MorphedWorld(const FPlanetTerrainNextPatchData& Patch, int32 Vertex, float Morph)
{
	const FVector Fine = Patch.VerticesCm[Vertex];
	const FVector Coarse = Patch.CoarseVerticesCm[Vertex];
	const FVector Displayed = FMath::Lerp(Fine, Coarse, Morph);
	return Patch.OriginM + FVector3d(Displayed) / PlanetTerrainNext::CmPerMeter;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextMorphCurveTest,
	"Singularity.PlanetTerrainNext.R3.MorphCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextMorphCurveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("PE far below target → coarse"), PlanetTerrainNextPatch::MorphFromPixelError(1.0, 4.0, 2.0), 1.0f);
	TestEqual(TEXT("PE at target → coarse"), PlanetTerrainNextPatch::MorphFromPixelError(4.0, 4.0, 2.0), 1.0f);
	TestEqual(TEXT("PE at 2x target → fine"), PlanetTerrainNextPatch::MorphFromPixelError(8.0, 4.0, 2.0), 0.0f);
	TestTrue(TEXT("mid-band is between"), PlanetTerrainNextPatch::MorphFromPixelError(6.0, 4.0, 2.0) > 0.0f
		&& PlanetTerrainNextPatch::MorphFromPixelError(6.0, 4.0, 2.0) < 1.0f);
	TestTrue(TEXT("monotone in PE"), PlanetTerrainNextPatch::MorphFromPixelError(5.0, 4.0, 2.0)
		> PlanetTerrainNextPatch::MorphFromPixelError(7.0, 4.0, 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextParentCompatibleTest,
	"Singularity.PlanetTerrainNext.R3.ParentCompatible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextParentCompatibleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDescriptorR3());

	double WorstEven = 0.0;
	double WorstMid = 0.0;
	double WorstFineGap = 0.0;
	int32 Patches = 0;

	FRandomStream Random(2026);
	for (int32 Trial = 0; Trial < 24; ++Trial)
	{
		const int32 Level = 2 + Trial % 8;
		const uint32 N = 1u << Level;
		const uint8 Face = static_cast<uint8>(Random.RandRange(0, 5));
		const uint32 X = static_cast<uint32>(Random.RandRange(0, static_cast<int32>(N) - 1));
		const uint32 Y = static_cast<uint32>(Random.RandRange(0, static_cast<int32>(N) - 1));
		const FPlanetTerrainNextNodeId Child(Face, static_cast<uint8>(Level), X, Y);
		const FPlanetTerrainNextNodeId Parent(Face, static_cast<uint8>(Level - 1), X >> 1, Y >> 1);

		FPlanetTerrainNextPatchData ChildPatch;
		FPlanetTerrainNextPatchData ParentPatch;
		Build(Field, Child, 1.0, ChildPatch);
		Build(Field, Parent, 1.0, ParentPatch);
		++Patches;

		// Every even child vertex must sit on a parent vertex when Morph = 1.
		for (int32 J = 0; J <= GridN; J += 2)
		{
			for (int32 I = 0; I <= GridN; I += 2)
			{
				const int32 ChildVertex = J * Row + I;
				const int32 ChildIndex = Child.ChildIndex();
				const int32 ParentI = ((ChildIndex & 1) ? GridN / 2 : 0) + I / 2;
				const int32 ParentJ = ((ChildIndex & 2) ? GridN / 2 : 0) + J / 2;
				const FVector3d ParentWorld = ParentPatch.OriginM
					+ FVector3d(ParentPatch.VerticesCm[ParentJ * Row + ParentI]) / PlanetTerrainNext::CmPerMeter;
				const FVector3d CoarseWorld = MorphedWorld(ChildPatch, ChildVertex, 1.0f);
				WorstEven = FMath::Max(WorstEven, (CoarseWorld - ParentWorld).Length());
			}
		}

		double MidGap = 0.0;
		double FineGap = 0.0;
		MeasureParentEdgeGaps(Field, Child, MidGap, FineGap);
		WorstMid = FMath::Max(WorstMid, MidGap);
		WorstFineGap = FMath::Max(WorstFineGap, FineGap);

		// Morph=0 is fine; Morph=1 is coarse; mid is a lerp — no mesh rebuild required for the blend.
		const int32 Probe = (GridN / 2) * Row + (GridN / 2);
		const FVector3d Fine = MorphedWorld(ChildPatch, Probe, 0.0f);
		const FVector3d Coarse = MorphedWorld(ChildPatch, Probe, 1.0f);
		const FVector3d Mid = MorphedWorld(ChildPatch, Probe, 0.5f);
		const FVector3d ExpectedMid = Fine * 0.5 + Coarse * 0.5;
		TestTrue(TEXT("displayed position is lerp(fine, coarse, morph)"), (Mid - ExpectedMid).Length() < 1.0e-6);
	}

	AddInfo(FString::Printf(
		TEXT("ParentCompatible over %d patches: even-vertex Morph=1 gap %.6f cm, T-midpoint gap %.6f cm, fine-vs-parent %.3f m"),
		Patches, WorstEven * PlanetTerrainNext::CmPerMeter, WorstMid * PlanetTerrainNext::CmPerMeter, WorstFineGap));

	TestTrue(TEXT("Morph=1 even vertices match parent vertices (< 0.01 cm)"), WorstEven * PlanetTerrainNext::CmPerMeter < 0.01);
	TestTrue(TEXT("Morph=1 T-junction midpoints lie on parent edges (< 0.01 cm)"), WorstMid * PlanetTerrainNext::CmPerMeter < 0.01);
	TestTrue(TEXT("fine detail differs from the parent edge somewhere"), WorstFineGap > 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextMorphHandoffTest,
	"Singularity.PlanetTerrainNext.R3.MorphHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextMorphHandoffTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDescriptorR3());

	// Parent→children handoff: at Morph=1 every child vertex that lands on the parent
	// grid (even I,J) matches the parent vertex, and T-junction midpoints lie on parent edges.
	const FPlanetTerrainNextNodeId Parent(4, 3, 2, 5);
	FPlanetTerrainNextPatchData ParentPatch;
	Build(Field, Parent, 1.0, ParentPatch);

	double WorstEvenCm = 0.0;
	double WorstMidCm = 0.0;
	for (int32 ChildIndex = 0; ChildIndex < 4; ++ChildIndex)
	{
		const FPlanetTerrainNextNodeId Child = Parent.Child(ChildIndex);
		FPlanetTerrainNextPatchData ChildPatch;
		Build(Field, Child, 1.0, ChildPatch);

		for (int32 J = 0; J <= GridN; J += 2)
		{
			for (int32 I = 0; I <= GridN; I += 2)
			{
				const int32 ParentI = ((ChildIndex & 1) ? GridN / 2 : 0) + I / 2;
				const int32 ParentJ = ((ChildIndex & 2) ? GridN / 2 : 0) + J / 2;
				const FVector3d ParentWorld = ParentPatch.OriginM
					+ FVector3d(ParentPatch.VerticesCm[ParentJ * Row + ParentI]) / PlanetTerrainNext::CmPerMeter;
				const FVector3d ChildWorld = MorphedWorld(ChildPatch, J * Row + I, 1.0f);
				WorstEvenCm = FMath::Max(WorstEvenCm, (ChildWorld - ParentWorld).Length() * PlanetTerrainNext::CmPerMeter);
			}
		}

		double MidGap = 0.0;
		double FineGap = 0.0;
		MeasureParentEdgeGaps(Field, Child, MidGap, FineGap);
		WorstMidCm = FMath::Max(WorstMidCm, MidGap * PlanetTerrainNext::CmPerMeter);
	}

	AddInfo(FString::Printf(TEXT("Parent→4 children Morph=1: even gap %.6f cm, T-mid %.6f cm"), WorstEvenCm, WorstMidCm));
	TestTrue(TEXT("children at Morph=1 match parent vertices"), WorstEvenCm < 0.01);
	TestTrue(TEXT("children at Morph=1 seal parent edges at T-junctions"), WorstMidCm < 0.01);

	FPlanetTerrainNextPatchData Root;
	Build(Field, FPlanetTerrainNextNodeId(0, 0, 0, 0), 1.0, Root);
	double RootDelta = 0.0;
	for (int32 Index = 0; Index < GridVertexCount; ++Index)
	{
		RootDelta = FMath::Max(RootDelta, (Root.VerticesCm[Index] - Root.CoarseVerticesCm[Index]).Size());
	}
	TestTrue(TEXT("root coarse equals fine"), RootDelta < 1.0e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextNoMeshRebuildInvariantTest,
	"Singularity.PlanetTerrainNext.R3.NoMeshRebuildInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextNoMeshRebuildInvariantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDescriptorR3());
	FPlanetTerrainNextPatchData Patch;
	Build(Field, FPlanetTerrainNextNodeId(2, 5, 3, 7), 5.0, Patch);

	const TArray<FVector> FineSnapshot = Patch.VerticesCm;
	const TArray<FVector> CoarseSnapshot = Patch.CoarseVerticesCm;
	const TArray<FVector2D> UV1Snapshot = Patch.UV1;
	const TArray<FVector2D> UV2Snapshot = Patch.UV2;

	// R9.1: UVs hold normalized deltas; reconstruct with MorphScaleCm = max(|Δ|, 1).
	const float MorphScaleCm = static_cast<float>(FMath::Max(Patch.MaxAbsCoarseDeltaCm, 1.0));
	for (float Morph : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
	{
		for (int32 Index = 0; Index < VertexCount; ++Index)
		{
			const FVector Expected = FMath::Lerp(FineSnapshot[Index], CoarseSnapshot[Index], Morph);
			const FVector FromUV(
				FineSnapshot[Index].X + UV1Snapshot[Index].X * MorphScaleCm * Morph,
				FineSnapshot[Index].Y + UV1Snapshot[Index].Y * MorphScaleCm * Morph,
				FineSnapshot[Index].Z + UV2Snapshot[Index].X * MorphScaleCm * Morph);
			if (!TestTrue(TEXT("normalized UV * MorphScaleCm reconstructs Morph lerp"),
				(Expected - FromUV).Size() < 1.0e-2))
			{
				return true;
			}
		}
	}

	TestEqual(TEXT("fine buffer unchanged after morph probes"), Patch.VerticesCm.Num(), FineSnapshot.Num());
	for (int32 Index = 0; Index < FineSnapshot.Num(); ++Index)
	{
		if (!TestEqual(TEXT("fine vertex immutable"), Patch.VerticesCm[Index], FineSnapshot[Index]))
		{
			break;
		}
		if (!TestEqual(TEXT("coarse vertex immutable"), Patch.CoarseVerticesCm[Index], CoarseSnapshot[Index]))
		{
			break;
		}
	}
	return true;
}

#endif
