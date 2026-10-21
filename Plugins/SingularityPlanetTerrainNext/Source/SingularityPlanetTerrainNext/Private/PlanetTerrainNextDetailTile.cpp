#include "PlanetTerrainNextDetailTile.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
	const FPlanetTerrainNextHeightComponents& Comp,
	const FPlanetTerrainNextSurfaceSample& Geology)
{
	if (Geology.MaterialId != 0 || Geology.MareWeight > 0.0 || Geology.EjectaWeight > 0.0 || Geology.ImpactMeltWeight > 0.0)
	{
		// Geological composition is blended in linear space. These are art-directed
		// lunar starting points: bright anorthositic highlands, dark basaltic mare,
		// brighter ejecta, and patchy intermediate impact melt.
		const FLinearColor Highland(0.58f, 0.55f, 0.50f, 1.0f);
		const FLinearColor Mare(0.20f, 0.21f, 0.22f, 1.0f);
		const FLinearColor Ejecta(0.70f, 0.67f, 0.61f, 1.0f);
		const FLinearColor Melt(0.40f, 0.39f, 0.37f, 1.0f);
		FLinearColor Result = Highland * static_cast<float>(Geology.HighlandWeight)
			+ Mare * static_cast<float>(Geology.MareWeight)
			+ Ejecta * static_cast<float>(Geology.EjectaWeight)
			+ Melt * static_cast<float>(Geology.ImpactMeltWeight);

		const float Slope = static_cast<float>(FMath::Clamp(Slope01, 0.0, 1.0));
		const float Fresh = static_cast<float>(FMath::Clamp(Geology.Freshness, 0.0, 1.0));
		Result *= FMath::Lerp(0.94f, 1.06f, Slope);
		Result = FMath::Lerp(Result, Ejecta, 0.10f * Fresh);
		Result.A = 1.0f;
		return Result.GetClamped();
	}

	// Conservative lunar regolith: elevation + crater bowl darkening + slope brightening.
	const float Bowl = static_cast<float>(FMath::Clamp(-Comp.Craters / 800.0, 0.0, 1.0));
	const float Elev = static_cast<float>(FMath::Clamp((HeightM + 2000.0) / 6000.0, 0.0, 1.0));
	const float Slope = static_cast<float>(FMath::Clamp(Slope01, 0.0, 1.0));
	const float Grey = FMath::Clamp(0.42f + 0.18f * Elev - 0.22f * Bowl + 0.12f * Slope, 0.12f, 0.72f);
	const float Cool = FMath::Clamp(Grey - 0.03f * Bowl, 0.08f, 0.7f);
	return FLinearColor(Grey * 1.02f, Grey, Cool, 1.0f);
}

void BuildMipChain(const TArray<FColor>& Base, int32 Resolution, bool bNormal, TArray<TArray<FColor>>& OutMips)
{
	OutMips.Reset();
	TArray<FColor> Previous = Base;
	int32 PreviousSize = Resolution;
	while (PreviousSize > 1)
	{
		const int32 Size = FMath::Max(1, PreviousSize / 2);
		TArray<FColor>& Mip = OutMips.AddDefaulted_GetRef();
		Mip.SetNumUninitialized(Size * Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 X0 = FMath::Min(X * 2, PreviousSize - 1);
				const int32 X1 = FMath::Min(X0 + 1, PreviousSize - 1);
				const int32 Y0 = FMath::Min(Y * 2, PreviousSize - 1);
				const int32 Y1 = FMath::Min(Y0 + 1, PreviousSize - 1);
				const FColor Samples[4] = {
					Previous[Y0 * PreviousSize + X0], Previous[Y0 * PreviousSize + X1],
					Previous[Y1 * PreviousSize + X0], Previous[Y1 * PreviousSize + X1]};
				FColor Result;
				if (bNormal)
				{
					FVector3f N = FVector3f::ZeroVector;
					uint32 Alpha = 0;
					for (const FColor& Sample : Samples)
					{
						N += FVector3f(Sample.R / 127.5f - 1.0f, Sample.G / 127.5f - 1.0f, Sample.B / 127.5f - 1.0f);
						Alpha += Sample.A;
					}
					if (!N.Normalize())
					{
						N = FVector3f(0.0f, 0.0f, 1.0f);
					}
					Result = FColor(
						static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((N.X * 0.5f + 0.5f) * 255.0f), 0, 255)),
						static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((N.Y * 0.5f + 0.5f) * 255.0f), 0, 255)),
						static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((N.Z * 0.5f + 0.5f) * 255.0f), 0, 255)),
						static_cast<uint8>(Alpha / 4));
				}
				else
				{
					FLinearColor C = FLinearColor::Black;
					uint32 Alpha = 0;
					for (const FColor& Sample : Samples)
					{
						C += FLinearColor(Sample);
						Alpha += Sample.A;
					}
					C /= 4.0f;
					Result = C.ToFColor(true);
					Result.A = static_cast<uint8>(Alpha / 4);
				}
				Mip[Y * Size + X] = Result;
			}
		}
		Previous = Mip;
		PreviousSize = Size;
	}
}
} // namespace

