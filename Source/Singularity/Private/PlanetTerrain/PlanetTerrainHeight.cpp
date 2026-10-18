#include "PlanetTerrain/PlanetTerrainHeight.h"

namespace
{
constexpr double ContinentWeight = 0.28;
constexpr double DetailWeight = 0.40;
constexpr int32 MaxContinentOctaves = 8;
constexpr int32 MaxMountainOctaves = 12;
constexpr int32 MaxDetailOctaves = 14;
constexpr int32 CraterOctaves = 12;
constexpr double CraterLacunarity = 1.9;
constexpr double CraterDepthFalloff = 0.62;
constexpr double CraterRimFrac = 0.28;
constexpr double LodAnchorSpacingFrac = 5e-4;

double Smooth(double T)
{
	return T * T * (3.0 - 2.0 * T);
}

double LerpD(double A, double B, double T)
{
	return A + (B - A) * T;
}

double Frac(double X)
{
	return X - FMath::Floor(X);
}
} // namespace

FPlanetTerrainHeight::FPlanetTerrainHeight(const FRockyPlanetDescriptor& InPlanet)
	: Planet(InPlanet)
{
	NoiseSeed = Planet.TerrainSeed ^ (static_cast<uint64>(Planet.GeneratorVersion) * 0x9E3779B97F4A7C15ull);
	FPugStream Rng = FPugStream::FromSeed(NoiseSeed ^ 0x7E44A1ull);

	const double ReliefFrac = 0.012 * static_cast<double>(Planet.ReliefAmplitude); // lunar-conservative
	AmplitudeM = Planet.RadiusMeters * ReliefFrac;

	ContinentFreq = 1.0 + Rng.NextF64() * 0.8;
	MountainFreq = ContinentFreq * (2.2 + Rng.NextF64() * 1.2);
	DetailFreq = 22.0 + Rng.NextF64() * 28.0;
	CraterFreq0 = 12.0 + Rng.NextF64() * 16.0;
	CraterDensity = FMath::Clamp(static_cast<double>(Planet.CraterDensity), 0.05, 0.98);
	CraterWeight = 0.40 + 0.35 * static_cast<double>(Planet.CraterDensity);
	MountainWeight = 0.20 + 0.25 * static_cast<double>(Planet.Roughness);
}

double FPlanetTerrainHeight::EvaluateHeightUnrestricted(const FVector3d& UnitDirection) const
{
	return EvaluateHeightAtSpacing(UnitDirection, 0.0);
}

double FPlanetTerrainHeight::EvaluateHeight(const FVector3d& UnitDirection, int32 DetailLevel) const
{
	if (DetailLevel < 0)
	{
		return EvaluateHeightUnrestricted(UnitDirection);
	}
	const double Circ = 2.0 * PI * Planet.RadiusMeters;
	const double Spacing = Circ / static_cast<double>(1 << FMath::Clamp(DetailLevel + 4, 4, 22));
	return EvaluateHeightAtSpacing(UnitDirection, Spacing);
}

double FPlanetTerrainHeight::LodAnchorElevation(const FVector3d& UnitDirection) const
{
	return EvaluateHeightAtSpacing(UnitDirection, Planet.RadiusMeters * LodAnchorSpacingFrac);
}

double FPlanetTerrainHeight::EvaluateCraterProfile(
	double DistanceFromCenter,
	double CraterRadius,
	double CraterDepth,
	double RimHeight)
{
	if (CraterRadius <= 0.0)
	{
		return 0.0;
	}
	const double T = DistanceFromCenter / CraterRadius;
	if (T >= 1.5)
	{
		return 0.0;
	}
	const double Bowl = -CraterDepth * (1.0 - Smooth(FMath::Clamp(T / 0.85, 0.0, 1.0)));
	const double E = (T - 0.95) / 0.12;
	const double Rim = RimHeight * FMath::Exp(-0.5 * E * E);
	return Bowl + Rim;
}

