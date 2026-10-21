#include "PlanetTerrainNextGeology.h"

#include "PlanetTerrainNextCoordinates.h"

namespace
{
constexpr uint64 GeologyGoldenGamma = 0x9E3779B97F4A7C15ull;

FORCEINLINE uint64 GeologyMix64(uint64 Z)
{
	Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
	Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
	return Z ^ (Z >> 31);
}

FORCEINLINE uint64 GeologyHashStep(uint64 State, uint64 Value)
{
	return GeologyMix64(State ^ (Value + GeologyGoldenGamma + (State << 6) + (State >> 2)));
}

struct FGeologySplitMixRng
{
	uint64 State = 0;

	double Next01()
	{
		State += GeologyGoldenGamma;
		return static_cast<double>(GeologyMix64(State) >> 11) * (1.0 / 9007199254740992.0);
	}
};

FORCEINLINE double GeologySmoothStep(double T)
{
	T = FMath::Clamp(T, 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}

FORCEINLINE double GeologyAngleBetweenUnit(const FVector3d& A, const FVector3d& B)
{
	return FMath::Atan2(FVector3d::CrossProduct(A, B).Length(), FVector3d::DotProduct(A, B));
}

FORCEINLINE uint64 GeologyDoubleBits(double Value)
{
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	return Bits;
}

FORCEINLINE FVector3d SeedDirection(uint64 Seed)
{
	const double Z = static_cast<double>(GeologyMix64(Seed) >> 11) * (2.0 / 9007199254740992.0) - 1.0;
	const double Phi = static_cast<double>(GeologyMix64(Seed ^ 0xA5A5A5A5ull) >> 11)
		* (2.0 * PI / 9007199254740992.0);
	const double S = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
	return FVector3d(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
}

FORCEINLINE double EventEnvelope(
	const FPlanetTerrainNextGeologyEvent& Event,
	const FVector3d& Direction,
	double DistanceMeters,
	double SampleFootprintMeters)
{
	const double Feather = FMath::Max(Event.InfluenceRadiusMeters * 0.12, SampleFootprintMeters);
	const double Base = 1.0 - GeologySmoothStep(
		(DistanceMeters - Event.InfluenceRadiusMeters + Feather) / (2.0 * Feather));
	if (Base <= 0.0)
	{
		return 0.0;
	}

	// Low-frequency, direction-derived distortion keeps shorelines irregular while
	// remaining continuous across cube-face, longitude, and pole boundaries.
	const FVector3d AxisA = SeedDirection(Event.LocalSeed);
	const FVector3d AxisB = SeedDirection(Event.LocalSeed ^ 0xD1B54A32D192ED03ull);
	const double Warp = 1.0
		+ 0.055 * FMath::Sin(FVector3d::DotProduct(Direction, AxisA) * 11.0 + 1.3)
		+ 0.025 * FMath::Sin(FVector3d::DotProduct(Direction, AxisB) * 23.0 - 0.7);
	return Base * FMath::Clamp(Warp, 0.82, 1.18);
}

FORCEINLINE double FeatureBand(double WavelengthMeters, double SampleFootprintMeters)
{
	const double Footprint = FMath::Max(SampleFootprintMeters, 1.0);
	const double T = FMath::Clamp(WavelengthMeters / Footprint - 1.0, 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}
} // namespace

FPlanetTerrainNextGeologySettings FPlanetTerrainNextGeologySettings::FromDescriptor(
	const FPlanetTerrainNextDescriptor& Descriptor)
{
	FPlanetTerrainNextGeologySettings Settings;
	Settings.bEnableGeologicalBiomes = Descriptor.bEnableGeologicalBiomes;
	Settings.Preset = static_cast<EPlanetTerrainNextGeologyPreset>(FMath::Clamp(Descriptor.GeologyPreset, 0, 2));
	return Settings;
}

uint64 FPlanetTerrainNextGeologySettings::GetStableHash() const
{
	uint64 Hash = GeologyHashStep(0x47454F4Cull, Version);
	Hash = GeologyHashStep(Hash, bEnableGeologicalBiomes ? 1ull : 0ull);
	Hash = GeologyHashStep(Hash, static_cast<uint64>(Preset));
	Hash = GeologyHashStep(Hash, static_cast<uint64>(BasinCount));
	Hash = GeologyHashStep(Hash, static_cast<uint64>(ImpactCount));
	Hash = GeologyHashStep(Hash, GeologyDoubleBits(OldestAgeMyrBP));
	return GeologyHashStep(Hash, GeologyDoubleBits(YoungestAgeMyrBP));
}

void FPlanetTerrainNextSurfaceSample::NormalizeWeights()
{
	HighlandWeight = FMath::Max(0.0, HighlandWeight);
	MareWeight = FMath::Max(0.0, MareWeight);
	EjectaWeight = FMath::Max(0.0, EjectaWeight);
	ImpactMeltWeight = FMath::Max(0.0, ImpactMeltWeight);
	const double Sum = HighlandWeight + MareWeight + EjectaWeight + ImpactMeltWeight;
	if (Sum > SMALL_NUMBER)
	{
		const double Inv = 1.0 / Sum;
		HighlandWeight *= Inv;
		MareWeight *= Inv;
		EjectaWeight *= Inv;
		ImpactMeltWeight *= Inv;
	}
	else
	{
		HighlandWeight = 1.0;
		MareWeight = EjectaWeight = ImpactMeltWeight = 0.0;
	}
}

namespace PlanetTerrainNextGeology
{
double GreatCircleDistanceMeters(const FVector3d& AUnit, const FVector3d& BUnit, double RadiusMeters)
{
	const FVector3d A = AUnit.GetSafeNormal();
	const FVector3d B = BUnit.GetSafeNormal();
	return FMath::Max(0.0, RadiusMeters) * GeologyAngleBetweenUnit(A, B);
}

void BuildTangentFrame(const FVector3d& CenterUnit, FVector3d& OutEast, FVector3d& OutNorth)
{
	const FVector3d Center = CenterUnit.GetSafeNormal();
	const FVector3d Reference = FMath::Abs(Center.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX();
	OutEast = FVector3d::CrossProduct(Reference, Center).GetSafeNormal();
	if (OutEast.IsNearlyZero())
	{
		OutEast = FVector3d::CrossProduct(FVector3d::UnitY(), Center).GetSafeNormal();
	}
	OutNorth = FVector3d::CrossProduct(Center, OutEast).GetSafeNormal();
}

double WrappedAngleRadians(double A, double B)
{
	return FMath::Atan2(FMath::Sin(A - B), FMath::Cos(A - B));
}
} // namespace PlanetTerrainNextGeology

FPlanetTerrainNextGeologyModel::FPlanetTerrainNextGeologyModel(
	const FPlanetTerrainNextDescriptor& Descriptor)
	: Settings(FPlanetTerrainNextGeologySettings::FromDescriptor(Descriptor))
	, RadiusMeters(FMath::Max(Descriptor.RadiusMeters, 1000.0))
{
	uint64 RadiusBits = 0;
	FMemory::Memcpy(&RadiusBits, &RadiusMeters, sizeof(RadiusBits));
	IdentityHash = GeologyHashStep(GeologyHashStep(GeologyHashStep(0x47454F4D4F44454Cull, static_cast<uint64>(Descriptor.PlanetId)), static_cast<uint64>(Descriptor.Seed)), static_cast<uint64>(Descriptor.GeneratorVersion));
	IdentityHash = GeologyHashStep(IdentityHash, RadiusBits);
	IdentityHash = GeologyHashStep(IdentityHash, Settings.GetStableHash());

	SpatialBins.SetNum(6 * SpatialBinsPerFace * SpatialBinsPerFace);
	GenerateEvents(Descriptor);
	for (int32 Index = 0; Index < Events.Num(); ++Index)
	{
		AddEventToSpatialIndex(Index);
	}
}

void FPlanetTerrainNextGeologyModel::GenerateEvents(const FPlanetTerrainNextDescriptor& Descriptor)
{
	Events.Reset();
	FGeologySplitMixRng Rng{GeologyHashStep(static_cast<uint64>(Descriptor.Seed), 0x4556454E5453ull)};
	auto RandomDirection = [&Rng]()
	{
		const double Z = Rng.Next01() * 2.0 - 1.0;
		const double Phi = Rng.Next01() * 2.0 * PI;
		const double S = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		return FVector3d(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	};

	uint32 EventId = 0;
	for (int32 Index = 0; Index < Settings.BasinCount; ++Index)
	{
		FPlanetTerrainNextGeologyEvent& Event = Events.AddDefaulted_GetRef();
		Event.Id = EventId++;
		Event.Type = EPlanetTerrainNextGeologyEventType::AncientBasin;
		Event.CenterUnit = RandomDirection();
		Event.RadiusMeters = Index == 0 ? 550000.0 : FMath::Lerp(250000.0, 450000.0, Rng.Next01());
		Event.AgeMyrBP = FMath::Lerp(3200.0, Settings.OldestAgeMyrBP, Rng.Next01());
		Event.ReliefMeters = -FMath::Min(0.008 * Event.RadiusMeters, 4500.0);
		Event.RimMeters = -Event.ReliefMeters * 0.15;
		Event.InfluenceRadiusMeters = Event.RadiusMeters * 1.6;
		Event.FillHeightMeters = Event.ReliefMeters * 0.22;
		Event.ShorelineWidthMeters = FMath::Max(12000.0, Event.RadiusMeters * 0.08);
		Event.BowlDepthMeters = FMath::Abs(Event.ReliefMeters);
		Event.RimHeightMeters = Event.RimMeters;
		Event.CentralPeakMeters = 0.0;
		Event.RayInfluenceRadiusMeters = 0.0;
		Event.CompositionStrength = 0.65f;
		Event.LocalSeed = GeologyHashStep(static_cast<uint64>(Descriptor.Seed), Event.Id);

		// Separate basin-local basalt episodes approximate reachable flooding. The
		// flood mask is analytic in this first visual stage; it is intentionally
		// bounded by the basin cap rather than filling unrelated global lowlands.
		const int32 EpisodeCount = 2;
		for (int32 Episode = 0; Episode < EpisodeCount; ++Episode)
		{
			FPlanetTerrainNextGeologyEvent& Flow = Events.AddDefaulted_GetRef();
			Flow.Id = EventId++;
			Flow.Type = EPlanetTerrainNextGeologyEventType::BasaltEpisode;
			const FVector3d Offset = SeedDirection(Event.LocalSeed ^ static_cast<uint64>(Episode + 1));
			Flow.CenterUnit = (Event.CenterUnit * 0.78 + Offset * 0.22).GetSafeNormal();
			Flow.RadiusMeters = Event.RadiusMeters * FMath::Lerp(0.45, 0.75, Rng.Next01());
			Flow.AgeMyrBP = FMath::Clamp(Event.AgeMyrBP - FMath::Lerp(40.0, 900.0, Rng.Next01()), Settings.YoungestAgeMyrBP, Settings.OldestAgeMyrBP);
			Flow.ReliefMeters = 0.0;
			Flow.RimMeters = 0.0;
			Flow.InfluenceRadiusMeters = Flow.RadiusMeters * 1.35;
			Flow.FillHeightMeters = Event.FillHeightMeters + FMath::Lerp(-80.0, 80.0, Rng.Next01());
			Flow.ShorelineWidthMeters = FMath::Max(8000.0, Flow.RadiusMeters * 0.1);
			Flow.BowlDepthMeters = 0.0;
			Flow.RimHeightMeters = 0.0;
			Flow.CentralPeakMeters = 0.0;
			Flow.RayInfluenceRadiusMeters = 0.0;
			Flow.CompositionStrength = static_cast<float>(FMath::Lerp(0.55, 0.9, Rng.Next01()));
			Flow.LocalSeed = GeologyHashStep(Event.LocalSeed, static_cast<uint64>(Episode + 1));
		}
	}

	for (int32 Index = 0; Index < Settings.ImpactCount; ++Index)
	{
		FPlanetTerrainNextGeologyEvent& Event = Events.AddDefaulted_GetRef();
		Event.Id = EventId++;
		Event.Type = EPlanetTerrainNextGeologyEventType::Impact;
		Event.CenterUnit = RandomDirection();
		const double Radius = FMath::Exp(FMath::Lerp(FMath::Loge(6000.0), FMath::Loge(120000.0), Rng.Next01()));
		Event.RadiusMeters = Radius;
		Event.AgeMyrBP = FMath::Lerp(Settings.YoungestAgeMyrBP, Settings.OldestAgeMyrBP, Rng.Next01());
		Event.ReliefMeters = FMath::Min(Radius * FMath::Lerp(0.03, 0.12, Rng.Next01()), 4000.0);
		Event.RimMeters = Event.ReliefMeters * 0.3;
		Event.InfluenceRadiusMeters = Radius * 1.6;
		Event.FillHeightMeters = 0.0;
		Event.ShorelineWidthMeters = Radius * 0.1;
		Event.BowlDepthMeters = Event.ReliefMeters;
		Event.RimHeightMeters = Event.ReliefMeters * 0.3;
		Event.CentralPeakMeters = Radius > 50000.0 ? Event.ReliefMeters * 0.18 : 0.0;
		Event.RayInfluenceRadiusMeters = Radius * 8.0;
		Event.CompositionStrength = static_cast<float>(FMath::Lerp(0.2, 0.8, Rng.Next01()));
		Event.LocalSeed = GeologyHashStep(static_cast<uint64>(Descriptor.Seed), Event.Id);
	}

	Events.Sort([](const FPlanetTerrainNextGeologyEvent& A, const FPlanetTerrainNextGeologyEvent& B)
	{
		return A.AgeMyrBP > B.AgeMyrBP;
	});
	IdentityHash = GeologyHashStep(IdentityHash, static_cast<uint64>(Events.Num()));
	for (const FPlanetTerrainNextGeologyEvent& Event : Events)
	{
		IdentityHash = GeologyHashStep(IdentityHash, Event.Id);
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.RadiusMeters));
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.AgeMyrBP));
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.FillHeightMeters));
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.ShorelineWidthMeters));
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.BowlDepthMeters));
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.RimHeightMeters));
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.CentralPeakMeters));
		IdentityHash = GeologyHashStep(IdentityHash, GeologyDoubleBits(Event.RayInfluenceRadiusMeters));
		IdentityHash = GeologyHashStep(IdentityHash, Event.LocalSeed);
	}
}