void Bake(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextDetailTileId& Id,
	FPlanetTerrainNextDetailTile& Out)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_DetailTileBake);
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

	// Cache center heights plus a one-texel border. Finite-difference samples retain
	// the previous ±0.45-texel offsets and bilinearly sample this deterministic cache.
	const int32 SampleStride = Res + 2;
	TArray<double> HeightSamples;
	HeightSamples.SetNumUninitialized(SampleStride * SampleStride);
	for (int32 J = -1; J <= Res; ++J)
	{
		for (int32 I = -1; I <= Res; ++I)
		{
			const double U = U0 + (static_cast<double>(I) + 0.5) * Du;
			const double V = V0 + (static_cast<double>(J) + 0.5) * Dv;
			const FVector3d Dir = PlanetTerrainNext::FacePoint(Id.Face, U, V);
			HeightSamples[(J + 1) * SampleStride + (I + 1)] = HeightField.HeightAt(Dir, MinWL);
		}
	}

	auto CachedHeight = [&HeightSamples, SampleStride, Res](double X, double Y)
	{
		const double CX = FMath::Clamp(X, 0.0, static_cast<double>(Res + 1));
		const double CY = FMath::Clamp(Y, 0.0, static_cast<double>(Res + 1));
		const int32 X0 = FMath::FloorToInt(CX);
		const int32 Y0 = FMath::FloorToInt(CY);
		const int32 X1 = FMath::Min(X0 + 1, Res + 1);
		const int32 Y1 = FMath::Min(Y0 + 1, Res + 1);
		const double TX = CX - X0;
		const double TY = CY - Y0;
		const double A = FMath::Lerp(HeightSamples[Y0 * SampleStride + X0], HeightSamples[Y0 * SampleStride + X1], TX);
		const double B = FMath::Lerp(HeightSamples[Y1 * SampleStride + X0], HeightSamples[Y1 * SampleStride + X1], TX);
		return FMath::Lerp(A, B, TY);
	};
	auto CachedPosition = [&](double SampleX, double SampleY)
	{
		const double U = U0 + (SampleX - 0.5) * Du;
		const double V = V0 + (SampleY - 0.5) * Dv;
		const FVector3d Dir = PlanetTerrainNext::FacePoint(Id.Face, U, V);
		return Dir * (Radius + CachedHeight(SampleX, SampleY));
	};

	Out.Albedo.SetNumUninitialized(Res * Res);
	Out.Normal.SetNumUninitialized(Res * Res);

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

			const double SampleX = static_cast<double>(I + 1);
			const double SampleY = static_cast<double>(J + 1);
			const FVector3d Pu = CachedPosition(SampleX + Eps, SampleY) - CachedPosition(SampleX - Eps, SampleY);
			const FVector3d Pv = CachedPosition(SampleX, SampleY + Eps) - CachedPosition(SampleX, SampleY - Eps);
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
			const FPlanetTerrainNextSurfaceSample Geology = HeightField.SampleGeology(Dir, TexelM);
			const FLinearColor Albedo = AlbedoFromTerrain(H, Slope01, Comp, Geology);

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
	if (Out.bValid)
	{
		BuildMipChain(Out.Albedo, Res, false, Out.AlbedoMips);
		BuildMipChain(Out.Normal, Res, true, Out.NormalMips);
	}
}
}
