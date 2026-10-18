#include "PlanetTerrainNextCollisionManager.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextPatchMesh.h"

void FPlanetTerrainNextCollisionManager::Reset()
{
	Desired.Reset();
	ResidentOrder.Reset();
	Entries.Reset();
	Stats = FPlanetTerrainNextCollisionStats();
	CookTimesMs.Reset();
	CookTimeCursor = 0;
}

int32 FPlanetTerrainNextCollisionManager::LevelForSpacing(double RadiusM, double TargetSpacingM, int32 MaxLevel)
{
	const double Spacing = FMath::Max(TargetSpacingM, 0.25);
	// VertexSpacing = 2R / (GridN * 2^L)  →  2^L = 2R / (GridN * Spacing)
	const double Pow2 = (2.0 * RadiusM) / (static_cast<double>(PlanetTerrainNextPatch::GridN) * Spacing);
	const int32 Level = FMath::CeilToInt(FMath::Log2(FMath::Max(Pow2, 1.0)));
	return FMath::Clamp(Level, 0, MaxLevel);
}

void FPlanetTerrainNextCollisionManager::CollectFootprint(
	const FVector3d& FocusDir,
	double AngularRadiusRad,
	int32 Level,
	const TArray<FVector3d>& ExtraDirs,
	TArray<FPlanetTerrainNextNodeId>& Out) const
{
	Out.Reset();
	TSet<FPlanetTerrainNextNodeId> Seen;
	auto AddDir = [&](const FVector3d& Dir)
	{
		const FVector3d N = Dir.GetSafeNormal();
		if (N.IsNearlyZero())
		{
			return;
		}
		int32 Face = 0;
		double U = 0.0;
		double V = 0.0;
		PlanetTerrainNext::DirectionToFaceUV(N, Face, U, V);
		const uint32 Edge = 1u << Level;
		const uint32 X = FMath::Clamp(static_cast<uint32>(FMath::FloorToInt(U * Edge)), 0u, Edge - 1u);
		const uint32 Y = FMath::Clamp(static_cast<uint32>(FMath::FloorToInt(V * Edge)), 0u, Edge - 1u);
		const FPlanetTerrainNextNodeId Id(static_cast<uint8>(Face), static_cast<uint8>(Level), X, Y);
		if (!Seen.Contains(Id))
		{
			Seen.Add(Id);
			Out.Add(Id);
		}
	};

	AddDir(FocusDir);

	// Orthonormal tangent frame for a geodesic ring around the focus.
	FVector3d Tangent = FVector3d::CrossProduct(FocusDir, FVector3d::UpVector);
	if (Tangent.SizeSquared() < 1.0e-12)
	{
		Tangent = FVector3d::CrossProduct(FocusDir, FVector3d::RightVector);
	}
	Tangent.Normalize();
	const FVector3d Bitangent = FVector3d::CrossProduct(FocusDir, Tangent).GetSafeNormal();
	constexpr int32 RingSamples = 24;
	for (int32 Index = 0; Index < RingSamples; ++Index)
	{
		const double Ang = (2.0 * PI * Index) / RingSamples;
		const FVector3d Offset =
			(Tangent * FMath::Cos(Ang) + Bitangent * FMath::Sin(Ang)) * FMath::Sin(AngularRadiusRad)
			+ FocusDir * FMath::Cos(AngularRadiusRad);
		AddDir(Offset);
	}
	// Half-radius ring for denser coverage near the player.
	for (int32 Index = 0; Index < RingSamples / 2; ++Index)
	{
		const double Ang = (2.0 * PI * Index) / (RingSamples / 2);
		const double Half = AngularRadiusRad * 0.5;
		const FVector3d Offset =
			(Tangent * FMath::Cos(Ang) + Bitangent * FMath::Sin(Ang)) * FMath::Sin(Half)
			+ FocusDir * FMath::Cos(Half);
		AddDir(Offset);
	}
	for (const FVector3d& Extra : ExtraDirs)
	{
		AddDir(Extra);
	}
}

