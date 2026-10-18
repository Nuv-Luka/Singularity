#include "PlanetTerrain/PlanetTerrainTile.h"
#include "PlanetTerrain/PlanetTerrainNormals.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void FPlanetTerrainTileBaker::Bake(
	const FPlanetTerrainHeight& Height,
	const FPlanetTerrainTileKey& Key,
	double U0, double V0, double U1, double V1,
	int32 Resolution,
	FPlanetTerrainTileData& Out,
	TFunction<bool()> ShouldCancel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_BakeHeights);
	const int32 Res = FMath::Max(2, Resolution);
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
	Out.HeightFineM.SetNum(VertCount);
	Out.HeightCoarseM.SetNum(VertCount);
	Out.PositionsLocal.SetNum(VertCount);
	Out.Normals.SetNum(VertCount);
	Out.UVs.SetNum(VertCount);

	const double Radius = Height.GetRadiusMeters();
	const double Du = (U1 - U0) / static_cast<double>(Res);
	const double Dv = (V1 - V0) / static_cast<double>(Res);
	const double Arc = (U1 - U0) * 0.5 * PI * Radius; // ~half-face * fraction
	Out.WorldSizeM = FMath::Max(1.0, Arc * 2.0);
	Out.SampleSpacingM = Out.WorldSizeM / static_cast<double>(Res);
	const double CoarseSpacing = Out.SampleSpacingM * 2.0;

	const double Uc = 0.5 * (U0 + U1);
	const double Vc = 0.5 * (V0 + V1);
	Out.CenterLocal = PlanetTerrainFacePoint(Key.Face, Uc, Vc) * Radius;
	const double AnchorH = Height.LodAnchorElevation(PlanetTerrainFacePoint(Key.Face, Uc, Vc));
	Out.CenterSurfaceLocal = PlanetTerrainFacePoint(Key.Face, Uc, Vc) * (Radius + AnchorH);

	// First pass: heights + absolute planet-local positions in double.
	TArray<FVector3d> Absolute;
	Absolute.SetNum(VertCount);
	FVector3d OriginSum = FVector3d::ZeroVector;

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
			const double U = U0 + static_cast<double>(I) * Du;
			const double V = V0 + static_cast<double>(J) * Dv;
			const FVector3d Dir = PlanetTerrainFacePoint(Key.Face, U, V);
			Out.Directions[Idx] = Dir;

			double Hf = 0.0, Hc = 0.0;
			Height.EvaluateHeight2(Dir, Out.SampleSpacingM, CoarseSpacing, Hf, Hc);
			Out.HeightFineM[Idx] = static_cast<float>(Hf);
			Out.HeightCoarseM[Idx] = static_cast<float>(Hc);

			const FVector3d Pos = Dir * (Radius + Hf);
			Absolute[Idx] = Pos;
			OriginSum += Pos;
			Out.UVs[Idx] = FVector2D(static_cast<float>(U), static_cast<float>(V));
		}
	}

	Out.TileOrigin = OriginSum / static_cast<double>(VertCount);
	for (int32 Idx = 0; Idx < VertCount; ++Idx)
	{
		const FVector3d Rel = Absolute[Idx] - Out.TileOrigin;
		Out.PositionsLocal[Idx] = FVector(Rel.X, Rel.Y, Rel.Z);
	}

	// Skip bake-time normals — UploadTileMesh recomputes morphed normals from the
	// height field. Saves a full halo FD pass per tile on the worker.
	Out.Normals.SetNum(VertCount);
	for (int32 Idx = 0; Idx < VertCount; ++Idx)
	{
		Out.Normals[Idx] = FVector(Out.Directions[Idx].X, Out.Directions[Idx].Y, Out.Directions[Idx].Z);
	}

	Out.State = EPlanetTerrainTileState::ReadyForUpload;
}
