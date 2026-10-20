#include "PlanetTerrainNextDetailTile.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"

FPlanetTerrainNextDetailTileId FPlanetTerrainNextDetailTileId::MakeRoot(
	int64 PlanetId,
	uint64 GeneratorHash,
	uint8 Face,
	uint16 Resolution)
{
	FPlanetTerrainNextDetailTileId Id;
	Id.PlanetId = PlanetId;
	Id.GeneratorHash = GeneratorHash;
	Id.Face = Face;
	Id.Level = 0;
	Id.X = 0;
	Id.Y = 0;
	Id.Resolution = Resolution;
	Id.FormatVersion = 1;
	return Id;
}

namespace PlanetTerrainNextDetailTile
{
namespace
{
FLinearColor AlbedoFromTerrain(
	double HeightM,
	double Slope01,
	const FPlanetTerrainNextHeightComponents& Comp)
{
	// Conservative lunar regolith: elevation + crater bowl darkening + slope brightening.
	const float Bowl = static_cast<float>(FMath::Clamp(-Comp.Craters / 800.0, 0.0, 1.0));
	const float Elev = static_cast<float>(FMath::Clamp((HeightM + 2000.0) / 6000.0, 0.0, 1.0));
	const float Slope = static_cast<float>(FMath::Clamp(Slope01, 0.0, 1.0));
	const float Grey = FMath::Clamp(0.42f + 0.18f * Elev - 0.22f * Bowl + 0.12f * Slope, 0.12f, 0.72f);
	const float Cool = FMath::Clamp(Grey - 0.03f * Bowl, 0.08f, 0.7f);
	return FLinearColor(Grey * 1.02f, Grey, Cool, 1.0f);
}
} // namespace

void Bake(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextDetailTileId& Id,
	FPlanetTerrainNextDetailTile& Out)
{
	Out = FPlanetTerrainNextDetailTile();
	Out.Id = Id;
	const int32 Res = FMath::Clamp(static_cast<int32>(Id.Resolution), 8, 2048);
	const double Radius = HeightField.GetRadiusMeters();
	const double FaceArcM = (UE_DOUBLE_PI * 0.5) * Radius; // ~90° face
	const double TexelM = FaceArcM / static_cast<double>(Res);
	// Nyquist-ish band limit for this texel size (antialias distant noise).
	const double MinWL = FMath::Max(TexelM * 2.0, 1.0);

	const double LevelScale = 1.0 / static_cast<double>(1ull << Id.Level);
	const double U0 = static_cast<double>(Id.X) * LevelScale;
	const double V0 = static_cast<double>(Id.Y) * LevelScale;
	const double U1 = U0 + LevelScale;
	const double V1 = V0 + LevelScale;
	const double Du = (U1 - U0) / static_cast<double>(Res);
	const double Dv = (V1 - V0) / static_cast<double>(Res);
	constexpr double Eps = 0.45; // fraction of a texel for finite differences

	Out.Albedo.SetNumUninitialized(Res * Res);
	Out.Normal.SetNumUninitialized(Res * Res);

	auto Position = [&](double U, double V) -> FVector3d
	{
		const FVector3d Dir = PlanetTerrainNext::FacePoint(Id.Face, U, V);
		const double H = HeightField.HeightAt(Dir, MinWL);
		return Dir * (Radius + H);
	};

	for (int32 J = 0; J < Res; ++J)
	{
		for (int32 I = 0; I < Res; ++I)
		{
			const double U = U0 + (static_cast<double>(I) + 0.5) * Du;
			const double V = V0 + (static_cast<double>(J) + 0.5) * Dv;
			const FVector3d Dir = PlanetTerrainNext::FacePoint(Id.Face, U, V);
			const FPlanetTerrainNextHeightComponents Comp = HeightField.EvaluateComponents(Dir, MinWL);
			const double H = Comp.Total();
			const FVector3d P = Dir * (Radius + H);

			const FVector3d Pu = Position(U + Eps * Du, V) - Position(U - Eps * Du, V);
			const FVector3d Pv = Position(U, V + Eps * Dv) - Position(U, V - Eps * Dv);
			FVector3d NDetail = FVector3d::CrossProduct(Pu, Pv).GetSafeNormal();
			if (NDetail.IsNearlyZero() || FVector3d::DotProduct(NDetail, Dir) < 0.0)
			{
				NDetail = Dir;
			}

			// Tangent frame from face UV (matches patch grid orientation).
			FVector3d T = Pu.GetSafeNormal();
			if (T.IsNearlyZero())
			{
				T = FVector3d::CrossProduct(Dir, FMath::Abs(Dir.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX()).GetSafeNormal();
			}
			FVector3d B = FVector3d::CrossProduct(Dir, T).GetSafeNormal();
			T = FVector3d::CrossProduct(B, Dir).GetSafeNormal();

			const FVector3d NTangent(
				FVector3d::DotProduct(NDetail, T),
				FVector3d::DotProduct(NDetail, B),
				FVector3d::DotProduct(NDetail, Dir));
			const FVector3d NEnc = NTangent.GetSafeNormal();

			const double Slope01 = 1.0 - FMath::Clamp(FVector3d::DotProduct(NDetail, Dir), 0.0, 1.0);
			const FLinearColor Albedo = AlbedoFromTerrain(H, Slope01, Comp);

			const int32 Index = J * Res + I;
			Out.Albedo[Index] = Albedo.ToFColor(true);
			Out.Normal[Index] = FLinearColor(
				static_cast<float>(NEnc.X * 0.5 + 0.5),
				static_cast<float>(NEnc.Y * 0.5 + 0.5),
				static_cast<float>(NEnc.Z * 0.5 + 0.5),
				1.0f).ToFColor(false);
		}
	}

	Out.bValid = Out.Albedo.Num() == Res * Res && Out.Normal.Num() == Res * Res;
}
}
