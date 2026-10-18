#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextQuadtree.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
using namespace PlanetTerrainNext;

FPlanetTerrainNextDescriptor MoonDescriptor(int64 Seed = 1)
{
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = Seed;
	return Descriptor;
}

FVector3d RandomUnit(FRandomStream& Random)
{
	const double Z = Random.FRandRange(-1.0, 1.0);
	const double Phi = Random.FRandRange(0.0, 2.0 * PI);
	const double S = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
	return FVector3d(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
}

TArray<double> GeometricErrors(const FPlanetTerrainNextHeightField& Field)
{
	TArray<double> Errors;
	for (int32 Level = 0; Level <= 24; ++Level)
	{
		Errors.Add(Field.GeometricErrorMeters(PlanetTerrainNextPatch::VertexSpacingMeters(Field.GetRadiusMeters(), Level)));
	}
	return Errors;
}

FPlanetTerrainNextNodeId ParentOf(const FPlanetTerrainNextNodeId& Id)
{
	return FPlanetTerrainNextNodeId(Id.Face, static_cast<uint8>(Id.Level - 1), Id.X >> 1, Id.Y >> 1);
}

/** Planet-local camera at an altitude above the full-detail surface at Up. */
FPlanetTerrainNextView MakeView(const FPlanetTerrainNextHeightField& Field, const FVector3d& Up, double AltitudeM, double TimeSeconds, bool bFlipForward = false)
{
	const FVector3d Dir = Up.GetSafeNormal();
	FPlanetTerrainNextView View;
	View.CameraM = Dir * (Field.GetRadiusMeters() + Field.HeightAt(Dir, 0.0) + AltitudeM);
	FVector3d Tangent = FVector3d::CrossProduct(Dir, FMath::Abs(Dir.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX()).GetSafeNormal();
	if (bFlipForward)
	{
		Tangent = -Tangent;
	}
	const double Pitch = AltitudeM > 0.5 * Field.GetRadiusMeters() ? 0.5 * PI : FMath::DegreesToRadians(20.0);
	View.Forward = (Tangent * FMath::Cos(Pitch) - Dir * FMath::Sin(Pitch)).GetSafeNormal();
	View.ProjectionScalePx = 1080.0 / (2.0 * 0.5625);
	View.HalfDiagonalFovRad = FMath::Atan(FMath::Sqrt(1.0 + 0.5625 * 0.5625));
	View.TimeSeconds = TimeSeconds;
	return View;
}

/** Pure CPU simulation of the component's frame loop: no meshes, readiness only. */
struct FCoverageSim
{
	const FPlanetTerrainNextHeightField& Field;
	FPlanetTerrainNextQuadtree Tree;
	FPlanetTerrainNextLodSettings Lod;
	TSet<FPlanetTerrainNextNodeId> Resident;
	TArray<FPlanetTerrainNextNodeId> DrawSet;
	int32 BuildsPerFrame = 64;
	int32 DenyChildIndex = -1;
	bool bOnlyRoots = false;
	double Time = 0.0;

	explicit FCoverageSim(const FPlanetTerrainNextHeightField& InField)
		: Field(InField)
	{
		Tree.Initialize(Field.GetRadiusMeters(), Field.GetMinHeightMeters(), Field.GetMaxHeightMeters(), GeometricErrors(Field));
		for (int32 Face = 0; Face < 6; ++Face)
		{
			const FPlanetTerrainNextNodeId Root(static_cast<uint8>(Face), 0, 0, 0);
			Resident.Add(Root);
			ReportHeightRange(Root);
		}
	}

	void Step(const FVector3d& Up, double AltitudeM, bool bFlip = false)
	{
		Time += 1.0 / 60.0;
		const FPlanetTerrainNextView View = MakeView(Field, Up, AltitudeM, Time, bFlip);
		auto IsReady = [this](const FPlanetTerrainNextNodeId& Id) { return Resident.Contains(Id); };

		TArray<FPlanetTerrainNextNodeId> Released;
		Tree.Update(View, Lod, IsReady, Released);
		for (const FPlanetTerrainNextNodeId& Id : Released)
		{
			Resident.Remove(Id);
		}

		if (!bOnlyRoots)
		{
			TArray<FPlanetTerrainNextPendingBuild> Pending;
			Tree.CollectPending(IsReady, Pending);
			int32 Built = 0;
			for (const FPlanetTerrainNextPendingBuild& Request : Pending)
			{
				if (Built >= BuildsPerFrame)
				{
					break;
				}
				if (DenyChildIndex >= 0 && Request.Id.Level > 0 && Request.Id.ChildIndex() == DenyChildIndex)
				{
					continue;
				}
				Resident.Add(Request.Id);
				ReportHeightRange(Request.Id);
				++Built;
			}
		}

		Tree.CollectDrawSet(IsReady, DrawSet);
	}

	/** Same feedback the component gives after a build; 5x5 samples of the patch grid. */
	void ReportHeightRange(const FPlanetTerrainNextNodeId& Id)
	{
		const double MinWavelength = PlanetTerrainNextPatch::MinWavelengthMeters(Field.GetRadiusMeters(), Id.Level);
		double MinH = TNumericLimits<double>::Max();
		double MaxH = TNumericLimits<double>::Lowest();
		for (int32 J = 0; J <= PlanetTerrainNextPatch::GridN; J += PlanetTerrainNextPatch::GridN / 4)
		{
			for (int32 I = 0; I <= PlanetTerrainNextPatch::GridN; I += PlanetTerrainNextPatch::GridN / 4)
			{
				const double H = Field.HeightAt(PlanetTerrainNextPatch::VertexDirection(Id, I, J), MinWavelength);
				MinH = FMath::Min(MinH, H);
				MaxH = FMath::Max(MaxH, H);
			}
		}
		Tree.SetNodeHeightRange(Id, MinH, MaxH);
	}

	int32 MaxDrawnLevel() const
	{
		int32 Level = 0;
		for (const FPlanetTerrainNextNodeId& Id : DrawSet)
		{
			Level = FMath::Max(Level, static_cast<int32>(Id.Level));
		}
		return Level;
	}

	/** Empty string when the draw set partitions all six faces exactly once with ready nodes. */
	FString CheckCoverage(FRandomStream& Random) const
	{
		TSet<FPlanetTerrainNextNodeId> Drawn;
		double Area[6] = {0, 0, 0, 0, 0, 0};
		for (const FPlanetTerrainNextNodeId& Id : DrawSet)
		{
			if (!Resident.Contains(Id))
			{
				return FString::Printf(TEXT("drawn node %s is not ready"), *Id.ToString());
			}
			bool bAlreadyIn = false;
			Drawn.Add(Id, &bAlreadyIn);
			if (bAlreadyIn)
			{
				return FString::Printf(TEXT("node %s drawn twice"), *Id.ToString());
			}
			Area[Id.Face] += Id.Size() * Id.Size();
		}
		for (int32 Face = 0; Face < 6; ++Face)
		{
			if (!FMath::IsNearlyEqual(Area[Face], 1.0, 1.0e-9))
			{
				return FString::Printf(TEXT("face %d covered area %.12f"), Face, Area[Face]);
			}
		}
		for (const FPlanetTerrainNextNodeId& Id : DrawSet)
		{
			for (FPlanetTerrainNextNodeId Up = Id; Up.Level > 0;)
			{
				Up = ParentOf(Up);
				if (Drawn.Contains(Up))
				{
					return FString::Printf(TEXT("%s drawn together with ancestor %s"), *Id.ToString(), *Up.ToString());
				}
			}
		}
		for (int32 Sample = 0; Sample < 64; ++Sample)
		{
			int32 Face = 0;
			double U = 0.0;
			double V = 0.0;
			DirectionToFaceUV(RandomUnit(Random), Face, U, V);
			int32 Owners = 0;
			for (const FPlanetTerrainNextNodeId& Id : DrawSet)
			{
				const bool bInU = U >= Id.U0() && (U < Id.U1() || (Id.U1() >= 1.0 && U <= 1.0));
				const bool bInV = V >= Id.V0() && (V < Id.V1() || (Id.V1() >= 1.0 && V <= 1.0));
				Owners += (Id.Face == Face && bInU && bInV) ? 1 : 0;
			}
			if (Owners != 1)
			{
				return FString::Printf(TEXT("direction on face %d (%.6f, %.6f) owned by %d drawn nodes"), Face, U, V, Owners);
			}
		}
		return FString();
	}
};

double LogLerp(double A, double B, double T)
{
	return FMath::Exp(FMath::Lerp(FMath::Loge(A), FMath::Loge(B), T));
}

/** Runs a full descent + hold, checking coverage every frame. Returns false on first failure. */
bool RunDescent(FAutomationTestBase& Test, FCoverageSim& Sim, const FVector3d& Up, const TCHAR* Label, int32 MaxNodes)
{
	FRandomStream Random(1234);
	const double Start = 3.0 * Sim.Field.GetRadiusMeters();
	constexpr int32 DescentFrames = 150;
	for (int32 Frame = 0; Frame < DescentFrames + 120; ++Frame)
	{
		const double T = FMath::Min(1.0, static_cast<double>(Frame) / DescentFrames);
		Sim.Step(Up, LogLerp(Start, 10.0, T));
		const FString Error = Sim.CheckCoverage(Random);
		if (!Error.IsEmpty())
		{
			Test.AddError(FString::Printf(TEXT("%s frame %d: %s"), Label, Frame, *Error));
			return false;
		}
		if (Sim.Tree.NumNodes() > MaxNodes)
		{
			Test.AddError(FString::Printf(TEXT("%s frame %d: %d nodes over cap %d"), Label, Frame, Sim.Tree.NumNodes(), MaxNodes));
			return false;
		}
	}
	return true;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextHeightFieldTest,
	"Singularity.PlanetTerrainNext.R2.HeightField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextHeightFieldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField A(MoonDescriptor(1));
	const FPlanetTerrainNextHeightField B(MoonDescriptor(1));
	const FPlanetTerrainNextHeightField Other(MoonDescriptor(2));

	FRandomStream Random(77);
	int32 Different = 0;
	constexpr int32 Samples = 400;
	for (int32 Index = 0; Index < Samples; ++Index)
	{
		const FVector3d Dir = RandomUnit(Random);
		const double H = A.HeightAt(Dir, 0.0);
		if (!TestEqual(TEXT("same descriptor gives bit-identical height"), H, B.HeightAt(Dir, 0.0)))
		{
			break;
		}
		Different += FMath::Abs(H - Other.HeightAt(Dir, 0.0)) > 1.0e-6 ? 1 : 0;
		if (!TestTrue(TEXT("height inside declared bounds"), H >= A.GetMinHeightMeters() && H <= A.GetMaxHeightMeters()))
		{
			AddInfo(FString::Printf(TEXT("H=%.3f bounds [%.3f, %.3f]"), H, A.GetMinHeightMeters(), A.GetMaxHeightMeters()));
			break;
		}
		for (const double MinWavelength : {1.0e3, 1.0e5, 1.0e6})
		{
			const double Hb = A.HeightAt(Dir, MinWavelength);
			if (!TestTrue(TEXT("band-limited height inside bounds"), Hb >= A.GetMinHeightMeters() && Hb <= A.GetMaxHeightMeters()))
			{
				break;
			}
		}
	}
	TestTrue(TEXT("different seed changes the surface"), Different > Samples * 9 / 10);
	TestNotEqual(TEXT("identity hash includes the seed"), A.GetIdentityHash(), Other.GetIdentityHash());
	TestEqual(TEXT("identity hash is stable"), A.GetIdentityHash(), B.GetIdentityHash());

	// Large features keep identity across LODs: once a feature is fully resolved, finer
	// band limits only add detail and never move it.
	const FPlanetTerrainNextCrater& Basin = A.GetCraters()[0];
	const double BasinAtCoarse = A.CraterContribution(0, Basin.Center, 500000.0);
	TestTrue(TEXT("basin 0 is a depression"), BasinAtCoarse < -1000.0);
	for (const double MinWavelength : {10000.0, 1.0, 0.0})
	{
		TestEqual(TEXT("basin 0 identical from 500 km to full detail"), A.CraterContribution(0, Basin.Center, MinWavelength), BasinAtCoarse);
	}
	for (int32 Index = 0; Index < 32; ++Index)
	{
		const FVector3d Dir = RandomUnit(Random);
		const double Macro = A.EvaluateComponents(Dir, 150000.0).Macro;
		TestEqual(TEXT("macro shape identical from 150 km to 10 km"), A.EvaluateComponents(Dir, 10000.0).Macro, Macro);
		TestEqual(TEXT("macro shape identical from 150 km to 1 m"), A.EvaluateComponents(Dir, 1.0).Macro, Macro);
	}

	// Crater rim height is continuous across the bowl edge (t = 1).
	const double R = A.GetRadiusMeters();
	const FVector3d Side = FVector3d::CrossProduct(Basin.Center, FVector3d::UnitZ()).GetSafeNormal();
	auto AtT = [&](double T)
	{
		const double Angle = T * Basin.RadiusM / R;
		return A.CraterContribution(0, (Basin.Center * FMath::Cos(Angle) + Side * FMath::Sin(Angle)).GetSafeNormal(), 0.0);
	};
	TestTrue(TEXT("crater profile continuous at the rim"), FMath::Abs(AtT(1.0 - 1.0e-7) - AtT(1.0 + 1.0e-7)) < 1.0);
	TestTrue(TEXT("crater contribution vanishes outside influence"), FMath::Abs(AtT(1.7)) < 1.0e-9);

	const TArray<double> Errors = GeometricErrors(A);
	for (int32 Level = 1; Level < Errors.Num(); ++Level)
	{
		if (!TestTrue(TEXT("geometric error never grows with level"), Errors[Level] <= Errors[Level - 1]))
		{
			break;
		}
	}
	TestTrue(TEXT("geometric error positive at the finest level"), Errors.Last() > 0.0);
	AddInfo(FString::Printf(TEXT("Heights [%.0f, %.0f] m. GE L0=%.0f m L8=%.2f m L12=%.3f m L17=%.3f m. Spacing L17=%.2f m."),
		A.GetMinHeightMeters(), A.GetMaxHeightMeters(), Errors[0], Errors[8], Errors[12], Errors[17],
		PlanetTerrainNextPatch::VertexSpacingMeters(R, 17)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextQuadtreeCoverageTest,
	"Singularity.PlanetTerrainNext.R2.QuadtreeCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextQuadtreeCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField Field(MoonDescriptor());

	struct FCase
	{
		const TCHAR* Label;
		FVector3d Up;
	};
	const FCase Cases[] = {
		{TEXT("cube corner"), FVector3d(1.0, 1.0, 1.0)},
		{TEXT("pole / face center"), FVector3d(0.0, 0.0, 1.0)},
		{TEXT("face edge"), FVector3d(1.0, 0.0, 1.0)},
		{TEXT("off-axis"), FVector3d(-0.31, 0.77, -0.52)},
	};

	for (const FCase& Case : Cases)
	{
		FCoverageSim Sim(Field);
		if (!RunDescent(*this, Sim, Case.Up, Case.Label, Sim.Lod.MaxNodes))
		{
			continue;
		}
		const int32 Deepest = Sim.MaxDrawnLevel();
		AddInfo(FString::Printf(TEXT("%s: 10 m altitude reaches L%d with %d nodes, %d drawn"), Case.Label, Deepest, Sim.Tree.NumNodes(), Sim.DrawSet.Num()));
		TestTrue(FString::Printf(TEXT("%s descent refines to at least L15"), Case.Label), Deepest >= 15);

		FRandomStream Random(99);
		bool bOk = true;
		for (int32 Frame = 0; Frame < 60 && bOk; ++Frame)
		{
			Sim.Step(Case.Up, 10.0, (Frame & 1) != 0);
			const FString Error = Sim.CheckCoverage(Random);
			bOk = TestTrue(FString::Printf(TEXT("%s rapid 180-degree turns keep coverage: %s"), Case.Label, *Error), Error.IsEmpty());
		}

		for (int32 Frame = 0; Frame < 300 && bOk; ++Frame)
		{
			const double T = FMath::Min(1.0, Frame / 120.0);
			Sim.Step(Case.Up, LogLerp(10.0, 3.0 * Field.GetRadiusMeters(), T));
			const FString Error = Sim.CheckCoverage(Random);
			bOk = TestTrue(FString::Printf(TEXT("%s ascent keeps coverage: %s"), Case.Label, *Error), Error.IsEmpty());
		}
		if (bOk)
		{
			TestTrue(FString::Printf(TEXT("%s ascent merges back to the roots (%d nodes)"), Case.Label, Sim.Tree.NumNodes()), Sim.Tree.NumNodes() <= 10);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextQuadtreeBudgetTest,
	"Singularity.PlanetTerrainNext.R2.QuadtreeBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextQuadtreeBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField Field(MoonDescriptor());
	const FVector3d Corner(1.0, 1.0, 1.0);

	{
		FCoverageSim Sim(Field);
		Sim.Lod.MaxNodes = 12;
		if (RunDescent(*this, Sim, Corner, TEXT("MaxNodes=12"), 12))
		{
			TestTrue(TEXT("budget blocked splits instead of opening holes"), Sim.Tree.GetStats().BudgetBlocked > 0);
		}
	}
	{
		FCoverageSim Sim(Field);
		Sim.BuildsPerFrame = 1;
		RunDescent(*this, Sim, Corner, TEXT("one build per frame"), Sim.Lod.MaxNodes);
	}
	{
		FCoverageSim Sim(Field);
		Sim.bOnlyRoots = true;
		if (RunDescent(*this, Sim, Corner, TEXT("children never ready"), Sim.Lod.MaxNodes))
		{
			TestEqual(TEXT("only the six roots are drawn while no child is ready"), Sim.DrawSet.Num(), 6);
			TestTrue(TEXT("unready children never split further"), Sim.Tree.NumNodes() <= 6 + 6 * 4);
		}
	}
	{
		FCoverageSim Sim(Field);
		Sim.DenyChildIndex = 3;
		if (RunDescent(*this, Sim, Corner, TEXT("child 3 denied"), Sim.Lod.MaxNodes))
		{
			bool bNoDenied = true;
			for (const FPlanetTerrainNextNodeId& Id : Sim.DrawSet)
			{
				bNoDenied &= Id.Level == 0 || Id.ChildIndex() != 3;
			}
			TestTrue(TEXT("denied children are never drawn"), bNoDenied);
			// Every split loses one child, so no parent is ever fully covered: the roots must stay.
			TestEqual(TEXT("parents of a denied child keep covering (roots only)"), Sim.MaxDrawnLevel(), 0);
			TestTrue(TEXT("ready siblings were still built"), Sim.Resident.Num() > 6);
		}
	}
	{
		FCoverageSim Sim(Field);
		Sim.Lod.bFreeze = true;
		RunDescent(*this, Sim, Corner, TEXT("frozen LOD"), 6);
		TestEqual(TEXT("frozen tree never splits"), Sim.Tree.NumNodes(), 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextLodProfileTest,
	"Singularity.PlanetTerrainNext.R2.LodProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextLodProfileTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextHeightField Field(MoonDescriptor());
	for (const double Target : {2.0, 4.0, 8.0})
	{
		for (const double Altitude : {1.0e5, 1.0e4, 1.0e3, 100.0, 10.0})
		{
			FCoverageSim Sim(Field);
			Sim.Lod.MaxNodes = 100000;
			Sim.Lod.TargetPixelError = Target;
			Sim.BuildsPerFrame = 100000;
			for (int32 Frame = 0; Frame < 40; ++Frame)
			{
				Sim.Step(FVector3d(0.3, 0.2, 1.0), Altitude);
			}
			TArray<int32> PerLevel;
			PerLevel.SetNumZeroed(25);
			for (const FPlanetTerrainNextNodeId& Id : Sim.DrawSet)
			{
				++PerLevel[Id.Level];
			}
			FString Levels;
			for (int32 Level = 0; Level < PerLevel.Num(); ++Level)
			{
				if (PerLevel[Level] > 0)
				{
					Levels += FString::Printf(TEXT(" L%d:%d"), Level, PerLevel[Level]);
				}
			}
			AddInfo(FString::Printf(TEXT("LodProfile target %.0f px alt %.0f m: nodes %d drawn %d deepest L%d |%s"),
				Target, Altitude, Sim.Tree.NumNodes(), Sim.DrawSet.Num(), Sim.MaxDrawnLevel(), *Levels));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextPatchTest,
	"Singularity.PlanetTerrainNext.R2.Patch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextPatchTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextHeightField Field(MoonDescriptor());
	const double R = Field.GetRadiusMeters();
	const TArray<int32>& Indices = PlanetTerrainNextPatch::Indices();

	TestEqual(TEXT("shared index count"), Indices.Num(), GridN * GridN * 6 + 4 * GridN * 6);
	bool bInRange = true;
	for (const int32 Index : Indices)
	{
		bInRange &= Index >= 0 && Index < VertexCount;
	}
	TestTrue(TEXT("indices address the patch vertex buffer"), bInRange);

	{
		TArray<double> Times;
		for (int32 Index = 0; Index < 64; ++Index)
		{
			const int32 Level = 4 + Index % 14;
			const uint32 Mid = (1u << Level) / 2 + Index;
			FPlanetTerrainNextPatchData Patch;
			const double Start = FPlatformTime::Seconds();
			Build(Field, FPlanetTerrainNextNodeId(static_cast<uint8>(Index % 6), static_cast<uint8>(Level), Mid % (1u << Level), Mid % (1u << Level)), 10.0, Patch);
			Times.Add((FPlatformTime::Seconds() - Start) * 1000.0);
		}
		Times.Sort();
		AddInfo(FString::Printf(TEXT("CPU patch build (%d verts): median %.3f ms, p95 %.3f ms, max %.3f ms"),
			VertexCount, Times[Times.Num() / 2], Times[FMath::CeilToInt(0.95 * Times.Num()) - 1], Times.Last()));
	}

	// Heap-allocate patches: TMap rehash must not invalidate references held across GetPatch calls.
	TMap<FPlanetTerrainNextNodeId, TUniquePtr<FPlanetTerrainNextPatchData>> Cache;
	auto GetPatch = [&](const FPlanetTerrainNextNodeId& Id) -> const FPlanetTerrainNextPatchData&
	{
		if (TUniquePtr<FPlanetTerrainNextPatchData>* Found = Cache.Find(Id))
		{
			return **Found;
		}
		TUniquePtr<FPlanetTerrainNextPatchData> Patch = MakeUnique<FPlanetTerrainNextPatchData>();
		Build(Field, Id, 2.0 * Field.GeometricErrorMeters(VertexSpacingMeters(R, FMath::Max(Id.Level - 2, 0))) + 1.0, *Patch);
		const FPlanetTerrainNextPatchData& Ref = *Patch;
		Cache.Add(Id, MoveTemp(Patch));
		return Ref;
	};
	auto WorldM = [](const FPlanetTerrainNextPatchData& Patch, int32 Vertex)
	{
		return Patch.OriginM + FVector3d(Patch.VerticesCm[Vertex]) / CmPerMeter;
	};

	// Winding and normals for every face and a few levels.
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (const int32 Level : {0, 3, 9})
		{
			const uint32 Mid = (1u << Level) / 2;
			const FPlanetTerrainNextPatchData& Patch = GetPatch(FPlanetTerrainNextNodeId(static_cast<uint8>(Face), static_cast<uint8>(Level), Mid, Mid));
			TestEqual(TEXT("vertex count"), Patch.VerticesCm.Num(), VertexCount);
			TestTrue(TEXT("patch-local vertices stay small"),
				Patch.VerticesCm[0].Length() < 2.0 * VertexSpacingMeters(R, Level) * GridN * CmPerMeter + 1.0e6);

			bool bWinding = true;
			for (int32 Tri = 0; Tri < GridN * GridN * 2 && bWinding; ++Tri)
			{
				const FVector3d A = WorldM(Patch, Indices[Tri * 3 + 0]);
				const FVector3d B = WorldM(Patch, Indices[Tri * 3 + 1]);
				const FVector3d C = WorldM(Patch, Indices[Tri * 3 + 2]);
				// UE treats (B - C) x (A - C) as the front normal, i.e. -(B - A) x (C - A).
				const FVector3d Front = -FVector3d::CrossProduct(B - A, C - A);
				bWinding = FVector3d::DotProduct(Front, (A + B + C).GetSafeNormal()) > 0.0;
			}
			TestTrue(FString::Printf(TEXT("face %d L%d triangles face outward in UE winding"), Face, Level), bWinding);

			bool bNormals = true;
			for (int32 Vertex = 0; Vertex < GridVertexCount && bNormals; ++Vertex)
			{
				bNormals = FVector3d::DotProduct(FVector3d(Patch.Normals[Vertex]), WorldM(Patch, Vertex).GetSafeNormal()) > 0.3;
			}
			TestTrue(FString::Printf(TEXT("face %d L%d normals point away from the planet"), Face, Level), bNormals);
		}
	}

	// Same-face neighbors at equal level share edge vertices exactly.
	{
		const FPlanetTerrainNextPatchData& Left = GetPatch(FPlanetTerrainNextNodeId(4, 6, 20, 33));
		const FPlanetTerrainNextPatchData& Right = GetPatch(FPlanetTerrainNextNodeId(4, 6, 21, 33));
		const FPlanetTerrainNextPatchData& Top = GetPatch(FPlanetTerrainNextNodeId(4, 6, 20, 34));
		double MaxGapCm = 0.0;
		for (int32 K = 0; K <= GridN; ++K)
		{
			MaxGapCm = FMath::Max(MaxGapCm, (WorldM(Left, K * Row + GridN) - WorldM(Right, K * Row)).Length() * CmPerMeter);
			MaxGapCm = FMath::Max(MaxGapCm, (WorldM(Left, GridN * Row + K) - WorldM(Top, K)).Length() * CmPerMeter);
		}
		TestTrue(FString::Printf(TEXT("same-face equal-level seams close (max %.6f cm)"), MaxGapCm), MaxGapCm < 0.01);
	}

	// Cross-face equal-level seams: every boundary vertex of every face has a twin on the other face.
	{
		constexpr int32 Level = 2;
		constexpr uint32 N = 1u << Level;
		double MaxGapCm = 0.0;
		int32 Unmatched = 0;
		for (int32 Face = 0; Face < 6; ++Face)
		{
			for (uint32 Y = 0; Y < N; ++Y)
			{
				for (uint32 X = 0; X < N; ++X)
				{
					if (X != 0 && Y != 0 && X != N - 1 && Y != N - 1)
					{
						continue;
					}
					const FPlanetTerrainNextNodeId Id(static_cast<uint8>(Face), Level, X, Y);
					const FPlanetTerrainNextPatchData& Patch = GetPatch(Id);
					for (int32 J = 0; J <= GridN; ++J)
					{
						for (int32 I = 0; I <= GridN; ++I)
						{
							const bool bFaceEdge = (I == 0 && X == 0) || (I == GridN && X == N - 1) || (J == 0 && Y == 0) || (J == GridN && Y == N - 1);
							if (!bFaceEdge)
							{
								continue;
							}
							const FVector3d P = WorldM(Patch, J * Row + I);
							const FVector3d Dir = VertexDirection(Id, I, J);
							for (int32 OtherFace = 0; OtherFace < 6; ++OtherFace)
							{
								double U = 0.0;
								double V = 0.0;
								if (OtherFace == Face || !TryUnprojectFace(Dir, OtherFace, U, V))
								{
									continue;
								}
								const FPlanetTerrainNextNodeId OtherId(
									static_cast<uint8>(OtherFace), Level,
									FMath::Min(static_cast<uint32>(U * N), N - 1),
									FMath::Min(static_cast<uint32>(V * N), N - 1));
								const FPlanetTerrainNextPatchData& OtherPatch = GetPatch(OtherId);
								double Best = TNumericLimits<double>::Max();
								for (int32 Vertex = 0; Vertex < GridVertexCount; ++Vertex)
								{
									Best = FMath::Min(Best, (WorldM(OtherPatch, Vertex) - P).Length());
								}
								MaxGapCm = FMath::Max(MaxGapCm, Best * CmPerMeter);
								Unmatched += Best * CmPerMeter > 1.0 ? 1 : 0;
							}
						}
					}
				}
			}
		}
		TestEqual(TEXT("cross-face seam vertices without a twin within 1 cm"), Unmatched, 0);
		AddInfo(FString::Printf(TEXT("Cross-face L%d seam max gap %.6f cm"), Level, MaxGapCm));
	}

	// L / L+1 T-junctions: the higher side's skirt reaches below the other side's edge.
	{
		FRandomStream Random(5150);
		int32 Uncovered = 0;
		double WorstRatio = 0.0;
		for (int32 Trial = 0; Trial < 48; ++Trial)
		{
			const int32 Level = 2 + Trial % 9;
			const uint32 N = 1u << Level;
			const uint8 Face = static_cast<uint8>(Random.RandRange(0, 5));
			const uint32 X = static_cast<uint32>(Random.RandRange(0, static_cast<int32>(N) - 2));
			const uint32 Y = static_cast<uint32>(Random.RandRange(0, static_cast<int32>(N) - 1));
			const FPlanetTerrainNextNodeId CoarseId(Face, static_cast<uint8>(Level), X, Y);
			const FPlanetTerrainNextNodeId Neighbor(Face, static_cast<uint8>(Level), X + 1, Y);
			const FPlanetTerrainNextPatchData& Coarse = GetPatch(CoarseId);
			for (const int32 ChildIndex : {0, 2})
			{
				const FPlanetTerrainNextNodeId FineId = Neighbor.Child(ChildIndex);
				const FPlanetTerrainNextPatchData& Fine = GetPatch(FineId);
				const int32 JOffset = ChildIndex == 2 ? GridN / 2 : 0;
				for (int32 K = 0; K <= GridN; ++K)
				{
					// Fine edge vertex K lies at coarse edge parameter (JOffset + K / 2).
					const double CoarseParam = JOffset + 0.5 * K;
					const int32 J0 = FMath::FloorToInt(CoarseParam);
					const int32 J1 = FMath::Min(J0 + 1, GridN);
					const double Alpha = CoarseParam - J0;
					const double CoarseH = FMath::Lerp(
						WorldM(Coarse, J0 * Row + GridN).Length(),
						WorldM(Coarse, J1 * Row + GridN).Length(), Alpha) - R;
					const double FineH = WorldM(Fine, K * Row).Length() - R;
					const double Gap = FMath::Abs(FineH - CoarseH);
					const double Skirt = FineH > CoarseH ? Fine.SkirtDepthM : Coarse.SkirtDepthM;
					WorstRatio = FMath::Max(WorstRatio, Gap / Skirt);
					Uncovered += Gap > Skirt ? 1 : 0;
				}
			}
		}
		TestEqual(TEXT("L/L+1 seam gaps deeper than the covering skirt"), Uncovered, 0);
		AddInfo(FString::Printf(TEXT("Worst L/L+1 gap / skirt depth = %.3f"), WorstRatio));
	}
	return true;
}

#endif
