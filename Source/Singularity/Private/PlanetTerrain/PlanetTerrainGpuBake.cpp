#include "PlanetTerrain/PlanetTerrainGpuBake.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"
#include "PlanetTerrain/PlanetTerrainNormals.h"
#include "SingularityPlanetTerrainBake.h"

bool FPlanetTerrainGpuBaker::IsGpuComputeAvailable()
{
	return SingularityPlanetTerrainIsGpuBakeAvailable() && IsInGameThread();
}

bool FPlanetTerrainGpuBaker::TryBakeHeightsCompute(
	const FPlanetTerrainHeight& Height,
	uint8 Face,
	double U0, double V0, double U1, double V1,
	int32 Resolution,
	float FineSpacingM,
	float CoarseSpacingM,
	TArray<float>& OutFine,
	TArray<float>& OutCoarse)
{
	if (!IsGpuComputeAvailable())
	{
		return false;
	}

	FSingularityPlanetTerrainBakeUniforms U;
	Height.GetGpuBakeUniforms(
		U.AmplitudeM, U.RadiusM, U.ContinentFreq, U.MountainFreq, U.DetailFreq,
		U.CraterFreq0, U.CraterDensity, U.CraterWeight, U.MountainWeight, U.NoiseSeed);

	return SingularityPlanetTerrainBakeHeights(
		U,
		Face,
		static_cast<float>(U0), static_cast<float>(V0),
		static_cast<float>(U1), static_cast<float>(V1),
		Resolution,
		FineSpacingM,
		CoarseSpacingM,
		OutFine,
		OutCoarse);
}

static void FinishTileFromHeights(
	const FPlanetTerrainHeight& Height,
	const FPlanetTerrainTileKey& Key,
	double U0, double V0, double U1, double V1,
	int32 Res,
	const TArray<float>& Fine,
	const TArray<float>& Coarse,
	FPlanetTerrainTileData& Out)
{
	const int32 N = Res + 1;
	const int32 VertCount = N * N;
	Out.Key = Key;
	Out.Face = Key.Face;
	Out.Level = Key.Level;
	Out.U0 = U0;
	Out.V0 = V0;
	Out.U1 = U1;
	Out.V1 = V1;
	Out.Resolution = Res;
	Out.Directions.SetNum(VertCount);
	Out.HeightFineM = Fine;
	Out.HeightCoarseM = Coarse;
	Out.PositionsLocal.SetNum(VertCount);
	Out.Normals.SetNum(VertCount);
	Out.UVs.SetNum(VertCount);

	const double Radius = Height.GetRadiusMeters();
	const double Du = (U1 - U0) / static_cast<double>(Res);
	const double Dv = (V1 - V0) / static_cast<double>(Res);
	const double Arc = (U1 - U0) * 0.5 * PI * Radius;
	Out.WorldSizeM = FMath::Max(1.0, Arc * 2.0);
	Out.SampleSpacingM = Out.WorldSizeM / static_cast<double>(Res);

	const double Uc = 0.5 * (U0 + U1);
	const double Vc = 0.5 * (V0 + V1);
	Out.CenterLocal = PlanetTerrainFacePoint(Key.Face, Uc, Vc) * Radius;
	const double AnchorH = Height.LodAnchorElevation(PlanetTerrainFacePoint(Key.Face, Uc, Vc));
	Out.CenterSurfaceLocal = PlanetTerrainFacePoint(Key.Face, Uc, Vc) * (Radius + AnchorH);

	TArray<FVector3d> Absolute;
	Absolute.SetNum(VertCount);
	FVector3d OriginSum = FVector3d::ZeroVector;
	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const int32 Idx = J * N + I;
			const double U = U0 + static_cast<double>(I) * Du;
			const double V = V0 + static_cast<double>(J) * Dv;
			const FVector3d Dir = PlanetTerrainFacePoint(Key.Face, U, V);
			Out.Directions[Idx] = Dir;
			Out.UVs[Idx] = FVector2D(static_cast<float>(U), static_cast<float>(V));
			Absolute[Idx] = Dir * (Radius + static_cast<double>(Fine[Idx]));
			OriginSum += Absolute[Idx];
		}
	}
	Out.TileOrigin = OriginSum / static_cast<double>(VertCount);
	Out.Normals.SetNum(VertCount);
	for (int32 Idx = 0; Idx < VertCount; ++Idx)
	{
		const FVector3d Rel = Absolute[Idx] - Out.TileOrigin;
		Out.PositionsLocal[Idx] = FVector(Rel.X, Rel.Y, Rel.Z);
		// Placeholder — UploadTileMesh recomputes morphed normals.
		Out.Normals[Idx] = FVector(Out.Directions[Idx].X, Out.Directions[Idx].Y, Out.Directions[Idx].Z);
	}
	Out.State = EPlanetTerrainTileState::ReadyForUpload;
}

