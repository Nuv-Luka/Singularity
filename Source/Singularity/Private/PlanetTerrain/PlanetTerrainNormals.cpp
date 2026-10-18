#include "PlanetTerrain/PlanetTerrainNormals.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"

void FPlanetTerrainNormals::ComputeFromHeightField(
	const FPlanetTerrainHeight& Height,
	uint8 Face,
	double U0, double V0, double U1, double V1,
	int32 Resolution,
	TFunctionRef<double(const FVector3d& UnitDir)> HeightAtDir,
	TArray<FVector>& OutNormals)
{
	const int32 Res = FMath::Max(2, Resolution);
	const int32 N = Res + 1;
	OutNormals.SetNum(N * N);

	const double Radius = Height.GetRadiusMeters();
	const double Du = (U1 - U0) / static_cast<double>(Res);
	const double Dv = (V1 - V0) / static_cast<double>(Res);
	// Angular step from approximate arc at mid-tile (halo = 1 sample).
	const double ArcU = FMath::Max(1e-9, FMath::Abs(Du) * 0.5 * PI * Radius);
	const double Ang = ArcU / FMath::Max(1.0, Radius);

	auto SurfacePos = [&](const FVector3d& Dir) -> FVector3d
	{
		const FVector3d D = Dir.GetSafeNormal();
		return D * (Radius + HeightAtDir(D));
	};

	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const double U = U0 + static_cast<double>(I) * Du;
			const double V = V0 + static_cast<double>(J) * Dv;
			const FVector3d Dir = PlanetTerrainFacePoint(Face, U, V);

			// Prefer UV halo on the same face (matches neighbor tile verts).
			// Fall back to spherical tangent halo when UV leaves [0,1] (cube edges).
			FVector3d PLeft, PRight, PDown, PUp;
			const double UL = U - Du;
			const double UR = U + Du;
			const double VD = V - Dv;
			const double VU = V + Dv;
			if (UL >= 0.0 && UR <= 1.0 && VD >= 0.0 && VU <= 1.0)
			{
				PLeft = SurfacePos(PlanetTerrainFacePoint(Face, UL, V));
				PRight = SurfacePos(PlanetTerrainFacePoint(Face, UR, V));
				PDown = SurfacePos(PlanetTerrainFacePoint(Face, U, VD));
				PUp = SurfacePos(PlanetTerrainFacePoint(Face, U, VU));
			}
			else
			{
				FVector3d Tu = FVector3d::CrossProduct(FVector3d(0.0, 1.0, 0.0), Dir);
				if (Tu.SizeSquared() < 1e-20)
				{
					Tu = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Dir);
				}
				Tu.Normalize();
				const FVector3d Tv = FVector3d::CrossProduct(Dir, Tu).GetSafeNormal();
				PLeft = SurfacePos((Dir - Tu * Ang).GetSafeNormal());
				PRight = SurfacePos((Dir + Tu * Ang).GetSafeNormal());
				PDown = SurfacePos((Dir - Tv * Ang).GetSafeNormal());
				PUp = SurfacePos((Dir + Tv * Ang).GetSafeNormal());
			}

			FVector3d Nrm = FVector3d::CrossProduct(PRight - PLeft, PUp - PDown).GetSafeNormal();
			if (FVector3d::DotProduct(Nrm, Dir) < 0.0)
			{
				Nrm *= -1.0;
			}
			OutNormals[J * N + I] = FVector(Nrm.X, Nrm.Y, Nrm.Z);
		}
	}
}

void FPlanetTerrainNormals::ComputeFine(
	const FPlanetTerrainHeight& Height,
	uint8 Face,
	double U0, double V0, double U1, double V1,
	int32 Resolution,
	double SampleSpacingM,
	TArray<FVector>& OutNormals)
{
	const double Spacing = FMath::Max(0.0, SampleSpacingM);
	ComputeFromHeightField(
		Height, Face, U0, V0, U1, V1, Resolution,
		[&Height, Spacing](const FVector3d& Dir) -> double
		{
			return Height.EvaluateHeightAtSpacing(Dir, Spacing);
		},
		OutNormals);
}

void FPlanetTerrainNormals::ComputeMorphed(
	const FPlanetTerrainHeight& Height,
	uint8 Face,
	double U0, double V0, double U1, double V1,
	int32 Resolution,
	double FineSpacingM,
	double CoarseSpacingM,
	float Morph,
	TArray<FVector>& OutNormals)
{
	const float M = FMath::Clamp(Morph, 0.0f, 1.0f);
	ComputeFromHeightField(
		Height, Face, U0, V0, U1, V1, Resolution,
		[&Height, FineSpacingM, CoarseSpacingM, M](const FVector3d& Dir) -> double
		{
			double Hf = 0.0, Hc = 0.0;
			Height.EvaluateHeight2(Dir, FineSpacingM, CoarseSpacingM, Hf, Hc);
			return FMath::Lerp(Hf, Hc, static_cast<double>(M));
		},
		OutNormals);
}