double FPlanetTerrainHeight::ValueNoise(double X, double Y, double Z) const
{
	const int64 Xi = static_cast<int64>(FMath::Floor(X));
	const int64 Yi = static_cast<int64>(FMath::Floor(Y));
	const int64 Zi = static_cast<int64>(FMath::Floor(Z));
	const double Xf = X - static_cast<double>(Xi);
	const double Yf = Y - static_cast<double>(Yi);
	const double Zf = Z - static_cast<double>(Zi);
	const double U = Smooth(Xf);
	const double V = Smooth(Yf);
	const double W = Smooth(Zf);

	auto Lattice = [this](int64 X, int64 Y, int64 Z) -> double
	{
		uint64 H = NoiseSeed;
		H = PugSplitMix64(H ^ static_cast<uint64>(X));
		H = PugSplitMix64(H ^ static_cast<uint64>(Y));
		H = PugSplitMix64(H ^ static_cast<uint64>(Z));
		return (static_cast<double>(H >> 11) * (1.0 / static_cast<double>(1ull << 53))) * 2.0 - 1.0;
	};

	const double C000 = Lattice(Xi, Yi, Zi), C100 = Lattice(Xi + 1, Yi, Zi);
	const double C010 = Lattice(Xi, Yi + 1, Zi), C110 = Lattice(Xi + 1, Yi + 1, Zi);
	const double C001 = Lattice(Xi, Yi, Zi + 1), C101 = Lattice(Xi + 1, Yi, Zi + 1);
	const double C011 = Lattice(Xi, Yi + 1, Zi + 1), C111 = Lattice(Xi + 1, Yi + 1, Zi + 1);

	const double X00 = LerpD(C000, C100, U), X10 = LerpD(C010, C110, U);
	const double X01 = LerpD(C001, C101, U), X11 = LerpD(C011, C111, U);
	const double Y0 = LerpD(X00, X10, V), Y1 = LerpD(X01, X11, V);
	return LerpD(Y0, Y1, W);
}

double FPlanetTerrainHeight::Fbm(const FVector3d& P, double Octaves, double Frequency, double Lacunarity, double Gain) const
{
	if (Octaves <= 0.0)
	{
		return 0.0;
	}
	const int32 Full = static_cast<int32>(FMath::Floor(Octaves));
	const double Fr = Octaves - static_cast<double>(Full);
	double Sum = 0.0, Amp = 1.0, F = Frequency, Norm = 0.0;
	for (int32 I = 0; I < Full; ++I)
	{
		Sum += Amp * ValueNoise(P.X * F, P.Y * F, P.Z * F);
		Norm += Amp;
		Amp *= Gain;
		F *= Lacunarity;
	}
	if (Fr > 0.0)
	{
		Sum += Amp * Fr * ValueNoise(P.X * F, P.Y * F, P.Z * F);
		Norm += Amp * Fr;
	}
	return Norm > 0.0 ? Sum / Norm : 0.0;
}

double FPlanetTerrainHeight::Ridged(const FVector3d& P, double Octaves, double Frequency, double Lacunarity, double Gain) const
{
	if (Octaves <= 0.0)
	{
		return 0.0;
	}
	const int32 Full = static_cast<int32>(FMath::Floor(Octaves));
	const double Fr = Octaves - static_cast<double>(Full);
	double Sum = 0.0, Amp = 0.5, F = Frequency, Prev = 1.0, Norm = 0.0;
	for (int32 I = 0; I < Full; ++I)
	{
		double N = 1.0 - FMath::Abs(ValueNoise(P.X * F, P.Y * F, P.Z * F));
		N *= N;
		N *= Prev;
		Sum += N * Amp;
		Norm += Amp;
		Prev = N;
		Amp *= Gain;
		F *= Lacunarity;
	}
	if (Fr > 0.0)
	{
		double N = 1.0 - FMath::Abs(ValueNoise(P.X * F, P.Y * F, P.Z * F));
		N *= N;
		N *= Prev;
		Sum += N * Amp * Fr;
		Norm += Amp * Fr;
	}
	return Norm > 0.0 ? FMath::Clamp(Sum / Norm, 0.0, 1.0) : 0.0;
}

