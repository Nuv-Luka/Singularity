#include "PlanetTerrainNextPatchMesh.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextLog.h"

namespace PlanetTerrainNextPatch
{
namespace
{
constexpr int32 HaloRow = GridN + 3;

FORCEINLINE int32 GridIndex(int32 I, int32 J)
{
	return J * Row + I;
}

FORCEINLINE int32 HaloIndex(int32 I, int32 J)
{
	return (J + 1) * HaloRow + (I + 1);
}

int32 EdgeGridIndex(int32 Edge, int32 K)
{
	switch (Edge)
	{
	case 0: return GridIndex(K, 0);
	case 1: return GridIndex(K, GridN);
	case 2: return GridIndex(0, K);
	default: return GridIndex(GridN, K);
	}
}

TArray<int32> MakeIndices()
{
	TArray<int32> Out;
	Out.Reserve(GridN * GridN * 6 + 4 * GridN * 6);
	for (int32 J = 0; J < GridN; ++J)
	{
		for (int32 I = 0; I < GridN; ++I)
		{
			const int32 A = GridIndex(I, J);
			Out.Append({A, A + Row, A + 1, A + 1, A + Row, A + Row + 1});
		}
	}
	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		for (int32 K = 0; K < GridN; ++K)
		{
			const int32 TopA = EdgeGridIndex(Edge, K);
			const int32 TopB = EdgeGridIndex(Edge, K + 1);
			const int32 BotA = GridVertexCount + Edge * Row + K;
			const int32 BotB = BotA + 1;
			// Match Legacy UPlanetTerrainComponent skirt winding per edge
			// (Edge 0=J0, 1=Jmax, 2=I0, 3=Imax).
			switch (Edge)
			{
			case 0: // Bottom J=0: {A, SA, B, B, SA, SB}
				Out.Append({TopA, BotA, TopB, TopB, BotA, BotB});
				break;
			case 1: // Top J=max: {C, D, SC, D, SD, SC}
				Out.Append({TopA, TopB, BotA, TopB, BotB, BotA});
				break;
			case 2: // Left I=0: {A, B, SA, B, SB, SA}
				Out.Append({TopA, TopB, BotA, TopB, BotB, BotA});
				break;
			default: // Right I=max: {C, SC, D, D, SC, SD}
				Out.Append({TopA, BotA, TopB, TopB, BotA, BotB});
				break;
			}
		}
	}
	return Out;
}

FPlanetTerrainNextNodeId ParentIdOf(const FPlanetTerrainNextNodeId& Id)
{
	return FPlanetTerrainNextNodeId(Id.Face, static_cast<uint8>(Id.Level - 1), Id.X >> 1, Id.Y >> 1);
}

/** Child (I,J) in parent-grid continuous coordinates [0, GridN]. */
void ChildToParentGrid(const FPlanetTerrainNextNodeId& Id, int32 I, int32 J, double& OutU, double& OutV)
{
	const int32 ChildIndex = Id.ChildIndex();
	const double Ox = (ChildIndex & 1) ? (GridN * 0.5) : 0.0;
	const double Oy = (ChildIndex & 2) ? (GridN * 0.5) : 0.0;
	OutU = Ox + 0.5 * I;
	OutV = Oy + 0.5 * J;
}

/** Bilinear sample of a Row×Row parent-relative position grid (coords in [0, GridN]). */
FVector3d BilinearParent(const TArray<FVector3d>& Grid, double U, double V)
{
	const double Uc = FMath::Clamp(U, 0.0, static_cast<double>(GridN));
	const double Vc = FMath::Clamp(V, 0.0, static_cast<double>(GridN));
	const int32 I0 = FMath::FloorToInt(Uc);
	const int32 J0 = FMath::FloorToInt(Vc);
	const int32 I1 = FMath::Min(I0 + 1, GridN);
	const int32 J1 = FMath::Min(J0 + 1, GridN);
	const double Fu = Uc - I0;
	const double Fv = Vc - J0;
	const FVector3d A = FMath::Lerp(Grid[GridIndex(I0, J0)], Grid[GridIndex(I1, J0)], Fu);
	const FVector3d B = FMath::Lerp(Grid[GridIndex(I0, J1)], Grid[GridIndex(I1, J1)], Fu);
	return FMath::Lerp(A, B, Fv);
}

} // namespace

