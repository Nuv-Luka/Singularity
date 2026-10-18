#include "PlanetTerrain/PlanetTerrainCollision.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"

namespace
{
bool DirectionToFaceUV(uint8 Face, const FVector3d& Dir, double& U, double& V)
{
	const FVector3d D = Dir.GetSafeNormal();
	const double Ax = FMath::Abs(D.X), Ay = FMath::Abs(D.Y), Az = FMath::Abs(D.Z);
	uint8 Dom = 0;
	if (Ax >= Ay && Ax >= Az)
	{
		Dom = D.X > 0.0 ? 0 : 1;
		U = D.X > 0.0 ? 0.5 * (1.0 - D.Z / Ax) : 0.5 * (1.0 + D.Z / Ax);
		V = 0.5 * (1.0 + D.Y / Ax);
	}
	else if (Ay >= Ax && Ay >= Az)
	{
		Dom = D.Y > 0.0 ? 2 : 3;
		U = 0.5 * (1.0 + D.X / Ay);
		V = D.Y > 0.0 ? 0.5 * (1.0 - D.Z / Ay) : 0.5 * (1.0 + D.Z / Ay);
	}
	else
	{
		Dom = D.Z > 0.0 ? 4 : 5;
		U = D.Z > 0.0 ? 0.5 * (1.0 + D.X / Az) : 0.5 * (1.0 - D.X / Az);
		V = 0.5 * (1.0 + D.Y / Az);
	}
	return Dom == Face;
}

float SampleGrid(const TArray<float>& Grid, int32 N, double FU, double FV)
{
	const double X = FMath::Clamp(FU, 0.0, static_cast<double>(N - 1));
	const double Y = FMath::Clamp(FV, 0.0, static_cast<double>(N - 1));
	const int32 X0 = FMath::FloorToInt(X);
	const int32 Y0 = FMath::FloorToInt(Y);
	const int32 X1 = FMath::Min(N - 1, X0 + 1);
	const int32 Y1 = FMath::Min(N - 1, Y0 + 1);
	const float Tx = static_cast<float>(X - X0);
	const float Ty = static_cast<float>(Y - Y0);
	const float A = Grid[Y0 * N + X0];
	const float B = Grid[Y0 * N + X1];
	const float C = Grid[Y1 * N + X0];
	const float D = Grid[Y1 * N + X1];
	return FMath::Lerp(FMath::Lerp(A, B, Tx), FMath::Lerp(C, D, Tx), Ty);
}
} // namespace

bool PlanetTerrainSampleTileHeight(
	const FPlanetTerrainTileData& Tile,
	const FVector3d& UnitDir,
	float Morph,
	double& OutHeightM)
{
	double U = 0.0, V = 0.0;
	if (!DirectionToFaceUV(Tile.Face, UnitDir, U, V))
	{
		return false;
	}
	if (U < Tile.U0 || U > Tile.U1 || V < Tile.V0 || V > Tile.V1)
	{
		// Allow slight bleed for edge queries.
		U = FMath::Clamp(U, Tile.U0, Tile.U1);
		V = FMath::Clamp(V, Tile.V0, Tile.V1);
	}
	const int32 N = Tile.Resolution + 1;
	if (Tile.HeightFineM.Num() < N * N || Tile.HeightCoarseM.Num() < N * N)
	{
		return false;
	}
	const double FU = (U - Tile.U0) / FMath::Max(1e-12, Tile.U1 - Tile.U0) * Tile.Resolution;
	const double FV = (V - Tile.V0) / FMath::Max(1e-12, Tile.V1 - Tile.V0) * Tile.Resolution;
	const float Hf = SampleGrid(Tile.HeightFineM, N, FU, FV);
	const float Hc = SampleGrid(Tile.HeightCoarseM, N, FU, FV);
	OutHeightM = static_cast<double>(FMath::Lerp(Hf, Hc, Morph));
	return true;
}

bool PlanetTerrainSampleTileNormal(
	const FPlanetTerrainTileData& Tile,
	const FVector3d& UnitDir,
	float Morph,
	FVector3d& OutNormal)
{
	double H = 0.0;
	const FVector3d T = UnitDir.GetSafeNormal();
	if (!PlanetTerrainSampleTileHeight(Tile, T, Morph, H))
	{
		OutNormal = T;
		return false;
	}
	(void)H;
	OutNormal = T; // radial approx; cooked patch mesh owns precise contacts
	return true;
}

void PlanetTerrainResampleCoarseFromParent(
	FPlanetTerrainTileData& Child,
	const FPlanetTerrainTileData& Parent,
	float ParentMorph)
{
	const int32 N = Child.Resolution + 1;
	if (Child.HeightCoarseM.Num() < N * N || Child.Directions.Num() < N * N)
	{
		return;
	}
	for (int32 Idx = 0; Idx < N * N; ++Idx)
	{
		double H = 0.0;
		if (PlanetTerrainSampleTileHeight(Parent, Child.Directions[Idx], ParentMorph, H))
		{
			Child.HeightCoarseM[Idx] = static_cast<float>(H);
		}
	}
}