double FPlanetTerrainHeight::OctavesFor(double BaseFreq, double SampleSpacing, int32 MaxOctaves) const
{
	if (SampleSpacing <= 0.0)
	{
		return static_cast<double>(MaxOctaves);
	}
	const double MaxFreq = PI * Planet.RadiusMeters / SampleSpacing;
	if (MaxFreq <= BaseFreq)
	{
		return 1.0;
	}
	const double O = FMath::Log2(MaxFreq / BaseFreq) + 1.0;
	return FMath::Clamp(O, 1.0, static_cast<double>(MaxOctaves));
}

double FPlanetTerrainHeight::LayerGate(double Freq, double SampleSpacing) const
{
	if (SampleSpacing <= 0.0)
	{
		return 1.0;
	}
	const double Wavelength = 2.0 * PI * Planet.RadiusMeters / Freq;
	const double Samples = Wavelength / SampleSpacing;
	return FMath::SmoothStep(4.0f, 64.0f, static_cast<float>(Samples));
}

void FPlanetTerrainHeight::CellFeature(
	int64 Cx, int64 Cy, int64 Cz, uint64 Salt,
	double& Fx, double& Fy, double& Fz, double& Rand) const
{
	uint64 H = PugSplitMix64(NoiseSeed ^ Salt);
	H = PugSplitMix64(H ^ static_cast<uint64>(Cx));
	H = PugSplitMix64(H ^ static_cast<uint64>(Cy));
	H = PugSplitMix64(H ^ static_cast<uint64>(Cz));
	auto Unit = [](uint64 V) { return static_cast<double>(V >> 11) * (1.0 / static_cast<double>(1ull << 53)); };
	Fx = static_cast<double>(Cx) + Unit(H);
	Fy = static_cast<double>(Cy) + Unit(PugSplitMix64(H ^ 0x9E3779B9u));
	Fz = static_cast<double>(Cz) + Unit(PugSplitMix64(H ^ 0x85EBCA6Bu));
	Rand = Unit(PugSplitMix64(H ^ 0xC2B2AE35u));
}

double FPlanetTerrainHeight::CraterLayer(
	const FVector3d& UnitDir, double Freq, uint64 Salt, double SampleSpacing, double Density) const
{
	const double Gate = LayerGate(Freq, SampleSpacing);
	if (Gate <= 0.0)
	{
		return 0.0;
	}
	const FVector3d P = UnitDir * Freq;
	const int64 Xi = static_cast<int64>(FMath::Floor(P.X));
	const int64 Yi = static_cast<int64>(FMath::Floor(P.Y));
	const int64 Zi = static_cast<int64>(FMath::Floor(P.Z));

	double MinBowl = 0.0, MaxRim = 0.0;
	for (int32 Dz = -1; Dz <= 1; ++Dz)
	{
		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				double Fx, Fy, Fz, Rand;
				CellFeature(Xi + Dx, Yi + Dy, Zi + Dz, Salt, Fx, Fy, Fz, Rand);
				if (Rand > Density)
				{
					continue;
				}
				const double Radius = 0.22 + 0.28 * Frac(Rand * 7.3 + 0.19);
				const double Ex = P.X - Fx, Ey = P.Y - Fy, Ez = P.Z - Fz;
				const double T = FMath::Sqrt(Ex * Ex + Ey * Ey + Ez * Ez) / Radius;
				if (T >= 1.5)
				{
					continue;
				}
				const double Bowl = -(1.0 - Smooth(FMath::Min(T, 1.0) / 0.85));
				const double E = (T - 0.95) / 0.12;
				const double Rim = CraterRimFrac * FMath::Exp(-0.5 * E * E);
				MinBowl = FMath::Min(MinBowl, Bowl);
				MaxRim = FMath::Max(MaxRim, Rim);
			}
		}
	}
	return (MinBowl + MaxRim) * Gate;
}