const TArray<int32>& Indices()
{
	// Versioned so Live Coding / hot reload picks up skirt winding fixes.
	constexpr int32 IndexBufferVersion = 3;
	static int32 BuiltVersion = -1;
	static TArray<int32> Shared;
	if (BuiltVersion != IndexBufferVersion)
	{
		Shared = MakeIndices();
		BuiltVersion = IndexBufferVersion;
	}
	return Shared;
}

double VertexSpacingMeters(double RadiusM, int32 Level)
{
	return 2.0 * RadiusM / (static_cast<double>(GridN) * static_cast<double>(1ull << FMath::Clamp(Level, 0, 40)));
}

double MinWavelengthMeters(double RadiusM, int32 Level)
{
	return 2.0 * VertexSpacingMeters(RadiusM, Level);
}

double RecommendedSkirtDepthM(double GeometricErrorM, double VertexSpacingM)
{
	const double FromGE = 2.0 * FMath::Max(GeometricErrorM, 0.0) + 1.0;
	// Absolute cap: seam cover only. L0 GE is ~8 km — uncapped skirts read as inside-out shells.
	const double Cap = FMath::Clamp(4.0 * FMath::Max(VertexSpacingM, 1.0), 25.0, 250.0);
	return FMath::Min(FromGE, Cap);
}

FVector3d VertexDirection(const FPlanetTerrainNextNodeId& Id, int32 I, int32 J)
{
	const double Denominator = static_cast<double>(GridN) * static_cast<double>(1ull << Id.Level);
	const double U = (static_cast<double>(Id.X) * GridN + I) / Denominator;
	const double V = (static_cast<double>(Id.Y) * GridN + J) / Denominator;
	return PlanetTerrainNext::FacePoint(Id.Face, U, V);
}

float MorphFromPixelError(double PixelError, double TargetPixelError, double EndFactor)
{
	const double Target = FMath::Max(TargetPixelError, 1.0e-3);
	const double End = Target * FMath::Max(EndFactor, 1.0 + KINDA_SMALL_NUMBER);
	// Far from merge (large PE) → 0 (fine). Near merge (PE ≤ Target) → 1 (coarse).
	return static_cast<float>(1.0 - FMath::SmoothStep(Target, End, PixelError));
}

void Build(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextNodeId& Id,
	double SkirtDepthM,
	FPlanetTerrainNextPatchData& Out)
{
	// Bake through the canonical tile so Build and BuildFromTile stay bit-identical,
	// but copy heights into locals first (avoids any aliasing across the bake buffer).
	FPlanetTerrainNextTileId TileId;
	TileId.Node = Id;
	TileId.Resolution = Row;
	TileId.ParameterHash = HeightField.GetIdentityHash();
	TileId.DataFormat = 1;
	FPlanetTerrainNextHeightTile Tile;
	PlanetTerrainNextHeightTile::Bake(HeightField, TileId, Tile);
	if (!Tile.IsValid() || Tile.FineHeightM.Num() != Row * Row)
	{
		Out = FPlanetTerrainNextPatchData();
		Out.Id = Id;
		return;
	}
	BuildFromTile(HeightField, Tile, SkirtDepthM, Out);
}