void FPlanetTerrainNextGeologyModel::AddEventToSpatialIndex(int32 EventIndex)
{
	if (!Events.IsValidIndex(EventIndex))
	{
		return;
	}
	const FPlanetTerrainNextGeologyEvent& Event = Events[EventIndex];
	int32 Face = 0;
	double U = 0.5;
	double V = 0.5;
	PlanetTerrainNext::DirectionToFaceUV(Event.CenterUnit, Face, U, V);
	const int32 X = FMath::Clamp(FMath::FloorToInt(U * SpatialBinsPerFace), 0, SpatialBinsPerFace - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(V * SpatialBinsPerFace), 0, SpatialBinsPerFace - 1);
	const double MaxInfluence = FMath::Max(Event.InfluenceRadiusMeters, Event.RayInfluenceRadiusMeters);
	const int32 RadiusBins = MaxInfluence > RadiusMeters * 0.25 ? SpatialBinsPerFace : 1;
	for (int32 Oy = -RadiusBins; Oy <= RadiusBins; ++Oy)
	{
		for (int32 Ox = -RadiusBins; Ox <= RadiusBins; ++Ox)
		{
			const int32 BX = X + Ox;
			const int32 BY = Y + Oy;
			if (BX < 0 || BX >= SpatialBinsPerFace || BY < 0 || BY >= SpatialBinsPerFace)
			{
				continue;
			}
			SpatialBins[(Face * SpatialBinsPerFace + BY) * SpatialBinsPerFace + BX].Add(EventIndex);
		}
	}
}