double FPlanetTerrainHeight::CraterField(const FVector3d& UnitDir, double SampleSpacing) const
{
	double Freq = CraterFreq0 * 0.5;
	double Weight = 1.0, Sum = 0.0, WSum = 0.0;
	for (int32 O = 0; O < CraterOctaves; ++O)
	{
		const uint64 Salt = 0x1111ull + static_cast<uint64>(O) * 0x9E3779B97F4A7C15ull;
		Sum += Weight * CraterLayer(UnitDir, Freq, Salt, SampleSpacing, CraterDensity);
		WSum += Weight;
		Freq *= CraterLacunarity;
		Weight *= CraterDepthFalloff;
	}
	return WSum > 0.0 ? Sum / WSum : 0.0;
}

double FPlanetTerrainHeight::EvaluateHeightAtSpacing(const FVector3d& UnitDirection, double SampleSpacingMeters) const
{
	const FVector3d Dir = UnitDirection.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return 0.0;
	}

	const double ContOct = OctavesFor(ContinentFreq, SampleSpacingMeters, MaxContinentOctaves);
	const double MtnOct = OctavesFor(MountainFreq, SampleSpacingMeters, MaxMountainOctaves);
	const double DetOct = OctavesFor(DetailFreq, SampleSpacingMeters, MaxDetailOctaves);

	const double Continents = Fbm(Dir, ContOct, ContinentFreq, 2.0, 0.5);
	const double Mask = FMath::SmoothStep(-0.2f, 0.4f, static_cast<float>(Continents));
	const FVector3d Warp(
		Fbm(Dir + FVector3d(1.1, 0.0, 0.0), 3.0, ContinentFreq * 0.9, 2.0, 0.5),
		Fbm(Dir + FVector3d(0.0, 1.3, 0.0), 3.0, ContinentFreq * 0.9, 2.0, 0.5),
		Fbm(Dir + FVector3d(0.0, 0.0, 1.7), 3.0, ContinentFreq * 0.9, 2.0, 0.5));
	const FVector3d Warped = (Dir + Warp * 0.12).GetSafeNormal();
	const double Mountains = Ridged(Warped, MtnOct, MountainFreq, 2.0, 0.55);
	const double Detail = Fbm(Dir, DetOct, DetailFreq, 2.0, 0.55);
	const double Crater = CraterWeight > 0.0 ? CraterField(Dir, SampleSpacingMeters) : 0.0;

	const double Shape = ContinentWeight * Continents
		+ MountainWeight * Mountains * Mask
		+ DetailWeight * Detail * 0.5
		+ CraterWeight * Crater;

	return AmplitudeM * Shape;
}

void FPlanetTerrainHeight::EvaluateHeight2(
	const FVector3d& UnitDirection,
	double FineSpacingMeters,
	double CoarseSpacingMeters,
	double& OutFine,
	double& OutCoarse) const
{
	OutFine = EvaluateHeightAtSpacing(UnitDirection, FineSpacingMeters);
	OutCoarse = EvaluateHeightAtSpacing(UnitDirection, CoarseSpacingMeters);
}

