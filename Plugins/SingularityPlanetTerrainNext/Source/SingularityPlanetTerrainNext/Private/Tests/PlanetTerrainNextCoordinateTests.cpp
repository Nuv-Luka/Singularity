#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextStaticShell.h"
#include <type_traits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextScaleTest,
	"Singularity.PlanetTerrainNext.Scale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextScaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("LWC FVector is double"), std::is_same_v<FVector::FReal, double>);
	TestEqual(TEXT("cm per meter"), PlanetTerrainNext::CmPerMeter, 100.0);
	TestEqual(TEXT("1737 km moon in uu"), PlanetTerrainNext::LunarRadiusUu, 173700000.0);
	TestEqual(TEXT("descriptor default matches"), PlanetTerrainNext::MetersToUu(1737000.0), 173700000.0);

	FPlanetTerrainNextFaceGeometry Face;
	BuildPlanetTerrainNextFaceMesh(0, 16, PlanetTerrainNext::LunarRadiusMeters, Face);
	TestEqual(TEXT("16x16 grid has 17^2 verts"), Face.VerticesCm.Num(), 17 * 17);
	TestEqual(TEXT("two tris per cell"), Face.Triangles.Num(), 16 * 16 * 6);

	for (int32 Index = 0; Index < Face.PositionsMeters.Num(); ++Index)
	{
		const double Meters = Face.PositionsMeters[Index].Length();
		const double Uu = Face.VerticesCm[Index].Length();
		if (!TestTrue(TEXT("vertex sits on the radius in meters"), FMath::IsNearlyEqual(Meters, PlanetTerrainNext::LunarRadiusMeters, 1.0e-4)))
		{
			break;
		}
		if (!TestTrue(TEXT("vertex centimeters are radius * 100"), FMath::IsNearlyEqual(Uu, PlanetTerrainNext::LunarRadiusUu, 1.0e-2)))
		{
			break;
		}
	}

	// Match UPlanetTerrainComponent::BuildDistantFallback winding exactly.
	TestEqual(TEXT("first tri matches proven distant-shell winding"), Face.Triangles[0], 0);
	TestEqual(TEXT("first tri row step"), Face.Triangles[1], 17);
	TestEqual(TEXT("first tri +U"), Face.Triangles[2], 1);
	TestEqual(TEXT("second tri"), Face.Triangles[3], 1);
	TestEqual(TEXT("second tri"), Face.Triangles[4], 17);
	TestEqual(TEXT("second tri"), Face.Triangles[5], 18);

	for (int32 Index = 0; Index < Face.Normals.Num(); ++Index)
	{
		if (!TestTrue(TEXT("normals point outward"),
			FVector::DotProduct(Face.Normals[Index], Face.VerticesCm[Index]) > 0.0))
		{
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextFaceCoverageTest,
	"Singularity.PlanetTerrainNext.FaceCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextFaceCoverageTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	for (int32 Face = 0; Face < 6; ++Face)
	{
		int32 RoundFace = -1;
		double U = 0.0;
		double V = 0.0;
		const FVector3d Center = PlanetTerrainNext::FacePoint(Face, 0.5, 0.5);
		PlanetTerrainNext::DirectionToFaceUV(Center, RoundFace, U, V);
		TestEqual(TEXT("face center round-trips"), RoundFace, Face);
		TestTrue(TEXT("center U"), FMath::IsNearlyEqual(U, 0.5, 1.0e-9));
		TestTrue(TEXT("center V"), FMath::IsNearlyEqual(V, 0.5, 1.0e-9));

		const FVector3d Back = PlanetTerrainNext::FacePoint(RoundFace, U, V);
		TestTrue(TEXT("round-trip direction"), (Back - Center).Size() < 1.0e-9);
	}

	TArray<FVector3d> Corners;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (double U : {0.0, 1.0})
		{
			for (double V : {0.0, 1.0})
			{
				const FVector3d Corner = PlanetTerrainNext::FacePoint(Face, U, V);
				bool bSeen = false;
				for (const FVector3d& Existing : Corners)
				{
					if ((Existing - Corner).Size() < 1.0e-8)
					{
						bSeen = true;
						break;
					}
				}
				if (!bSeen)
				{
					Corners.Add(Corner);
				}
			}
		}
	}
	TestEqual(TEXT("cube has 8 corners"), Corners.Num(), 8);

	for (const FVector3d& Corner : Corners)
	{
		int32 Owners = 0;
		for (int32 Face = 0; Face < 6; ++Face)
		{
			double U = 0.0;
			double V = 0.0;
			if (PlanetTerrainNext::TryUnprojectFace(Corner, Face, U, V))
			{
				const FVector3d Rebuilt = PlanetTerrainNext::FacePoint(Face, U, V);
				if ((Rebuilt - Corner).Size() < 1.0e-7)
				{
					++Owners;
				}
			}
		}
		TestEqual(TEXT("each corner is on 3 faces"), Owners, 3);
	}

	const double Samples[] = {0.25, 0.5, 0.75};
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			for (double T : Samples)
			{
				double U = 0.5;
				double V = 0.5;
				switch (Edge)
				{
				case 0: U = T; V = 0.0; break;
				case 1: U = T; V = 1.0; break;
				case 2: U = 0.0; V = T; break;
				default: U = 1.0; V = T; break;
				}
				const FVector3d Point = PlanetTerrainNext::FacePoint(Face, U, V);
				int32 Owners = 0;
				for (int32 Other = 0; Other < 6; ++Other)
				{
					double OU = 0.0;
					double OV = 0.0;
					if (!PlanetTerrainNext::TryUnprojectFace(Point, Other, OU, OV))
					{
						continue;
					}
					const bool bOnBoundary = OU < 1.0e-6 || OV < 1.0e-6 || OU > 1.0 - 1.0e-6 || OV > 1.0 - 1.0e-6;
					const FVector3d Rebuilt = PlanetTerrainNext::FacePoint(Other, OU, OV);
					if (bOnBoundary && (Rebuilt - Point).Size() < 1.0e-7)
					{
						++Owners;
					}
				}
				if (!TestEqual(TEXT("edge interior belongs to exactly two faces"), Owners, 2))
				{
					return false;
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextPrecisionTest,
	"Singularity.PlanetTerrainNext.Precision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextPrecisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FVector3d PlanetCenter(PlanetTerrainNext::AstronomicalUnitMeters, -2.0 * PlanetTerrainNext::AstronomicalUnitMeters, 0.5 * PlanetTerrainNext::AstronomicalUnitMeters);
	const FVector3d Radial = PlanetTerrainNext::FacePoint(4, 0.5, 0.5);
	const FVector3d Surface = PlanetCenter + Radial * PlanetTerrainNext::LunarRadiusMeters;
	const FVector3d Camera = Surface + Radial * 10.0;
	const FVector3d ExpectedCm = (Surface - Camera) * PlanetTerrainNext::CmPerMeter;

	const FVector3f GpuRelative = PlanetTerrainNext::CameraRelativeGpuUu(Surface, Camera);
	const FVector FloatFirst = PlanetTerrainNext::FloatFirstRelativeUu(Surface, Camera);
	const FVector3d GpuCm(GpuRelative.X, GpuRelative.Y, GpuRelative.Z);

	const double GpuErrorCm = (GpuCm - ExpectedCm).Length();
	const double FloatErrorCm = (FVector3d(FloatFirst.X, FloatFirst.Y, FloatFirst.Z) - ExpectedCm).Length();

	TestTrue(TEXT("double-then-float camera delta stays within a millimeter at 1 AU"), GpuErrorCm < 0.1);
	TestTrue(TEXT("float-first subtraction drops the 10 m surface gap"), FloatErrorCm > 500.0);

	const double AuCentimeters = PlanetTerrainNext::AstronomicalUnitMeters * PlanetTerrainNext::CmPerMeter;
	const double QuantizedAu = static_cast<double>(static_cast<float>(AuCentimeters));
	TestTrue(TEXT("a float cannot store 1 AU in centimeters"), FMath::Abs(QuantizedAu - AuCentimeters) > 100000.0);

	const FVector PlanetCenterUu(
		PlanetCenter.X * PlanetTerrainNext::CmPerMeter,
		PlanetCenter.Y * PlanetTerrainNext::CmPerMeter,
		PlanetCenter.Z * PlanetTerrainNext::CmPerMeter);
	const FVector SurfaceUu = PlanetCenterUu + FVector(Radial * PlanetTerrainNext::LunarRadiusUu);
	const FVector3d Local = PlanetTerrainNext::PlanetLocalMeters(SurfaceUu, PlanetCenterUu);
	TestTrue(TEXT("world subtraction recovers planet-local meters"), (Local - Radial * PlanetTerrainNext::LunarRadiusMeters).Size() < 1.0e-3);
	return true;
}

#endif