void FPlanetTerrainTileBakerEx::Bake(
	const FPlanetTerrainHeight& Height,
	const FPlanetTerrainTileKey& Key,
	double U0, double V0, double U1, double V1,
	int32 Resolution,
	FPlanetTerrainTileData& Out,
	EPlanetTerrainBakeBackend Backend,
	TFunction<bool()> ShouldCancel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_BakeEx);
	const int32 Res = FMath::Max(2, Resolution);
	const int32 N = Res + 1;
	const double Arc = (U1 - U0) * 0.5 * PI * Height.GetRadiusMeters();
	const double WorldSize = FMath::Max(1.0, Arc * 2.0);
	const float FineSpacing = static_cast<float>(WorldSize / static_cast<double>(Res));
	const float CoarseSpacing = FineSpacing * 2.0f;

	TArray<float> Fine;
	TArray<float> Coarse;
	Fine.SetNumUninitialized(N * N);
	Coarse.SetNumUninitialized(N * N);

	bool bHaveHeights = false;
	if (Backend == EPlanetTerrainBakeBackend::GpuCompute)
	{
		if (ShouldCancel && ShouldCancel())
		{
			Out.State = EPlanetTerrainTileState::Unrequested;
			return;
		}
		bHaveHeights = FPlanetTerrainGpuBaker::TryBakeHeightsCompute(
			Height, Key.Face, U0, V0, U1, V1, Res, FineSpacing, CoarseSpacing, Fine, Coarse);
	}

	if (!bHaveHeights && Backend != EPlanetTerrainBakeBackend::DoubleCpu)
	{
		const double Du = (U1 - U0) / static_cast<double>(Res);
		const double Dv = (V1 - V0) / static_cast<double>(Res);
		for (int32 J = 0; J < N; ++J)
		{
			if (ShouldCancel && (J & 3) == 0 && ShouldCancel())
			{
				Out.State = EPlanetTerrainTileState::Unrequested;
				return;
			}
			for (int32 I = 0; I < N; ++I)
			{
				const int32 Idx = J * N + I;
				const FVector3d Dir = PlanetTerrainFacePoint(
					Key.Face, U0 + static_cast<double>(I) * Du, V0 + static_cast<double>(J) * Dv);
				float Hf = 0.0f, Hc = 0.0f;
				Height.EvaluateHeight2GpuFloat(Dir, FineSpacing, CoarseSpacing, Hf, Hc);
				Fine[Idx] = Hf;
				Coarse[Idx] = Hc;
			}
		}
		bHaveHeights = true;
	}

	if (!bHaveHeights)
	{
		FPlanetTerrainTileBaker::Bake(Height, Key, U0, V0, U1, V1, Resolution, Out, ShouldCancel);
		return;
	}

	if (ShouldCancel && ShouldCancel())
	{
		Out.State = EPlanetTerrainTileState::Unrequested;
		return;
	}
	FinishTileFromHeights(Height, Key, U0, V0, U1, V1, Res, Fine, Coarse, Out);
}