void FPlanetTerrainNextCollisionManager::UpdateDesired(
	const FVector3d& FocusM,
	const TArray<FVector3d>& PredictedPathM,
	double PlanetRadiusM)
{
	const double R = FMath::Max(PlanetRadiusM, 1.0);
	const FVector3d FocusDir = FocusM.GetSafeNormal();
	Stats.Level = LevelForSpacing(R, Settings.TargetSpacingM, Settings.MaxLevel);
	Stats.SpacingM = PlanetTerrainNextPatch::VertexSpacingMeters(R, Stats.Level);
	Stats.RadiusM = Settings.RadiusM;

	const double Angular = FMath::Clamp(Settings.RadiusM / R, 0.0, PI);
	TArray<FVector3d> Extras;
	Extras.Reserve(PredictedPathM.Num());
	for (const FVector3d& P : PredictedPathM)
	{
		Extras.Add(P.GetSafeNormal());
	}

	TArray<FPlanetTerrainNextNodeId> Raw;
	CollectFootprint(FocusDir, Angular, Stats.Level, Extras, Raw);

	// Keep the closest MaxPatches (by geodesic distance of node center to focus).
	Raw.Sort([&](const FPlanetTerrainNextNodeId& A, const FPlanetTerrainNextNodeId& B)
	{
		const FVector3d CA = PlanetTerrainNext::FacePoint(A.Face, 0.5 * (A.U0() + A.U1()), 0.5 * (A.V0() + A.V1()));
		const FVector3d CB = PlanetTerrainNext::FacePoint(B.Face, 0.5 * (B.U0() + B.U1()), 0.5 * (B.V0() + B.V1()));
		const double DA = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(FocusDir, CA), -1.0, 1.0));
		const double DB = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(FocusDir, CB), -1.0, 1.0));
		return DA < DB;
	});
	if (Raw.Num() > Settings.MaxPatches)
	{
		Raw.SetNum(Settings.MaxPatches);
	}

	Desired = MoveTemp(Raw);
	Stats.Desired = Desired.Num();

	TSet<FPlanetTerrainNextNodeId> DesiredSet;
	DesiredSet.Append(Desired);
	for (TPair<FPlanetTerrainNextNodeId, FEntry>& Pair : Entries)
	{
		Pair.Value.bDesired = DesiredSet.Contains(Pair.Key);
	}
}

void FPlanetTerrainNextCollisionManager::NotifyCooked(
	const FPlanetTerrainNextNodeId& Id,
	const FPlanetTerrainNextHeightTile& Tile,
	int64 Frame)
{
	FEntry& Entry = Entries.FindOrAdd(Id);
	Entry.Tile = Tile;
	Entry.CookedFrame = Frame;
	Entry.RetireFrame = INDEX_NONE;
	Entry.bDesired = true;
	if (!ResidentOrder.Contains(Id))
	{
		ResidentOrder.Add(Id);
	}
	Stats.Resident = Entries.Num();
}

void FPlanetTerrainNextCollisionManager::TickResidency(int64 Frame, TArray<FPlanetTerrainNextNodeId>& OutReleased)
{
	OutReleased.Reset();
	Stats.RetiredThisFrame = 0;

	bool bDesiredReady = true;
	for (const FPlanetTerrainNextNodeId& Id : Desired)
	{
		if (!Entries.Contains(Id))
		{
			bDesiredReady = false;
			break;
		}
	}

	for (TPair<FPlanetTerrainNextNodeId, FEntry>& Pair : Entries)
	{
		if (Pair.Value.bDesired)
		{
			Pair.Value.RetireFrame = INDEX_NONE;
			continue;
		}
		if (Pair.Value.RetireFrame == INDEX_NONE)
		{
			Pair.Value.RetireFrame = Frame + Settings.RetireGraceFrames;
		}
		// Keep undired until the new footprint is fully cooked, then honor grace.
		if (bDesiredReady && Frame >= Pair.Value.RetireFrame)
		{
			OutReleased.Add(Pair.Key);
		}
	}

	for (const FPlanetTerrainNextNodeId& Id : OutReleased)
	{
		Entries.Remove(Id);
		ResidentOrder.Remove(Id);
		++Stats.RetiredThisFrame;
	}
	Stats.Resident = Entries.Num();
}