void BuildFromTile(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextHeightTile& Tile,
	double SkirtDepthM,
	FPlanetTerrainNextPatchData& Out)
{
	const FPlanetTerrainNextNodeId Id = Tile.Id.Node;
	const double Radius = HeightField.GetRadiusMeters();
	const double FineWL = MinWavelengthMeters(Radius, Id.Level);
	const double CoarseWL = Id.Level == 0 ? FineWL : MinWavelengthMeters(Radius, Id.Level - 1);

	Out.Id = Id;
	Out.SkirtDepthM = SkirtDepthM;
	Out.OriginM = PlanetTerrainNext::FacePoint(
		Id.Face, 0.5 * (Id.U0() + Id.U1()), 0.5 * (Id.V0() + Id.V1())) * Radius;
	Out.MinHeightM = Tile.MinHeightM;
	Out.MaxHeightM = Tile.MaxHeightM;

	const int32 Res = Row;
	const TArray<float> FineH = Tile.FineHeightM;
	const bool bTileOk = FineH.Num() == Res * Res;
	if (!bTileOk)
	{
		UE_LOG(LogPlanetTerrainNext, Error,
			TEXT("BuildFromTile: fine tile size mismatch Fine=%d expected %d (GridN=%d Row=%d) node %s"),
			FineH.Num(), Res * Res, GridN, Row, *Id.ToString());
	}

	// Coarse positions = bilinear of the parent patch vertex grid (R3 morph contract).
	// Tile CoarseHeightM stays height-space for the GPU atlas; mesh morph uses positions.
	TArray<FVector3d> ParentGrid;
	if (Id.Level > 0)
	{
		const FPlanetTerrainNextNodeId Parent = ParentIdOf(Id);
		ParentGrid.SetNumUninitialized(GridVertexCount);
		for (int32 J = 0; J <= GridN; ++J)
		{
			for (int32 I = 0; I <= GridN; ++I)
			{
				const FVector3d Dir = VertexDirection(Parent, I, J);
				// Match parent tile bake: FineHeightM is float, so Morph=1 seals bit-exactly.
				const double H = static_cast<float>(HeightField.HeightAt(Dir, CoarseWL));
				ParentGrid[GridIndex(I, J)] = Dir * (Radius + H) - Out.OriginM;
			}
		}
	}

	TArray<FVector3d> FineHalo;
	TArray<FVector3d> CoarseHalo;
	TArray<FVector3d> HaloDirs;
	FineHalo.SetNumUninitialized(HaloRow * HaloRow);
	CoarseHalo.SetNumUninitialized(HaloRow * HaloRow);
	HaloDirs.SetNumUninitialized(HaloRow * HaloRow);

	for (int32 J = -1; J <= GridN + 1; ++J)
	{
		for (int32 I = -1; I <= GridN + 1; ++I)
		{
			const FVector3d Dir = VertexDirection(Id, I, J);
			const int32 Halo = HaloIndex(I, J);
			check(Halo >= 0 && Halo < FineHalo.Num());
			HaloDirs[Halo] = Dir;

			double Hf;
			if (bTileOk && I >= 0 && J >= 0 && I < Res && J < Res)
			{
				Hf = FineH[J * Res + I];
			}
			else
			{
				Hf = HeightField.HeightAt(Dir, FineWL);
			}
			FineHalo[Halo] = Dir * (Radius + Hf) - Out.OriginM;

			if (Id.Level == 0)
			{
				CoarseHalo[Halo] = FineHalo[Halo];
			}
			else
			{
				double PU = 0.0;
				double PV = 0.0;
				ChildToParentGrid(Id, I, J, PU, PV);
				CoarseHalo[Halo] = BilinearParent(ParentGrid, PU, PV);
			}
		}
	}

	Out.VerticesCm.SetNumUninitialized(VertexCount);
	Out.CoarseVerticesCm.SetNumUninitialized(VertexCount);
	Out.Normals.SetNumUninitialized(VertexCount);
	Out.CoarseNormals.SetNumUninitialized(VertexCount);
	Out.Tangents.SetNumUninitialized(VertexCount);
	Out.UVs.SetNumUninitialized(VertexCount);
	Out.UV1.SetNumUninitialized(VertexCount);
	Out.UV2.SetNumUninitialized(VertexCount);
	Out.VertexColors.SetNumUninitialized(VertexCount);
	Out.MaxAbsCoarseDeltaCm = 0.0;

	auto EmitVertex = [&](int32 Index, const FVector3d& Fine, const FVector3d& Coarse, const FVector3d& FineN, const FVector3d& CoarseN, const FVector3d& Tangent, const FVector2D& UV)
	{
		const FVector FineCm(Fine * PlanetTerrainNext::CmPerMeter);
		const FVector CoarseCm(Coarse * PlanetTerrainNext::CmPerMeter);
		const FVector Delta = CoarseCm - FineCm;
		Out.VerticesCm[Index] = FineCm;
		Out.CoarseVerticesCm[Index] = CoarseCm;
		Out.Normals[Index] = FVector(FineN);
		Out.CoarseNormals[Index] = FVector(CoarseN);
		Out.Tangents[Index] = FVector(Tangent);
		Out.UVs[Index] = UV;
		Out.UV1[Index] = FVector2D(Delta.X, Delta.Y);
		Out.UV2[Index] = FVector2D(Delta.Z, 0.0);
		Out.VertexColors[Index] = FLinearColor(
			static_cast<float>(CoarseN.X * 0.5 + 0.5),
			static_cast<float>(CoarseN.Y * 0.5 + 0.5),
			static_cast<float>(CoarseN.Z * 0.5 + 0.5),
			1.0f);
		Out.MaxAbsCoarseDeltaCm = FMath::Max(Out.MaxAbsCoarseDeltaCm, Delta.GetAbsMax());
	};

	for (int32 J = 0; J <= GridN; ++J)
	{
		for (int32 I = 0; I <= GridN; ++I)
		{
			const int32 HI = HaloIndex(I, J);
			const FVector3d& Fine = FineHalo[HI];
			const FVector3d& Coarse = CoarseHalo[HI];

			auto NormalOf = [](const TArray<FVector3d>& Halo, int32 I0, int32 J0, const FVector3d& Fallback)
			{
				const FVector3d DU = Halo[HaloIndex(I0 + 1, J0)] - Halo[HaloIndex(I0 - 1, J0)];
				const FVector3d DV = Halo[HaloIndex(I0, J0 + 1)] - Halo[HaloIndex(I0, J0 - 1)];
				FVector3d N = FVector3d::CrossProduct(DU, DV).GetSafeNormal();
				if (N.IsNearlyZero())
				{
					return Fallback;
				}
				// Keep lighting normals outward even if the discrete cross flips in rough terrain.
				if (FVector3d::DotProduct(N, Fallback) < 0.0)
				{
					N = -N;
				}
				return N;
			};
			const FVector3d FineN = NormalOf(FineHalo, I, J, HaloDirs[HI]);
			const FVector3d CoarseN = NormalOf(CoarseHalo, I, J, HaloDirs[HI]);
			const FVector3d Tangent = (FineHalo[HaloIndex(I + 1, J)] - FineHalo[HaloIndex(I - 1, J)]).GetSafeNormal();

			EmitVertex(GridIndex(I, J), Fine, Coarse, FineN, CoarseN, Tangent,
				FVector2D(static_cast<double>(I) / GridN, static_cast<double>(J) / GridN));
		}
	}

	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		for (int32 K = 0; K <= GridN; ++K)
		{
			const int32 Top = EdgeGridIndex(Edge, K);
			const int32 I = Top % Row;
			const int32 J = Top / Row;
			const FVector3d Down = HaloDirs[HaloIndex(I, J)] * SkirtDepthM;
			const int32 Bottom = GridVertexCount + Edge * Row + K;
			const FVector3d Fine = FineHalo[HaloIndex(I, J)] - Down;
			const FVector3d Coarse = CoarseHalo[HaloIndex(I, J)] - Down;
			EmitVertex(Bottom, Fine, Coarse,
				FVector3d(Out.Normals[Top]), FVector3d(Out.CoarseNormals[Top]),
				FVector3d(Out.Tangents[Top]), Out.UVs[Top]);
		}
	}

	// Normalize morph deltas into ~[-1,1] so ProceduralMesh F16 UVs cannot overflow
	// (raw cm deltas at coarse LODs reach ~1e7 and become Inf → missing faces).
	const float Scale = static_cast<float>(FMath::Max(Out.MaxAbsCoarseDeltaCm, 1.0));
	for (int32 Index = 0; Index < VertexCount; ++Index)
	{
		Out.UV1[Index].X /= Scale;
		Out.UV1[Index].Y /= Scale;
		Out.UV2[Index].X /= Scale;
		Out.UV2[Index].Y = 0.0f;
	}
}

