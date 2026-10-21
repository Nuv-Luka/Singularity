#include "PlanetTerrainNextHeightField.h"

namespace
{
constexpr uint64 GoldenGamma = 0x9E3779B97F4A7C15ull;

FORCEINLINE uint64 Mix64(uint64 Z)
{
	Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
	Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
	return Z ^ (Z >> 31);
}

FORCEINLINE uint64 HashStep(uint64 State, uint64 Value)
{
	return Mix64(State ^ (Value + GoldenGamma + (State << 6) + (State >> 2)));
}

struct FSplitMixRng
{
	uint64 State = 0;

	double Next01()
	{
		State += GoldenGamma;
		return static_cast<double>(Mix64(State) >> 11) * (1.0 / 9007199254740992.0);
	}
};

FORCEINLINE double LatticeValue(int64 X, int64 Y, int64 Z, uint64 Seed)
{
	uint64 H = HashStep(Seed, static_cast<uint64>(X));
	H = HashStep(H, static_cast<uint64>(Y));
	H = HashStep(H, static_cast<uint64>(Z));
	return static_cast<double>(H >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

FORCEINLINE double Quintic(double T)
{
	return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
}

/** Trilinear value noise with quintic fade, range [-1, 1]. Double throughout. */
double ValueNoise(const FVector3d& P, uint64 Seed)
{
	const double Fx = FMath::FloorToDouble(P.X);
	const double Fy = FMath::FloorToDouble(P.Y);
	const double Fz = FMath::FloorToDouble(P.Z);
	const int64 Ix = static_cast<int64>(Fx);
	const int64 Iy = static_cast<int64>(Fy);
	const int64 Iz = static_cast<int64>(Fz);
	const double Ux = Quintic(P.X - Fx);
	const double Uy = Quintic(P.Y - Fy);
	const double Uz = Quintic(P.Z - Fz);

	const double N000 = LatticeValue(Ix, Iy, Iz, Seed);
	const double N100 = LatticeValue(Ix + 1, Iy, Iz, Seed);
	const double N010 = LatticeValue(Ix, Iy + 1, Iz, Seed);
	const double N110 = LatticeValue(Ix + 1, Iy + 1, Iz, Seed);
	const double N001 = LatticeValue(Ix, Iy, Iz + 1, Seed);
	const double N101 = LatticeValue(Ix + 1, Iy, Iz + 1, Seed);
	const double N011 = LatticeValue(Ix, Iy + 1, Iz + 1, Seed);
	const double N111 = LatticeValue(Ix + 1, Iy + 1, Iz + 1, Seed);

	const double X00 = FMath::Lerp(N000, N100, Ux);
	const double X10 = FMath::Lerp(N010, N110, Ux);
	const double X01 = FMath::Lerp(N001, N101, Ux);
	const double X11 = FMath::Lerp(N011, N111, Ux);
	const double Y0 = FMath::Lerp(X00, X10, Uy);
	const double Y1 = FMath::Lerp(X01, X11, Uy);
	return FMath::Lerp(Y0, Y1, Uz);
}

FORCEINLINE double AngleBetweenUnit(const FVector3d& A, const FVector3d& B)
{
	return FMath::Atan2(FVector3d::CrossProduct(A, B).Length(), FVector3d::DotProduct(A, B));
}

constexpr double MacroBaseWavelengthM = 3000000.0;
constexpr double MacroBaseAmplitudeM = 1200.0;
constexpr int32 MacroOctaveCount = 4;

constexpr double RoughBaseWavelengthM = 80000.0;
constexpr double RoughBaseAmplitudeM = 300.0;
constexpr double RoughHurst = 0.7;
constexpr double RoughFinestWavelengthM = 1.0;

constexpr int32 BasinCount = 4;
constexpr int32 CraterCount = 56;
constexpr double RimHalfWidth = 0.6;
} // namespace

double FPlanetTerrainNextHeightField::BandWeight(double WavelengthM, double MinResolvedWavelengthM)
{
	if (MinResolvedWavelengthM <= 0.0)
	{
		return 1.0;
	}
	const double T = FMath::Clamp(WavelengthM / MinResolvedWavelengthM - 1.0, 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}

FPlanetTerrainNextHeightField::FPlanetTerrainNextHeightField(const FPlanetTerrainNextDescriptor& Descriptor)
{
	RadiusM = FMath::Max(Descriptor.RadiusMeters, 1000.0);
	Seed = HashStep(HashStep(0x5EED5EEDull, static_cast<uint64>(Descriptor.Seed)), static_cast<uint64>(Descriptor.GeneratorVersion));

	uint64 RadiusBits = 0;
	FMemory::Memcpy(&RadiusBits, &RadiusM, sizeof(RadiusBits));
	IdentityHash = HashStep(Seed, RadiusBits);
	GeologyModel = MakeShared<FPlanetTerrainNextGeologyModel, ESPMode::ThreadSafe>(Descriptor);
	IdentityHash = HashStep(IdentityHash, GeologyModel->GetIdentityHash());

	FSplitMixRng Rng{HashStep(Seed, 0x0FF5E7ull)};
	auto MakeOctave = [this, &Rng](double Wavelength, double Amplitude, uint64 Layer, int32 Index)
	{
		FOctave Octave;
		Octave.WavelengthM = Wavelength;
		Octave.AmplitudeM = Amplitude;
		Octave.Frequency = RadiusM / Wavelength;
		Octave.Offset = FVector3d(Rng.Next01(), Rng.Next01(), Rng.Next01()) * 1024.0;
		Octave.Seed = HashStep(HashStep(Seed, Layer), static_cast<uint64>(Index));
		return Octave;
	};

	double MacroSum = 0.0;
	for (int32 Index = 0; Index < MacroOctaveCount; ++Index)
	{
		const double Wavelength = MacroBaseWavelengthM / FMath::Pow(2.0, Index);
		const double Amplitude = MacroBaseAmplitudeM * FMath::Pow(0.5, Index);
		MacroOctaves.Add(MakeOctave(Wavelength, Amplitude, 0x4D41ull, Index));
		MacroSum += Amplitude;
	}

	double RoughSum = 0.0;
	for (int32 Index = 0;; ++Index)
	{
		const double Wavelength = RoughBaseWavelengthM / FMath::Pow(2.0, Index);
		if (Wavelength < RoughFinestWavelengthM)
		{
			break;
		}
		const double Amplitude = RoughBaseAmplitudeM * FMath::Pow(Wavelength / RoughBaseWavelengthM, RoughHurst);
		RoughOctaves.Add(MakeOctave(Wavelength, Amplitude, 0x524Full, Index));
		RoughSum += Amplitude;
	}

	FSplitMixRng CraterRng{HashStep(Seed, 0xC4A7E5ull)};
	auto RandomDirection = [&CraterRng]()
	{
		const double Z = CraterRng.Next01() * 2.0 - 1.0;
		const double Phi = CraterRng.Next01() * 2.0 * PI;
		const double S = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		return FVector3d(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	};

	double MaxDepth = 0.0;
	double MaxRim = 0.0;
	auto AddCrater = [this, &MaxDepth, &MaxRim](const FVector3d& Center, double Radius, double Depth, double Rim)
	{
		FPlanetTerrainNextCrater Crater;
		Crater.Center = Center.GetSafeNormal();
		Crater.RadiusM = Radius;
		Crater.DepthM = Depth;
		Crater.RimM = Rim;
		const double InfluenceAngle = FMath::Min(PI, (1.0 + RimHalfWidth) * Radius / RadiusM);
		Crater.CosInfluence = FMath::Cos(InfluenceAngle) - 1.0e-12;
		Craters.Add(Crater);
		MaxDepth = FMath::Max(MaxDepth, Depth);
		MaxRim = FMath::Max(MaxRim, Rim);
	};

	for (int32 Index = 0; Index < BasinCount; ++Index)
	{
		const FVector3d Center = RandomDirection();
		const double Radius = Index == 0 ? 550000.0 : FMath::Lerp(250000.0, 450000.0, CraterRng.Next01());
		const double Depth = FMath::Min(0.008 * Radius, 4500.0);
		AddCrater(Center, Radius, Depth, 0.15 * Depth);
	}
	for (int32 Index = 0; Index < CraterCount; ++Index)
	{
		const FVector3d Center = RandomDirection();
		const double Radius = FMath::Exp(FMath::Lerp(FMath::Loge(6000.0), FMath::Loge(120000.0), CraterRng.Next01()));
		const double Ratio = FMath::Lerp(0.12, 0.03, FMath::Clamp((Radius - 6000.0) / 114000.0, 0.0, 1.0));
		const double Depth = FMath::Min(Radius * Ratio, 4000.0);
		AddCrater(Center, Radius, Depth, 0.3 * Depth);
	}

	MinHeightM = -(MacroSum + RoughSum + MaxDepth);
	MaxHeightM = MacroSum + RoughSum + MaxRim;
}

double FPlanetTerrainNextHeightField::SumOctaves(const TArray<FOctave>& Octaves, const FVector3d& Direction, double MinResolvedWavelengthM) const
{
	double Sum = 0.0;
	for (const FOctave& Octave : Octaves)
	{
		const double Weight = BandWeight(Octave.WavelengthM, MinResolvedWavelengthM);
		if (Weight <= 0.0)
		{
			break;
		}
		Sum += Weight * Octave.AmplitudeM * ValueNoise(Direction * Octave.Frequency + Octave.Offset, Octave.Seed);
	}
	return Sum;
}

double FPlanetTerrainNextHeightField::CraterProfile(
	const FPlanetTerrainNextCrater& Crater,
	const FVector3d& Direction,
	double Weight,
	double& OutBowl,
	double& OutRim) const
{
	OutBowl = 0.0;
	OutRim = 0.0;
	const double T = AngleBetweenUnit(Direction, Crater.Center) * RadiusM / Crater.RadiusM;
	if (T < 1.0)
	{
		OutBowl = -Weight * Crater.DepthM * (1.0 - T * T);
	}
	const double X = FMath::Abs(T - 1.0) / RimHalfWidth;
	if (X < 1.0)
	{
		const double Bump = 1.0 - X * X;
		OutRim = Weight * Crater.RimM * Bump * Bump;
	}
	return OutBowl + OutRim;
}

FPlanetTerrainNextHeightComponents FPlanetTerrainNextHeightField::EvaluateComponents(
	const FVector3d& UnitDirection,
	double MinResolvedWavelengthM) const
{
	const FVector3d Direction = UnitDirection.GetSafeNormal();

	FPlanetTerrainNextHeightComponents Out;
	Out.Macro = SumOctaves(MacroOctaves, Direction, MinResolvedWavelengthM);
	Out.Roughness = SumOctaves(RoughOctaves, Direction, MinResolvedWavelengthM);

	double MinBowl = 0.0;
	double MaxRim = 0.0;
	for (const FPlanetTerrainNextCrater& Crater : Craters)
	{
		if (FVector3d::DotProduct(Direction, Crater.Center) < Crater.CosInfluence)
		{
			continue;
		}
		const double Weight = BandWeight(2.0 * Crater.RadiusM, MinResolvedWavelengthM);
		if (Weight <= 0.0)
		{
			continue;
		}
		double Bowl = 0.0;
		double Rim = 0.0;
		CraterProfile(Crater, Direction, Weight, Bowl, Rim);
		MinBowl = FMath::Min(MinBowl, Bowl);
		MaxRim = FMath::Max(MaxRim, Rim);
	}
	Out.Craters = MinBowl + MaxRim;
	return Out;
}

double FPlanetTerrainNextHeightField::HeightAt(const FVector3d& UnitDirection, double MinResolvedWavelengthM) const
{
	return EvaluateComponents(UnitDirection, MinResolvedWavelengthM).Total();
}

FPlanetTerrainNextSurfaceSample FPlanetTerrainNextHeightField::SampleGeology(
	const FVector3d& UnitDirection,
	double SampleFootprintMeters) const
{
	return GeologyModel.IsValid()
		? GeologyModel->SampleSurface(UnitDirection, SampleFootprintMeters)
		: FPlanetTerrainNextSurfaceSample();
}

double FPlanetTerrainNextHeightField::CraterContribution(int32 CraterIndex, const FVector3d& UnitDirection, double MinResolvedWavelengthM) const
{
	if (!Craters.IsValidIndex(CraterIndex))
	{
		return 0.0;
	}
	const FPlanetTerrainNextCrater& Crater = Craters[CraterIndex];
	const FVector3d Direction = UnitDirection.GetSafeNormal();
	if (FVector3d::DotProduct(Direction, Crater.Center) < Crater.CosInfluence)
	{
		return 0.0;
	}
	double Bowl = 0.0;
	double Rim = 0.0;
	return CraterProfile(Crater, Direction, BandWeight(2.0 * Crater.RadiusM, MinResolvedWavelengthM), Bowl, Rim);
}

double FPlanetTerrainNextHeightField::GeometricErrorMeters(double VertexSpacingM) const
{
	const double Spacing = FMath::Max(VertexSpacingM, 0.0);
	const double MinWavelength = 2.0 * Spacing;
	const double Sag = Spacing * Spacing / (8.0 * RadiusM);

	double Noise = 0.0;
	auto AccumulateOctaves = [&Noise, Spacing, MinWavelength](const TArray<FOctave>& Octaves)
	{
		for (const FOctave& Octave : Octaves)
		{
			const double Weight = BandWeight(Octave.WavelengthM, MinWavelength);
			const double Ratio = PI * Spacing / Octave.WavelengthM;
			const double Interp = FMath::Min(1.0, 0.5 * Ratio * Ratio);
			Noise += Octave.AmplitudeM * ((1.0 - Weight) + Weight * Interp);
		}
	};
	AccumulateOctaves(MacroOctaves);
	AccumulateOctaves(RoughOctaves);

	double CraterError = 0.0;
	for (const FPlanetTerrainNextCrater& Crater : Craters)
	{
		const double Weight = BandWeight(2.0 * Crater.RadiusM, MinWavelength);
		const double Ratio = Spacing / Crater.RadiusM;
		const double Interp = FMath::Min(1.0, 2.0 * Ratio * Ratio);
		CraterError = FMath::Max(CraterError, Crater.DepthM * ((1.0 - Weight) + Weight * Interp));
	}
	return Sag + Noise + CraterError;
}