bool FPlanetTerrainNextCollisionManager::TrySampleHeightM(
	const FPlanetTerrainNextHeightField& HeightField,
	const FVector3d& UnitDirection,
	double& OutHeightM) const
{
	OutHeightM = 0.0;
	const FVector3d Dir = UnitDirection.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}

	int32 Face = 0;
	double U = 0.0;
	double V = 0.0;
	PlanetTerrainNext::DirectionToFaceUV(Dir, Face, U, V);

	// Prefer a resident collision tile that contains this UV.
	for (const TPair<FPlanetTerrainNextNodeId, FEntry>& Pair : Entries)
	{
		const FPlanetTerrainNextNodeId& Id = Pair.Key;
		if (Id.Face != Face || !Id.ContainsUV(U, V) || !Pair.Value.Tile.IsValid())
		{
			continue;
		}
		const int32 Res = Pair.Value.Tile.Resolution();
		if (Res < 2)
		{
			continue;
		}
		const double Gu = FMath::Clamp((U - Id.U0()) / Id.Size(), 0.0, 1.0) * (Res - 1);
		const double Gv = FMath::Clamp((V - Id.V0()) / Id.Size(), 0.0, 1.0) * (Res - 1);
		const int32 I0 = FMath::Clamp(FMath::FloorToInt(Gu), 0, Res - 2);
		const int32 J0 = FMath::Clamp(FMath::FloorToInt(Gv), 0, Res - 2);
		const double Fu = Gu - I0;
		const double Fv = Gv - J0;
		const TArray<float>& Fine = Pair.Value.Tile.FineHeightM;
		auto H = [&](int32 I, int32 J) { return Fine[J * Res + I]; };
		// Fine surface only — stable for walking; morph is visual.
		OutHeightM = Fu >= Fv
			? H(I0, J0) * (1.0 - Fu) + H(I0 + 1, J0) * (Fu - Fv) + H(I0 + 1, J0 + 1) * Fv
			: H(I0, J0) * (1.0 - Fv) + H(I0, J0 + 1) * (Fv - Fu) + H(I0 + 1, J0 + 1) * Fu;
		return true;
	}

	const double Band = PlanetTerrainNextPatch::MinWavelengthMeters(
		HeightField.GetRadiusMeters(), Stats.Level > 0 ? Stats.Level : LevelForSpacing(HeightField.GetRadiusMeters(), Settings.TargetSpacingM, Settings.MaxLevel));
	OutHeightM = HeightField.HeightAt(Dir, Band);
	return true;
}

double FPlanetTerrainNextCollisionManager::SampleAltitudeM(
	const FPlanetTerrainNextHeightField& HeightField,
	const FVector3d& PointM) const
{
	const double Dist = PointM.Length();
	if (Dist < 1.0e-6)
	{
		return -HeightField.GetRadiusMeters();
	}
	double Height = 0.0;
	TrySampleHeightM(HeightField, PointM / Dist, Height);
	return Dist - (HeightField.GetRadiusMeters() + Height);
}

void FPlanetTerrainNextCollisionManager::RecordCookTime(double Ms)
{
	Stats.LastCookMs = Ms;
	constexpr int32 Samples = 128;
	if (CookTimesMs.Num() < Samples)
	{
		CookTimesMs.Add(Ms);
	}
	else
	{
		CookTimesMs[CookTimeCursor] = Ms;
		CookTimeCursor = (CookTimeCursor + 1) % Samples;
	}
	TArray<double> Sorted = CookTimesMs;
	Sorted.Sort();
	Stats.P95CookMs = Sorted[FMath::Clamp(FMath::CeilToInt(0.95 * Sorted.Num()) - 1, 0, Sorted.Num() - 1)];
}