void MeasureParentEdgeGaps(
	const FPlanetTerrainNextHeightField& HeightField,
	const FPlanetTerrainNextNodeId& ChildId,
	double& OutMaxMidpointGapM,
	double& OutMaxFineParentGapM)
{
	OutMaxMidpointGapM = 0.0;
	OutMaxFineParentGapM = 0.0;
	if (ChildId.Level == 0)
	{
		return;
	}

	FPlanetTerrainNextPatchData Patch;
	Build(HeightField, ChildId, 1.0, Patch);

	const FPlanetTerrainNextNodeId Parent = ParentIdOf(ChildId);
	const double Radius = HeightField.GetRadiusMeters();
	const double CoarseWL = MinWavelengthMeters(Radius, Parent.Level);

	auto ParentEdgePoint = [&](int32 Ip, int32 Jp) -> FVector3d
	{
		const FVector3d Dir = VertexDirection(Parent, Ip, Jp);
		// Same float quantization as parent tile FineHeightM / child ParentGrid.
		return Dir * (Radius + static_cast<float>(HeightField.HeightAt(Dir, CoarseWL)));
	};

	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		for (int32 K = 1; K < GridN; K += 2)
		{
			const int32 ChildVertex = EdgeGridIndex(Edge, K);
			double PU = 0.0;
			double PV = 0.0;
			const int32 I = ChildVertex % Row;
			const int32 J = ChildVertex / Row;
			ChildToParentGrid(ChildId, I, J, PU, PV);

			const int32 I0 = FMath::FloorToInt(PU);
			const int32 J0 = FMath::FloorToInt(PV);
			const int32 I1 = FMath::Min(I0 + 1, GridN);
			const int32 J1 = FMath::Min(J0 + 1, GridN);

			// Along a parent grid edge the midpoint is the 0.5 lerp of the two parent vertices.
			FVector3d ParentA;
			FVector3d ParentB;
			if (FMath::IsNearlyEqual(PV, static_cast<double>(J0), 1.0e-9) || FMath::IsNearlyEqual(PV, static_cast<double>(J1), 1.0e-9))
			{
				const int32 Je = FMath::RoundToInt(PV);
				ParentA = ParentEdgePoint(I0, Je);
				ParentB = ParentEdgePoint(I1, Je);
			}
			else if (FMath::IsNearlyEqual(PU, static_cast<double>(I0), 1.0e-9) || FMath::IsNearlyEqual(PU, static_cast<double>(I1), 1.0e-9))
			{
				const int32 Ie = FMath::RoundToInt(PU);
				ParentA = ParentEdgePoint(Ie, J0);
				ParentB = ParentEdgePoint(Ie, J1);
			}
			else
			{
				continue; // interior relative to parent grid — not a T-junction edge mid
			}

			const FVector3d ParentMid = FMath::Lerp(ParentA, ParentB, 0.5);
			const FVector3d CoarseWorld = Patch.OriginM + FVector3d(Patch.CoarseVerticesCm[ChildVertex]) / PlanetTerrainNext::CmPerMeter;
			const FVector3d FineWorld = Patch.OriginM + FVector3d(Patch.VerticesCm[ChildVertex]) / PlanetTerrainNext::CmPerMeter;
			OutMaxMidpointGapM = FMath::Max(OutMaxMidpointGapM, (CoarseWorld - ParentMid).Length());
			OutMaxFineParentGapM = FMath::Max(OutMaxFineParentGapM, (FineWorld - ParentMid).Length());
		}
	}
}
}
