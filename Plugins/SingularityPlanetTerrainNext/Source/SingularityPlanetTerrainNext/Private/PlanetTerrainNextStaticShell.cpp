#include "PlanetTerrainNextStaticShell.h"
#include "PlanetTerrainNextCoordinates.h"

void BuildPlanetTerrainNextFaceMesh(
	int32 Face,
	int32 CellsPerEdge,
	double RadiusMeters,
	FPlanetTerrainNextFaceGeometry& OutGeometry)
{
	const int32 N = FMath::Clamp(CellsPerEdge, 1, 64);
	const int32 Row = N + 1;
	const double Radius = FMath::Max(RadiusMeters, 1.0);

	OutGeometry.PositionsMeters.Reset();
	OutGeometry.VerticesCm.Reset();
	OutGeometry.Normals.Reset();
	OutGeometry.Triangles.Reset();
	OutGeometry.UVs.Reset();
	OutGeometry.PositionsMeters.Reserve(Row * Row);
	OutGeometry.VerticesCm.Reserve(Row * Row);
	OutGeometry.Normals.Reserve(Row * Row);
	OutGeometry.UVs.Reserve(Row * Row);
	OutGeometry.Triangles.Reserve(N * N * 6);

	for (int32 J = 0; J <= N; ++J)
	{
		const double V = static_cast<double>(J) / static_cast<double>(N);
		for (int32 I = 0; I <= N; ++I)
		{
			const double U = static_cast<double>(I) / static_cast<double>(N);
			const FVector3d Direction = PlanetTerrainNext::FacePoint(Face, U, V);
			const FVector3d Meters = Direction * Radius;
			OutGeometry.PositionsMeters.Add(Meters);
			OutGeometry.VerticesCm.Add(FVector(Meters * PlanetTerrainNext::CmPerMeter));
			OutGeometry.Normals.Add(FVector(Direction));
			OutGeometry.UVs.Add(FVector2D(U, V));
		}
	}

	// Same winding as UPlanetTerrainComponent::BuildDistantFallback — the adaptive
	// CrossProduct·radius check picked the opposite order and rendered the shell inside-out.
	for (int32 J = 0; J < N; ++J)
	{
		for (int32 I = 0; I < N; ++I)
		{
			const int32 A = J * Row + I;
			OutGeometry.Triangles.Append({A, A + Row, A + 1, A + 1, A + Row, A + Row + 1});
		}
	}
}