namespace
{
constexpr int32 GpuCraterOctaves = 6;

uint32 GpuHashU32(uint32 X)
{
	X ^= X >> 16;
	X *= 0x7feb352du;
	X ^= X >> 15;
	X *= 0x846ca68bu;
	X ^= X >> 16;
	return X;
}

float GpuLattice(uint32 Seed, int32 Xi, int32 Yi, int32 Zi)
{
	uint32 H = Seed;
	H = GpuHashU32(H ^ static_cast<uint32>(Xi) * 0x85ebca6bu);
	H = GpuHashU32(H ^ static_cast<uint32>(Yi) * 0xc2b2ae35u);
	H = GpuHashU32(H ^ static_cast<uint32>(Zi) * 0x27d4eb2du);
	return (static_cast<float>(H & 0x00FFFFFFu) * (1.0f / 16777215.0f)) * 2.0f - 1.0f;
}

float GpuSmooth(float T)
{
	return T * T * (3.0f - 2.0f * T);
}

float GpuValueNoise(uint32 Seed, float X, float Y, float Z)
{
	const int32 Xi = FMath::FloorToInt(X);
	const int32 Yi = FMath::FloorToInt(Y);
	const int32 Zi = FMath::FloorToInt(Z);
	const float Xf = X - static_cast<float>(Xi);
	const float Yf = Y - static_cast<float>(Yi);
	const float Zf = Z - static_cast<float>(Zi);
	const float U = GpuSmooth(Xf);
	const float V = GpuSmooth(Yf);
	const float W = GpuSmooth(Zf);
	const float C000 = GpuLattice(Seed, Xi, Yi, Zi);
	const float C100 = GpuLattice(Seed, Xi + 1, Yi, Zi);
	const float C010 = GpuLattice(Seed, Xi, Yi + 1, Zi);
	const float C110 = GpuLattice(Seed, Xi + 1, Yi + 1, Zi);
	const float C001 = GpuLattice(Seed, Xi, Yi, Zi + 1);
	const float C101 = GpuLattice(Seed, Xi + 1, Yi, Zi + 1);
	const float C011 = GpuLattice(Seed, Xi, Yi + 1, Zi + 1);
	const float C111 = GpuLattice(Seed, Xi + 1, Yi + 1, Zi + 1);
	const float X00 = FMath::Lerp(C000, C100, U);
	const float X10 = FMath::Lerp(C010, C110, U);
	const float X01 = FMath::Lerp(C001, C101, U);
	const float X11 = FMath::Lerp(C011, C111, U);
	return FMath::Lerp(FMath::Lerp(X00, X10, V), FMath::Lerp(X01, X11, V), W);
}

float GpuFbm(uint32 Seed, FVector3f P, float Octaves, float Frequency, float Lacunarity, float Gain)
{
	if (Octaves <= 0.0f)
	{
		return 0.0f;
	}
	const int32 Full = FMath::FloorToInt(Octaves);
	const float Fr = Octaves - static_cast<float>(Full);
	float Sum = 0.0f, Amp = 1.0f, F = Frequency, Norm = 0.0f;
	for (int32 I = 0; I < Full; ++I)
	{
		Sum += Amp * GpuValueNoise(Seed, P.X * F, P.Y * F, P.Z * F);
		Norm += Amp;
		Amp *= Gain;
		F *= Lacunarity;
	}
	if (Fr > 0.0f)
	{
		Sum += Amp * Fr * GpuValueNoise(Seed, P.X * F, P.Y * F, P.Z * F);
		Norm += Amp * Fr;
	}
	return Norm > 0.0f ? Sum / Norm : 0.0f;
}

float GpuRidged(uint32 Seed, FVector3f P, float Octaves, float Frequency, float Lacunarity, float Gain)
{
	if (Octaves <= 0.0f)
	{
		return 0.0f;
	}
	const int32 Full = FMath::FloorToInt(Octaves);
	const float Fr = Octaves - static_cast<float>(Full);
	float Sum = 0.0f, Amp = 0.5f, F = Frequency, Prev = 1.0f, Norm = 0.0f;
	for (int32 I = 0; I < Full; ++I)
	{
		float N = 1.0f - FMath::Abs(GpuValueNoise(Seed, P.X * F, P.Y * F, P.Z * F));
		N *= N;
		N *= Prev;
		Sum += N * Amp;
		Norm += Amp;
		Prev = N;
		Amp *= Gain;
		F *= Lacunarity;
	}
	if (Fr > 0.0f)
	{
		float N = 1.0f - FMath::Abs(GpuValueNoise(Seed, P.X * F, P.Y * F, P.Z * F));
		N *= N;
		N *= Prev;
		Sum += N * Amp * Fr;
		Norm += Amp * Fr;
	}
	return Norm > 0.0f ? FMath::Clamp(Sum / Norm, 0.0f, 1.0f) : 0.0f;
}

float GpuOctavesFor(float BaseFreq, float SampleSpacing, float RadiusM, int32 MaxOctaves)
{
	if (SampleSpacing <= 0.0f)
	{
		return static_cast<float>(MaxOctaves);
	}
	const float MaxFreq = PI * RadiusM / SampleSpacing;
	if (MaxFreq <= BaseFreq)
	{
		return 1.0f;
	}
	return FMath::Clamp(FMath::Log2(MaxFreq / BaseFreq) + 1.0f, 1.0f, static_cast<float>(MaxOctaves));
}

float GpuLayerGate(float Freq, float SampleSpacing, float RadiusM)
{
	if (SampleSpacing <= 0.0f)
	{
		return 1.0f;
	}
	const float Wavelength = 2.0f * PI * RadiusM / Freq;
	const float Samples = Wavelength / SampleSpacing;
	return FMath::SmoothStep(4.0f, 64.0f, Samples);
}

float GpuCraterLayer(uint32 Seed, FVector3f Dir, float Freq, uint32 Salt, float SampleSpacing, float Density, float RadiusM)
{
	const float Gate = GpuLayerGate(Freq, SampleSpacing, RadiusM);
	if (Gate <= 0.0f)
	{
		return 0.0f;
	}
	const FVector3f P = Dir * Freq;
	const int32 Xi = FMath::FloorToInt(P.X);
	const int32 Yi = FMath::FloorToInt(P.Y);
	const int32 Zi = FMath::FloorToInt(P.Z);
	float MinBowl = 0.0f, MaxRim = 0.0f;
	for (int32 Dz = -1; Dz <= 1; ++Dz)
	{
		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				uint32 H = GpuHashU32(Seed ^ Salt);
				H = GpuHashU32(H ^ static_cast<uint32>(Xi + Dx) * 0x85ebca6bu);
				H = GpuHashU32(H ^ static_cast<uint32>(Yi + Dy) * 0xc2b2ae35u);
				H = GpuHashU32(H ^ static_cast<uint32>(Zi + Dz) * 0x27d4eb2du);
				const float Fx = static_cast<float>(Xi + Dx) + (static_cast<float>(H & 0xFFFF) * (1.0f / 65535.0f));
				H = GpuHashU32(H ^ 0x9e3779b9u);
				const float Fy = static_cast<float>(Yi + Dy) + (static_cast<float>(H & 0xFFFF) * (1.0f / 65535.0f));
				H = GpuHashU32(H ^ 0x85ebca6bu);
				const float Fz = static_cast<float>(Zi + Dz) + (static_cast<float>(H & 0xFFFF) * (1.0f / 65535.0f));
				H = GpuHashU32(H ^ 0xc2b2ae35u);
				const float Rand = static_cast<float>(H & 0xFFFF) * (1.0f / 65535.0f);
				if (Rand > Density)
				{
					continue;
				}
				const float CraterR = 0.22f + 0.28f * FMath::Frac(Rand * 7.3f + 0.19f);
				const float Ex = P.X - Fx, Ey = P.Y - Fy, Ez = P.Z - Fz;
				const float T = FMath::Sqrt(Ex * Ex + Ey * Ey + Ez * Ez) / CraterR;
				if (T >= 1.5f)
				{
					continue;
				}
				const float Bowl = -(1.0f - GpuSmooth(FMath::Min(T, 1.0f) / 0.85f));
				const float E = (T - 0.95f) / 0.12f;
				const float Rim = 0.28f * FMath::Exp(-0.5f * E * E);
				MinBowl = FMath::Min(MinBowl, Bowl);
				MaxRim = FMath::Max(MaxRim, Rim);
			}
		}
	}
	return (MinBowl + MaxRim) * Gate;
}
} // namespace

