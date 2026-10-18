#include "Pug/PugCore.h"

namespace
{
struct FDir
{
	double X = 0;
	double Y = 0;
	double Z = 0;
};

FDir Dir(double X, double Y, double Z)
{
	return {X, Y, Z};
}

FDir Normalize(FDir V)
{
	const double Len = FMath::Max(1e-15, FMath::Sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z));
	return {V.X / Len, V.Y / Len, V.Z / Len};
}

FDir Cross(FDir A, FDir B)
{
	return {
		A.Y * B.Z - A.Z * B.Y,
		A.Z * B.X - A.X * B.Z,
		A.X * B.Y - A.Y * B.X
	};
}

double Dot(FDir A, FDir B)
{
	return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

uint64 Mix3(uint64 A, uint64 B, uint64 C)
{
	return PugSplitMix64(A ^ PugSplitMix64(B * 0xC2B2AE3D27D4EB4Full ^ C));
}

double Fade(double T)
{
	return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
}

double Lerp(double A, double B, double T)
{
	return A + (B - A) * T;
}

double Smoothstep(double E0, double E1, double X)
{
	const double T = FMath::Clamp((X - E0) / (E1 - E0 + 1e-15), 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}

FDir Grad(uint64 Seed, int64 X, int64 Y, int64 Z)
{
	static const FDir G[12] = {
		{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
		{1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
		{0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}
	};
	const uint64 H = Mix3(Seed, static_cast<uint64>(X) ^ (static_cast<uint64>(Y) << 21), static_cast<uint64>(Z) * 0x9E3779B9ull);
	return G[H % 12];
}

double GradientNoise3(uint64 Seed, FDir P)
{
	const int64 X0 = static_cast<int64>(FMath::FloorToDouble(P.X));
	const int64 Y0 = static_cast<int64>(FMath::FloorToDouble(P.Y));
	const int64 Z0 = static_cast<int64>(FMath::FloorToDouble(P.Z));
	const double Fx = P.X - static_cast<double>(X0);
	const double Fy = P.Y - static_cast<double>(Y0);
	const double Fz = P.Z - static_cast<double>(Z0);
	const double U = Fade(Fx);
	const double V = Fade(Fy);
	const double W = Fade(Fz);
	double Corners[8];
	int32 Index = 0;
	for (int64 Dz = 0; Dz < 2; ++Dz)
	{
		for (int64 Dy = 0; Dy < 2; ++Dy)
		{
			for (int64 Dx = 0; Dx < 2; ++Dx)
			{
				const FDir G = Grad(Seed, X0 + Dx, Y0 + Dy, Z0 + Dz);
				Corners[Index++] = G.X * (Fx - Dx) + G.Y * (Fy - Dy) + G.Z * (Fz - Dz);
			}
		}
	}
	const double C00 = Lerp(Corners[0], Corners[1], U);
	const double C01 = Lerp(Corners[2], Corners[3], U);
	const double C10 = Lerp(Corners[4], Corners[5], U);
	const double C11 = Lerp(Corners[6], Corners[7], U);
	return Lerp(Lerp(C00, C01, V), Lerp(C10, C11, V), W);
}

double Noise3Dir(uint64 Seed, FDir Direction, double Freq)
{
	return GradientNoise3(Seed, {Direction.X * Freq, Direction.Y * Freq, Direction.Z * Freq});
}

double Fbm3(uint64 Seed, FDir Direction, double Freq, uint32 Octaves, double Lacunarity, double Persistence)
{
	double Amp = 1.0;
	double F = Freq;
	double Sum = 0.0;
	double Norm = 0.0;
	uint64 S = Seed;
	for (uint32 Octave = 0; Octave < Octaves; ++Octave)
	{
		Sum += Amp * Noise3Dir(S, Direction, F);
		Norm += Amp;
		Amp *= Persistence;
		F *= Lacunarity;
		S = Mix3(S, 0x9E3779B97F4A7C15ull, PugDoubleBits(F));
	}
	return Norm > 0.0 ? Sum / Norm : 0.0;
}

double Ridged3(uint64 Seed, FDir Direction, double Freq, uint32 Octaves, double Lacunarity, double Persistence)
{
	double Amp = 1.0;
	double F = Freq;
	double Sum = 0.0;
	double Norm = 0.0;
	uint64 S = Seed;
	for (uint32 Octave = 0; Octave < Octaves; ++Octave)
	{
		const double N = 1.0 - FMath::Abs(Noise3Dir(S, Direction, F));
		Sum += Amp * N * N;
		Norm += Amp;
		Amp *= Persistence;
		F *= Lacunarity;
		S = Mix3(S, 0xA5A5A5A5A5A5A5A5ull, PugDoubleBits(F));
	}
	return Norm > 0.0 ? Sum / Norm : 0.0;
}

void WorleyF1F2(uint64 Seed, FDir D, double Freq, double& OutF1, double& OutF2)
{
	const FDir P = {D.X * Freq, D.Y * Freq, D.Z * Freq};
	const int32 Ix = static_cast<int32>(FMath::FloorToDouble(P.X));
	const int32 Iy = static_cast<int32>(FMath::FloorToDouble(P.Y));
	const int32 Iz = static_cast<int32>(FMath::FloorToDouble(P.Z));
	double F1 = TNumericLimits<double>::Max();
	double F2 = F1;
	for (int32 Oz = -1; Oz <= 1; ++Oz)
	{
		for (int32 Oy = -1; Oy <= 1; ++Oy)
		{
			for (int32 Ox = -1; Ox <= 1; ++Ox)
			{
				const int32 Cx = Ix + Ox;
				const int32 Cy = Iy + Oy;
				const int32 Cz = Iz + Oz;
				const uint64 H = Mix3(
					Seed,
					static_cast<uint64>(static_cast<int64>(Cx)) * 0x85EBCA6Bull ^ static_cast<uint64>(static_cast<int64>(Cy)) * 0xC2B2AE35ull,
					static_cast<uint64>(static_cast<int64>(Cz)) * 0x27D4EB2Full);
				const double Jx = static_cast<double>((H >> 8) & 0xFFFF) / 65535.0;
				const double Jy = static_cast<double>((H >> 24) & 0xFFFF) / 65535.0;
				const double Jz = static_cast<double>((H >> 40) & 0xFFFF) / 65535.0;
				const double Dx = Cx + Jx - P.X;
				const double Dy = Cy + Jy - P.Y;
				const double Dz = Cz + Jz - P.Z;
				const double Dist2 = Dx * Dx + Dy * Dy + Dz * Dz;
				if (Dist2 < F1)
				{
					F2 = F1;
					F1 = Dist2;
				}
				else if (Dist2 < F2)
				{
					F2 = Dist2;
				}
			}
		}
	}
	OutF1 = FMath::Sqrt(F1);
	OutF2 = FMath::Sqrt(F2);
}

double CraterProfile(double R)
{
	if (R >= 1.4)
	{
		return 0.0;
	}
	const double Envelope = Smoothstep(1.4, 1.0, R);
	double Bowl = 0.0;
	if (R < 1.0)
	{
		const double T = 1.0 - R * R;
		Bowl = -T * T * (0.65 + 0.35 * T);
	}
	double Rim = 0.0;
	const double T = (R - 0.72) / 0.38;
	if (T >= 0.0 && T < 1.0)
	{
		const double S = T * T * (3.0 - 2.0 * T);
		Rim = 0.38 * (4.0 * S * (1.0 - S));
	}
	return (Bowl + Rim) * Envelope;
}

double CraterHeightM(uint64 Seed, FDir Direction, double Density, double AmplitudeM)
{
	if (Density <= 0.0 || AmplitudeM <= 0.0)
	{
		return 0.0;
	}
	const double Dens = FMath::Clamp(Density, 0.0, 1.0);
	const double Layers[3][2] = {{2.8, 1.0}, {6.5, 0.55}, {14.0, 0.22}};
	double H = 0.0;
	for (int32 I = 0; I < 3; ++I)
	{
		const uint64 LayerSeed = PugSplitMix64(Seed ^ (0xC7A7ull * (static_cast<uint64>(I) + 1ull)));
		double F1 = 0;
		double F2 = 0;
		WorleyF1F2(LayerSeed, Direction, Layers[I][0], F1, F2);
		const double Clarity = FMath::Clamp((F2 - F1) * 2.4, 0.0, 1.0);
		const double Presence = Smoothstep(1.0 - Dens * 0.85, 1.05 - Dens * 0.25, Clarity);
		const double R = FMath::Clamp(F1 * Layers[I][0] * 0.85, 0.0, 1.6);
		H += Presence * Layers[I][1] * CraterProfile(R);
	}
	return H * AmplitudeM;
}

struct FHeightParams
{
	double ContinentFreq;
	double WarpFreq;
	double WarpStrength;
	double MountainFreq;
	double PlateFreq;
	double AContinentM;
	double AMountainM;
	double CraterDensity;
	double CraterAmpM;
	double HeightMinM;
	double HeightMaxM;
	uint32 Octaves;
	double Lacunarity;
	double Persistence;
};

FHeightParams HeightFor(ERockPreset Preset)
{
	if (Preset == ERockPreset::Cratered)
	{
		return {0.7, 1.1, 0.04, 2.0, 0.9, 280.0, 180.0, 0.9, 1600.0, -2400.0, 900.0, 4, 2.05, 0.45};
	}
	return {1.4, 2.0, 0.18, 3.5, 1.6, 1800.0, 3200.0, 0.28, 420.0, -3200.0, 7200.0, 6, 2.05, 0.5};
}

double SoftClamp(double X, double Lo, double Hi)
{
	const double Mid = 0.5 * (Lo + Hi);
	const double Half = 0.5 * (Hi - Lo);
	if (Half <= 0.0)
	{
		return Mid;
	}
	const double T = FMath::Clamp((X - Mid) / Half, -1.8, 1.8);
	return Mid + Half * (T / FMath::Sqrt(1.0 + T * T));
}

double HeightM(uint64 PlanetSeed, FDir Direction, const FHeightParams& Params)
{
	const uint64 S = PugSplitMix64(PlanetSeed);
	const uint64 Sa = PugSplitMix64(S ^ 0x1111111111111111ull);
	const uint64 Sb = PugSplitMix64(S ^ 0x2222222222222222ull);
	const uint64 Sc = PugSplitMix64(S ^ 0x3333333333333333ull);
	const uint64 Sd = PugSplitMix64(S ^ 0x4444444444444444ull);
	const uint64 Se = PugSplitMix64(S ^ 0x5555555555555555ull);
	const uint64 Sf = PugSplitMix64(S ^ 0x6666666666666666ull);
	const double Base = Fbm3(Sa, Direction, Params.ContinentFreq, Params.Octaves, Params.Lacunarity, Params.Persistence);
	const FDir Warp = {
		Fbm3(Sb, Direction, Params.WarpFreq, 4, 2.0, 0.5),
		Fbm3(Sc, Direction, Params.WarpFreq, 4, 2.0, 0.5),
		Fbm3(Sd, Direction, Params.WarpFreq, 4, 2.0, 0.5)
	};
	const FDir D = Normalize({
		Direction.X + Warp.X * Params.WarpStrength,
		Direction.Y + Warp.Y * Params.WarpStrength,
		Direction.Z + Warp.Z * Params.WarpStrength
	});
	const double Ridges = Ridged3(Se ^ 0xABCDull, D, Params.MountainFreq, 4, Params.Lacunarity, 0.55);
	const double Tectonic = Smoothstep(0.2, 0.65, 0.5 + 0.5 * Fbm3(Se, D, Params.PlateFreq, 5, 2.0, 0.5));
	double H = Base * Params.AContinentM + Ridges * Tectonic * Params.AMountainM;
	H += CraterHeightM(Sf, D, Params.CraterDensity, Params.CraterAmpM);
	return SoftClamp(H, Params.HeightMinM, Params.HeightMaxM);
}

FDir FaceDirection(EPugCubeFace Face, double U, double V)
{
	FDir N, Tu, Tv;
	switch (Face)
	{
	case EPugCubeFace::PosX: N = {1, 0, 0}; Tu = {0, 0, -1}; Tv = {0, 1, 0}; break;
	case EPugCubeFace::NegX: N = {-1, 0, 0}; Tu = {0, 0, 1}; Tv = {0, 1, 0}; break;
	case EPugCubeFace::PosY: N = {0, 1, 0}; Tu = {1, 0, 0}; Tv = {0, 0, 1}; break;
	case EPugCubeFace::NegY: N = {0, -1, 0}; Tu = {1, 0, 0}; Tv = {0, 0, -1}; break;
	case EPugCubeFace::PosZ: N = {0, 0, 1}; Tu = {1, 0, 0}; Tv = {0, 1, 0}; break;
	case EPugCubeFace::NegZ:
	default: N = {0, 0, -1}; Tu = {-1, 0, 0}; Tv = {0, 1, 0}; break;
	}
	return Normalize({
		N.X + Tu.X * U + Tv.X * V,
		N.Y + Tu.Y * U + Tv.Y * V,
		N.Z + Tu.Z * U + Tv.Z * V
	});
}

FDir SurfacePoint(uint64 Seed, FDir Direction, const FHeightParams& Params, double RadiusM)
{
	const double R = RadiusM + HeightM(Seed, Direction, Params);
	return {Direction.X * R, Direction.Y * R, Direction.Z * R};
}

FDir SurfaceNormal(uint64 Seed, FDir Direction, const FHeightParams& Params, double RadiusM)
{
	constexpr double Eps = 1.2e-4;
	const FDir Up = FMath::Abs(Direction.Y) < 0.999 ? Dir(0, 1, 0) : Dir(1, 0, 0);
	const FDir T = Normalize(Cross(Up, Direction));
	const FDir B = Cross(Direction, T);
	const FDir Dt = Normalize({Direction.X + T.X * Eps, Direction.Y + T.Y * Eps, Direction.Z + T.Z * Eps});
	const FDir Db = Normalize({Direction.X + B.X * Eps, Direction.Y + B.Y * Eps, Direction.Z + B.Z * Eps});
	const FDir P = SurfacePoint(Seed, Direction, Params, RadiusM);
	const FDir Pt = SurfacePoint(Seed, Dt, Params, RadiusM);
	const FDir Pb = SurfacePoint(Seed, Db, Params, RadiusM);
	FDir N = Normalize(Cross({Pt.X - P.X, Pt.Y - P.Y, Pt.Z - P.Z}, {Pb.X - P.X, Pb.Y - P.Y, Pb.Z - P.Z}));
	if (Dot(N, Direction) < 0.0)
	{
		N = {-N.X, -N.Y, -N.Z};
	}
	return N;
}

double Slope(uint64 Seed, FDir Direction, const FHeightParams& Params, double RadiusM)
{
	const FDir N = SurfaceNormal(Seed, Direction, Params, RadiusM);
	return FMath::Clamp(1.0 - FMath::Clamp(Dot(Direction, N), -1.0, 1.0), 0.0, 1.0);
}

struct FBiome
{
	double TeqK;
	double Lapse;
	double SnowHigh;
	double SnowLow;
	double Dryness;
	double SlopeLow;
	double SlopeHigh;
	double RadiusM;
};

FLinearColor ShadeAlbedo(uint64 Seed, FDir Direction, const FHeightParams& Params, const FBiome& Biome)
{
	const double H = HeightM(Seed, Direction, Params);
	const double S = Slope(Seed, Direction, Params, Biome.RadiusM);
	const double Lat = FMath::Abs(Direction.Y);
	const double Temperature = Biome.TeqK - Biome.Lapse * FMath::Max(0.0, H) - Lat * 28.0;
	const double RockW = Smoothstep(Biome.SlopeLow, Biome.SlopeHigh, S);
	const double SnowW = Smoothstep(Biome.SnowHigh, Biome.SnowLow, Temperature);
	const double SandW = Biome.Dryness * (1.0 - RockW) * (1.0 - SnowW) * (1.0 - Lat * 0.35);
	const double SoilW = (1.0 - RockW) * (1.0 - SnowW) * (1.0 - FMath::Max(0.0, SandW));
	const double Sum = FMath::Max(0.0, RockW) + FMath::Max(0.0, SnowW) + FMath::Max(0.0, SandW) + FMath::Max(0.0, SoilW);
	const double Inv = Sum > 1e-12 ? 1.0 / Sum : 0.0;
	const FLinearColor Rock(0.45f, 0.36f, 0.28f);
	const FLinearColor Snow(0.93f, 0.95f, 0.97f);
	const FLinearColor Sand(0.76f, 0.63f, 0.38f);
	const FLinearColor Soil(0.24f, 0.40f, 0.18f);
	FLinearColor Color = Rock * static_cast<float>(FMath::Max(0.0, RockW) * Inv)
		+ Snow * static_cast<float>(FMath::Max(0.0, SnowW) * Inv)
		+ Sand * static_cast<float>(FMath::Max(0.0, SandW) * Inv)
		+ Soil * static_cast<float>(FMath::Max(0.0, SoilW) * Inv);
	if (Sum <= 1e-12)
	{
		Color = Soil;
	}
	const FDir N = SurfaceNormal(Seed, Direction, Params, Biome.RadiusM);
	const FDir Light = Normalize({0.45, 0.72, 0.35});
	const float Shade = static_cast<float>(0.38 + 0.62 * FMath::Clamp(Dot(N, Light), 0.0, 1.0));
	return Color * Shade;
}
}

FLinearColor PugBakeTileAlbedo(uint64 PlanetId, EPlanetClass Class, double EquilibriumTempK, EPugCubeFace Face, double U, double V)
{
	const bool bCratered = Class == EPlanetClass::Dwarf;
	const FHeightParams Height = HeightFor(bCratered ? ERockPreset::Cratered : ERockPreset::Rocky);
	FBiome Biome;
	if (bCratered)
	{
		Biome = {220.0, 0.002, 200.0, 180.0, 0.95, 0.05, 0.28, 6371000.0 * 0.27};
	}
	else if (Class == EPlanetClass::IceWorld)
	{
		Biome = {FMath::Clamp(EquilibriumTempK, 180.0, 260.0), 0.004, 265.0, 240.0, 0.2, 0.08, 0.35, 6371000.0};
	}
	else
	{
		Biome = {FMath::Clamp(EquilibriumTempK, 220.0, 340.0), 0.0065, 275.0, 255.0, 0.45, 0.08, 0.35, 6371000.0};
	}
	return ShadeAlbedo(PlanetId, FaceDirection(Face, U, V), Height, Biome);
}
