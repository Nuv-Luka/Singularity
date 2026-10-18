#include "PlanetTerrainNextHeightTile.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextPatchMesh.h"

namespace PlanetTerrainNextHeightTile
{
namespace
{
FPlanetTerrainNextNodeId ParentIdOf(const FPlanetTerrainNextNodeId& Id)
{
	return FPlanetTerrainNextNodeId(Id.Face, static_cast<uint8>(Id.Level - 1), Id.X >> 1, Id.Y >> 1);
}

void ChildToParentGrid(const FPlanetTerrainNextNodeId& Id, int32 I, int32 J, double& OutU, double& OutV)
{
	const int32 ChildIndex = Id.ChildIndex();
	const double Ox = (ChildIndex & 1) ? (PlanetTerrainNextPatch::GridN * 0.5) : 0.0;
	const double Oy = (ChildIndex & 2) ? (PlanetTerrainNextPatch::GridN * 0.5) : 0.0;
	OutU = Ox + 0.5 * I;
	OutV = Oy + 0.5 * J;
}

double BilinearHeight(const TArray<float>& Grid, int32 Res, double U, double V)
{
	const double Uc = FMath::Clamp(U, 0.0, static_cast<double>(Res - 1));
	const double Vc = FMath::Clamp(V, 0.0, static_cast<double>(Res - 1));
	const int32 I0 = FMath::FloorToInt(Uc);
	const int32 J0 = FMath::FloorToInt(Vc);
	const int32 I1 = FMath::Min(I0 + 1, Res - 1);
	const int32 J1 = FMath::Min(J0 + 1, Res - 1);
	const double Fu = Uc - I0;
	const double Fv = Vc - J0;
	const float A = FMath::Lerp(Grid[J0 * Res + I0], Grid[J0 * Res + I1], static_cast<float>(Fu));
	const float B = FMath::Lerp(Grid[J1 * Res + I0], Grid[J1 * Res + I1], static_cast<float>(Fu));
	return FMath::Lerp(A, B, Fv);
}
} // namespace

void Bake(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextTileId& Id,
	FPlanetTerrainNextHeightTile& Out)
{
	Out.Id = Id;
	Out.Id.Resolution = PlanetTerrainNextPatch::Row;
	Out.RadiusM = HeightField.GetRadiusMeters();
	const int32 Res = PlanetTerrainNextPatch::Row;
	Out.FineHeightM.SetNumUninitialized(Res * Res);
	Out.CoarseHeightM.SetNumUninitialized(Res * Res);

	const double FineWL = PlanetTerrainNextPatch::MinWavelengthMeters(Out.RadiusM, Id.Node.Level);
	const double CoarseWL = Id.Node.Level == 0
		? FineWL
		: PlanetTerrainNextPatch::MinWavelengthMeters(Out.RadiusM, Id.Node.Level - 1);

	TArray<float> ParentGrid;
	if (Id.Node.Level > 0)
	{
		const FPlanetTerrainNextNodeId Parent = ParentIdOf(Id.Node);
		ParentGrid.SetNumUninitialized(Res * Res);
		for (int32 J = 0; J < Res; ++J)
		{
			for (int32 I = 0; I < Res; ++I)
			{
				const FVector3d Dir = PlanetTerrainNextPatch::VertexDirection(Parent, I, J);
				ParentGrid[J * Res + I] = static_cast<float>(HeightField.HeightAt(Dir, CoarseWL));
			}
		}
	}

	double MinH = TNumericLimits<double>::Max();
	double MaxH = TNumericLimits<double>::Lowest();
	for (int32 J = 0; J < Res; ++J)
	{
		for (int32 I = 0; I < Res; ++I)
		{
			const FVector3d Dir = PlanetTerrainNextPatch::VertexDirection(Id.Node, I, J);
			const float Fine = static_cast<float>(HeightField.HeightAt(Dir, FineWL));
			float Coarse = Fine;
			if (Id.Node.Level > 0)
			{
				double PU = 0.0;
				double PV = 0.0;
				ChildToParentGrid(Id.Node, I, J, PU, PV);
				Coarse = static_cast<float>(BilinearHeight(ParentGrid, Res, PU, PV));
			}
			Out.FineHeightM[J * Res + I] = Fine;
			Out.CoarseHeightM[J * Res + I] = Coarse;
			MinH = FMath::Min(MinH, static_cast<double>(Fine));
			MaxH = FMath::Max(MaxH, static_cast<double>(Fine));
		}
	}
	Out.MinHeightM = MinH;
	Out.MaxHeightM = MaxH;
}

void PackRgbaFloat32(const FPlanetTerrainNextHeightTile& Tile, TArray<FLinearColor>& Out)
{
	Out.SetNumUninitialized(Tile.NumTexels());
	for (int32 Index = 0; Index < Tile.NumTexels(); ++Index)
	{
		Out[Index] = FLinearColor(Tile.FineHeightM[Index], Tile.CoarseHeightM[Index], 0.0f, 1.0f);
	}
}
}