void FPlanetTerrainHeight::GetGpuBakeUniforms(
	float& OutAmplitudeM,
	float& OutRadiusM,
	float& OutContinentFreq,
	float& OutMountainFreq,
	float& OutDetailFreq,
	float& OutCraterFreq0,
	float& OutCraterDensity,
	float& OutCraterWeight,
	float& OutMountainWeight,
	uint32& OutNoiseSeed) const
{
	OutAmplitudeM = static_cast<float>(AmplitudeM);
	OutRadiusM = static_cast<float>(Planet.RadiusMeters);
	OutContinentFreq = static_cast<float>(ContinentFreq);
	OutMountainFreq = static_cast<float>(MountainFreq);
	OutDetailFreq = static_cast<float>(DetailFreq);
	OutCraterFreq0 = static_cast<float>(CraterFreq0);
	OutCraterDensity = static_cast<float>(CraterDensity);
	OutCraterWeight = static_cast<float>(CraterWeight);
	OutMountainWeight = static_cast<float>(MountainWeight);
	OutNoiseSeed = static_cast<uint32>(NoiseSeed ^ (NoiseSeed >> 32));
}

float FPlanetTerrainHeight::EvaluateHeightGpuFloat(const FVector3d& UnitDirection, float SampleSpacingMeters) const
{
	const FVector3d Dn = UnitDirection.GetSafeNormal();
	if (Dn.IsNearlyZero())
	{
		return 0.0f;
	}
	float Amp, Radius, ContF, MtnF, DetF, CrF0, CrDen, CrW, MtnW;
	uint32 Seed;
	GetGpuBakeUniforms(Amp, Radius, ContF, MtnF, DetF, CrF0, CrDen, CrW, MtnW, Seed);

	const FVector3f Dir(static_cast<float>(Dn.X), static_cast<float>(Dn.Y), static_cast<float>(Dn.Z));
	const float ContOct = GpuOctavesFor(ContF, SampleSpacingMeters, Radius, 8);
	const float MtnOct = GpuOctavesFor(MtnF, SampleSpacingMeters, Radius, 10);
	const float DetOct = GpuOctavesFor(DetF, SampleSpacingMeters, Radius, 12);

	const float Continents = GpuFbm(Seed, Dir, ContOct, ContF, 2.0f, 0.5f);
	const float Mask = FMath::SmoothStep(-0.2f, 0.4f, Continents);
	const FVector3f Warp(
		GpuFbm(Seed, Dir + FVector3f(1.1f, 0.0f, 0.0f), 3.0f, ContF * 0.9f, 2.0f, 0.5f),
		GpuFbm(Seed, Dir + FVector3f(0.0f, 1.3f, 0.0f), 3.0f, ContF * 0.9f, 2.0f, 0.5f),
		GpuFbm(Seed, Dir + FVector3f(0.0f, 0.0f, 1.7f), 3.0f, ContF * 0.9f, 2.0f, 0.5f));
	FVector3f Warped = Dir + Warp * 0.12f;
	Warped.Normalize();
	const float Mountains = GpuRidged(Seed, Warped, MtnOct, MtnF, 2.0f, 0.55f);
	const float Detail = GpuFbm(Seed, Dir, DetOct, DetF, 2.0f, 0.55f);

	float Crater = 0.0f;
	if (CrW > 0.0f)
	{
		float Freq = CrF0 * 0.5f;
		float Weight = 1.0f, Sum = 0.0f, WSum = 0.0f;
		for (int32 O = 0; O < GpuCraterOctaves; ++O)
		{
			const uint32 Salt = 0x1111u + static_cast<uint32>(O) * 0x9E3779B9u;
			Sum += Weight * GpuCraterLayer(Seed, Dir, Freq, Salt, SampleSpacingMeters, CrDen, Radius);
			WSum += Weight;
			Freq *= 1.9f;
			Weight *= 0.62f;
		}
		Crater = WSum > 0.0f ? Sum / WSum : 0.0f;
	}

	const float Shape = 0.28f * Continents + MtnW * Mountains * Mask + 0.40f * Detail * 0.5f + CrW * Crater;
	return Amp * Shape;
}

void FPlanetTerrainHeight::EvaluateHeight2GpuFloat(
	const FVector3d& UnitDirection,
	float FineSpacingMeters,
	float CoarseSpacingMeters,
	float& OutFine,
	float& OutCoarse) const
{
	OutFine = EvaluateHeightGpuFloat(UnitDirection, FineSpacingMeters);
	OutCoarse = EvaluateHeightGpuFloat(UnitDirection, CoarseSpacingMeters);
}