void FPlanetTerrainNextGeologyModel::GatherCandidateIndices(
	const FVector3d& UnitDirection,
	TArray<int32, TInlineAllocator<64>>& OutIndices) const
{
	OutIndices.Reset();
	if (Events.Num() == 0)
	{
		return;
	}
	const FVector3d Direction = UnitDirection.GetSafeNormal();
	int32 Face = 0;
	double U = 0.5;
	double V = 0.5;
	PlanetTerrainNext::DirectionToFaceUV(Direction, Face, U, V);
	const int32 X = FMath::Clamp(FMath::FloorToInt(U * SpatialBinsPerFace), 0, SpatialBinsPerFace - 1);
	const int32 Y = FMath::Clamp(FMath::FloorToInt(V * SpatialBinsPerFace), 0, SpatialBinsPerFace - 1);
	TArray<bool, TInlineAllocator<64>> Seen;
	Seen.Init(false, Events.Num());
	const bool bNearCubeEdge = U < 0.08 || U > 0.92 || V < 0.08 || V > 0.92;
	if (bNearCubeEdge)
	{
		for (int32 Index = 0; Index < Events.Num(); ++Index)
		{
			Seen[Index] = true;
			OutIndices.Add(Index);
		}
		return;
	}
	for (int32 Oy = -1; Oy <= 1; ++Oy)
	{
		for (int32 Ox = -1; Ox <= 1; ++Ox)
		{
			const int32 BX = X + Ox;
			const int32 BY = Y + Oy;
			if (BX < 0 || BX >= SpatialBinsPerFace || BY < 0 || BY >= SpatialBinsPerFace)
			{
				continue;
			}
			const TArray<int32>& Bin = SpatialBins[(Face * SpatialBinsPerFace + BY) * SpatialBinsPerFace + BX];
			for (int32 EventIndex : Bin)
			{
				if (Seen.IsValidIndex(EventIndex) && !Seen[EventIndex])
				{
					Seen[EventIndex] = true;
					OutIndices.Add(EventIndex);
				}
			}
		}
	}
}

int32 FPlanetTerrainNextGeologyModel::CountCandidates(const FVector3d& UnitDirection) const
{
	TArray<int32, TInlineAllocator<64>> Candidates;
	GatherCandidateIndices(UnitDirection, Candidates);
	return Candidates.Num();
}

FPlanetTerrainNextSurfaceSample FPlanetTerrainNextGeologyModel::SampleSurface(
	const FVector3d& UnitDirection,
	double SampleFootprintMeters) const
{
	const FVector3d Direction = UnitDirection.GetSafeNormal();
	FPlanetTerrainNextSurfaceSample Sample;
	if (!Settings.bEnableGeologicalBiomes)
	{
		return Sample;
	}
	TArray<int32, TInlineAllocator<64>> Candidates;
	GatherCandidateIndices(Direction, Candidates);
	const double Footprint = FMath::Max(0.0, SampleFootprintMeters);
	for (int32 EventIndex : Candidates)
	{
		const FPlanetTerrainNextGeologyEvent& Event = Events[EventIndex];
		const double Distance = PlanetTerrainNextGeology::GreatCircleDistanceMeters(Direction, Event.CenterUnit, RadiusMeters);
		const double Influence = EventEnvelope(Event, Direction, Distance, Footprint);
		const double RayReach = Event.RayInfluenceRadiusMeters;
		const bool bRayCandidate = Event.Type == EPlanetTerrainNextGeologyEventType::Impact
			&& RayReach > 0.0 && Distance <= RayReach * 1.1;
		if (Influence <= 0.0 && !bRayCandidate)
		{
			continue;
		}
		const double Fresh = FMath::Exp(-Event.AgeMyrBP / 500.0);
		Sample.ExposureAgeMyr = FMath::Min(Sample.ExposureAgeMyr, Event.AgeMyrBP);
		Sample.Freshness = FMath::Max(Sample.Freshness, Fresh * Influence);
		if (Event.Type == EPlanetTerrainNextGeologyEventType::AncientBasin)
		{
			const double Deposit = Influence * Event.CompositionStrength;
			Sample.MareWeight = Sample.MareWeight * (1.0 - Deposit) + Deposit;
			Sample.HighlandWeight *= (1.0 - Deposit);
			const double BasinT = Distance / FMath::Max(Event.RadiusMeters, 1.0);
			if (BasinT < 1.0)
			{
				Sample.HeightOffsetM += Event.ReliefMeters * (1.0 - BasinT * BasinT) * Influence;
			}
			else if (BasinT < 1.35)
			{
				const double RingT = (BasinT - 1.0) / 0.35;
				Sample.HeightOffsetM += Event.RimMeters * (1.0 - RingT) * (1.0 - RingT) * Influence;
			}
		}
		else if (Event.Type == EPlanetTerrainNextGeologyEventType::BasaltEpisode)
		{
			// Basin-local flood approximation: smooth shoreline and bounded reach.
			const double Deposit = Influence * Event.CompositionStrength;
			Sample.MareWeight = Sample.MareWeight * (1.0 - Deposit) + Deposit;
			Sample.HighlandWeight *= (1.0 - Deposit);
			Sample.HeightOffsetM = FMath::Lerp(Sample.HeightOffsetM, Event.FillHeightMeters, Deposit * 0.25);
		}
		else
		{
			const double Deposit = Influence * Event.CompositionStrength * 0.5;
			Sample.EjectaWeight = Sample.EjectaWeight * (1.0 - Deposit) + Deposit;
			Sample.ImpactMeltWeight = FMath::Max(Sample.ImpactMeltWeight, Deposit * Fresh);
			Sample.RayIntensity = FMath::Max(Sample.RayIntensity, Deposit * Fresh);

			if (Event.Type == EPlanetTerrainNextGeologyEventType::Impact)
			{
				const double DegradationLife = FMath::Max(80.0, 320.0 * FMath::Sqrt(Event.RadiusMeters / 10000.0));
				const double Degradation = FMath::Exp(-Event.AgeMyrBP / DegradationLife);
				const double Band = FeatureBand(2.0 * Event.RadiusMeters, Footprint);
				const double T = Distance / FMath::Max(Event.RadiusMeters, 1.0);
				double Morphology = 0.0;
				if (T < 1.0)
				{
					const double BowlShape = FMath::Pow(FMath::Max(0.0, 1.0 - T * T), 1.25);
					Morphology -= Event.BowlDepthMeters * Degradation * BowlShape * Band;
				}
				const double RimT = FMath::Abs(T - 1.0) / 0.18;
				if (RimT < 1.0)
				{
					const double RimShape = FMath::Exp(-2.5 * RimT * RimT);
					Morphology += Event.RimHeightMeters * Degradation * RimShape * Band;
				}
				if (Event.CentralPeakMeters > 0.0 && T < 0.24)
				{
					const double PeakShape = FMath::Exp(-18.0 * T * T);
					Morphology += Event.CentralPeakMeters * Degradation * PeakShape * Band;
				}
				Sample.HeightOffsetM += Morphology * Influence;

				// Directional bright rays are material-only features. Their bearings,
				// lengths, and widths come from the immutable event seed, so the field
				// remains stable across tiles and cube-face boundaries.
				if (Fresh > 0.02 && Distance > Event.RadiusMeters * 0.8)
				{
					const double RayFeather = FMath::Max(Event.RayInfluenceRadiusMeters * 0.08, Footprint);
					const double RayInfluence = 1.0 - GeologySmoothStep(
						(Distance - Event.RayInfluenceRadiusMeters + RayFeather) / (2.0 * RayFeather));
					if (RayInfluence <= 0.0)
					{
						continue;
					}
					FVector3d East;
					FVector3d North;
					PlanetTerrainNextGeology::BuildTangentFrame(Event.CenterUnit, East, North);
					const FVector3d Tangent = (Direction - Event.CenterUnit * FVector3d::DotProduct(Direction, Event.CenterUnit)).GetSafeNormal();
					const double Theta = FMath::Atan2(
						FVector3d::DotProduct(Tangent, North),
						FVector3d::DotProduct(Tangent, East));
					const double Radial = Distance / FMath::Max(Event.RadiusMeters, 1.0);
					double BestRay = 0.0;
					for (int32 RayIndex = 0; RayIndex < 4; ++RayIndex)
					{
						const uint64 RaySeed = GeologyMix64(Event.LocalSeed ^ (0x9E3779B97F4A7C15ull * static_cast<uint64>(RayIndex + 1)));
						const double Unit = static_cast<double>(RaySeed >> 11) * (1.0 / 9007199254740992.0);
						const double Bearing = Unit * 2.0 * PI;
						const double Length = FMath::Lerp(2.5, 8.0, static_cast<double>((RaySeed >> 28) & 0xffff) / 65535.0);
						const double Width = FMath::Lerp(0.045, 0.12, static_cast<double>((RaySeed >> 44) & 0xffff) / 65535.0);
						const double Angular = PlanetTerrainNextGeology::WrappedAngleRadians(Theta, Bearing);
						const double AngularMask = FMath::Exp(-0.5 * FMath::Square(Angular / Width));
						const double RadialMask = FMath::Clamp(1.0 - (Radial - 1.0) / FMath::Max(Length, 1.0), 0.0, 1.0);
						BestRay = FMath::Max(BestRay, AngularMask * RadialMask);
					}
					Sample.RayIntensity = FMath::Max(Sample.RayIntensity, BestRay * Fresh * RayInfluence);
				}
			}
		}
	}
	Sample.NormalizeWeights();
	Sample.MaterialId = Sample.MareWeight > 0.5 ? 1 : (Sample.EjectaWeight > 0.35 ? 2 : 0);
	return Sample;
}
