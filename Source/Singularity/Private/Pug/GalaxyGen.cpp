#include "Pug/PugCore.h"

namespace
{
double Sech(double X)
{
	return 2.0 / (FMath::Exp(X) + FMath::Exp(-X));
}

double RawDensity(const FGalaxyProfile& Profile, const double PositionPc[3])
{
	const double X = PositionPc[0];
	const double Y = PositionPc[1];
	const double Z = PositionPc[2];
	const double RCyl = FMath::Sqrt(X * X + Y * Y);
	const double RSph = FMath::Sqrt(X * X + Y * Y + Z * Z);
	const double Disk = Profile.DiskRho0
		* FMath::Exp(-RCyl / Profile.DiskRadialScalePc)
		* FMath::Square(Sech(Z / Profile.DiskVerticalScalePc));
	const double Bulge = Profile.BulgeRho0 * FMath::Pow(1.0 + FMath::Square(RSph / Profile.BulgeScalePc), -1.5);
	const double Theta = FMath::Atan2(Y, X);
	const double Arms = static_cast<double>(FMath::Max(1u, Profile.SpiralArms));
	const double Pitch = FMath::Max(1e-3, Profile.SpiralPitchRad);
	const double SpiralAngle = Theta - FMath::Loge(FMath::Max(1.0, RCyl)) / FMath::Tan(Pitch) - Profile.SpiralPhaseRad;
	const double Spiral = 1.0 + Profile.SpiralAmplitude * FMath::Cos(Arms * SpiralAngle);
	const double Soft = FMath::Max(1.0, Profile.DiskEdgeSoftnessPc);
	const double EdgeX = (RCyl - Profile.DiskTruncationRadiusPc) / Soft;
	const double Edge = 1.0 / (1.0 + FMath::Exp(EdgeX));
	return FMath::Max(0.0, Disk + Bulge) * FMath::Max(0.08, Spiral) * Edge;
}

double MeanDensity(const FGalaxyProfile& Profile, const double MinPc[3], const double MaxPc[3])
{
	double Sum = 0.0;
	constexpr int32 N = 3;
	for (int32 Ix = 0; Ix < N; ++Ix)
	{
		for (int32 Iy = 0; Iy < N; ++Iy)
		{
			for (int32 Iz = 0; Iz < N; ++Iz)
			{
				const double Fx = (Ix + 0.5) / N;
				const double Fy = (Iy + 0.5) / N;
				const double Fz = (Iz + 0.5) / N;
				const double P[3] = {
					MinPc[0] + (MaxPc[0] - MinPc[0]) * Fx,
					MinPc[1] + (MaxPc[1] - MinPc[1]) * Fy,
					MinPc[2] + (MaxPc[2] - MinPc[2]) * Fz
				};
				Sum += PugSampleDensity(Profile, P);
			}
		}
	}
	return Sum / static_cast<double>(N * N * N);
}

uint32 PoissonCount(FPugStream& Stream, double Lambda)
{
	Lambda = FMath::Max(0.0, Lambda);
	if (Lambda < 1e-6)
	{
		return 0;
	}
	if (Lambda > 60.0)
	{
		const double U1 = FMath::Clamp(Stream.NextF64(), 1e-12, 1.0);
		const double U2 = Stream.NextF64();
		const double Z = FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(PugTau * U2);
		return static_cast<uint32>(FMath::Max(0.0, FMath::RoundToDouble(Lambda + Z * FMath::Sqrt(Lambda))));
	}
	const double Limit = FMath::Exp(-Lambda);
	uint32 K = 0;
	double Product = 1.0;
	for (;;)
	{
		Product *= FMath::Clamp(Stream.NextF64(), 1e-12, 1.0);
		if (Product <= Limit)
		{
			return K;
		}
		++K;
		if (K > 10000)
		{
			return K;
		}
	}
}

double SegmentWeight(double MA, double MB, double Alpha)
{
	if (FMath::Abs(Alpha - 1.0) < 1e-9)
	{
		return FMath::Abs(FMath::Loge(MB / MA));
	}
	return FMath::Abs((FMath::Pow(MB, 1.0 - Alpha) - FMath::Pow(MA, 1.0 - Alpha)) / (1.0 - Alpha));
}

double SampleSegment(double U, double MA, double MB, double Alpha)
{
	U = FMath::Clamp(U, 0.0, 1.0);
	if (FMath::Abs(Alpha - 1.0) < 1e-9)
	{
		return MA * FMath::Pow(MB / MA, U);
	}
	const double Exp = 1.0 - Alpha;
	const double A = FMath::Pow(MA, Exp);
	const double B = FMath::Pow(MB, Exp);
	return FMath::Pow(A + U * (B - A), 1.0 / Exp);
}

double SampleMassSolar(FPugStream& Stream)
{
	constexpr double M0 = 0.08;
	constexpr double M1 = 0.5;
	constexpr double M2 = 120.0;
	const double WLow = SegmentWeight(M0, M1, 1.3);
	const double WHigh = SegmentWeight(M1, M2, 2.3);
	const double U = Stream.NextF64() * (WLow + WHigh);
	const double Mass = U < WLow
		? SampleSegment(Stream.NextF64(), M0, M1, 1.3)
		: SampleSegment(Stream.NextF64(), M1, M2, 2.3);
	return FMath::Clamp(Mass, M0, M2);
}

double Lerp(double A, double B, double T)
{
	return A + (B - A) * FMath::Clamp(T, 0.0, 1.0);
}

double Gaussian01(FPugStream& Stream)
{
	const double U1 = FMath::Clamp(Stream.NextF64(), 1e-12, 1.0);
	const double U2 = Stream.NextF64();
	const double Z = FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(PugTau * U2);
	return FMath::IsFinite(Z) ? Z : 0.0;
}

double SampleAgeGyr(FPugStream& Stream, const double PositionPc[3])
{
	constexpr double ThickScale = 900.0;
	const double ThickW = FMath::Clamp(1.0 - FMath::Exp(-FMath::Abs(PositionPc[2]) / ThickScale), 0.0, 0.85);
	if (Stream.NextF64() < ThickW)
	{
		return Lerp(6.0, 13.0, Stream.NextF64());
	}
	return Lerp(0.1, 10.0, Stream.NextF64());
}

double SampleMetallicity(FPugStream& Stream, const double PositionPc[3])
{
	const double R = FMath::Sqrt(PositionPc[0] * PositionPc[0] + PositionPc[1] * PositionPc[1]);
	const double Mean = (-0.07 / 1000.0) * (R - 8000.0);
	return FMath::Clamp(Mean + Gaussian01(Stream) * 0.22, -2.2, 0.6);
}

double MassLuminosity(double M)
{
	if (M < 0.43) return 0.23 * FMath::Pow(M, 2.3);
	if (M < 2.0) return FMath::Pow(M, 4.0);
	if (M < 20.0) return 1.5 * FMath::Pow(M, 3.5);
	return 3200.0 * M;
}

double MassRadius(double M)
{
	if (M < 1.0) return FMath::Pow(M, 0.8);
	if (M < 20.0) return FMath::Pow(M, 0.57);
	return FMath::Pow(M, 0.3) * 1.6;
}

ESpectralClass SpectralFromTeff(double TeffK)
{
	if (TeffK >= 30000.0) return ESpectralClass::O;
	if (TeffK >= 10000.0) return ESpectralClass::B;
	if (TeffK >= 7500.0) return ESpectralClass::A;
	if (TeffK >= 6000.0) return ESpectralClass::F;
	if (TeffK >= 5200.0) return ESpectralClass::G;
	if (TeffK >= 3700.0) return ESpectralClass::K;
	return ESpectralClass::M;
}

FStellarProperties GenerateStar(FPugStream& Stream, const double PositionPc[3])
{
	const double Mass = SampleMassSolar(Stream);
	const double Age = SampleAgeGyr(Stream, PositionPc);
	const double FeH = SampleMetallicity(Stream, PositionPc);
	Stream.NextF64();

	const double M = FMath::Clamp(Mass, 0.08, 120.0);
	const double LumMs = MassLuminosity(M);
	const double RadMs = MassRadius(M);
	const double TeffMs = FMath::Clamp(5772.0 * FMath::Pow(FMath::Max(1e-8, LumMs) / FMath::Square(FMath::Max(1e-3, RadMs)), 0.25), 2200.0, 50000.0);
	const double Lifetime = FMath::Clamp(10.0 * FMath::Pow(FMath::Max(0.08, Mass), -2.5), 0.003, 200.0);

	FStellarProperties Star;
	Star.MassSolar = Mass;
	Star.AgeGyr = Age;
	Star.MetallicityFeH = FeH;
	if (Age <= Lifetime)
	{
		Star.Stage = EStarStage::MainSequence;
		Star.LuminositySolar = LumMs;
		Star.RadiusSolar = RadMs;
		Star.TeffK = TeffMs;
	}
	else
	{
		Star.Stage = EStarStage::PostMainSequence;
		Star.LuminositySolar = FMath::Max(1e-4, LumMs * 0.05);
		Star.RadiusSolar = FMath::Max(0.01, RadMs * 0.2);
		Star.TeffK = FMath::Clamp(TeffMs * 0.55, 2500.0, 12000.0);
	}
	Star.TeffK = FMath::Clamp(Star.TeffK * (1.0 - 0.02 * FeH), 2200.0, 50000.0);
	Star.Spectral = SpectralFromTeff(Star.TeffK);
	return Star;
}
}

double PugSampleDensity(const FGalaxyProfile& Profile, const double PositionPc[3])
{
	const double AnchorPos[3] = {Profile.LocalRadiusPc, 0.0, 0.0};
	const double Anchor = FMath::Max(1e-9, RawDensity(Profile, AnchorPos));
	return FMath::Max(0.0, RawDensity(Profile, PositionPc) / Anchor);
}

FGeneratedSector PugGenerateSector(uint64 GalaxySeed, uint64 GalaxyId, FSectorCoord Coord, const FGalaxyProfile& Profile)
{
	FGeneratedSector Sector;
	Sector.SectorId = Coord.ToSectorId();
	const double Size = Profile.SectorSizePc;
	Sector.MinPc[0] = static_cast<double>(Coord.X) * Size;
	Sector.MinPc[1] = static_cast<double>(Coord.Y) * Size;
	Sector.MinPc[2] = static_cast<double>(Coord.Z) * Size;
	Sector.MaxPc[0] = Sector.MinPc[0] + Size;
	Sector.MaxPc[1] = Sector.MinPc[1] + Size;
	Sector.MaxPc[2] = Sector.MinPc[2] + Size;

	const uint64 SectorSeed = PugDeriveSectorSeed(GalaxySeed, GalaxyId, Sector.SectorId);
	FPugStream Stream = FPugStream::FromSeed(SectorSeed);
	const double Center[3] = {
		0.5 * (Sector.MinPc[0] + Sector.MaxPc[0]),
		0.5 * (Sector.MinPc[1] + Sector.MaxPc[1]),
		0.5 * (Sector.MinPc[2] + Sector.MaxPc[2])
	};
	const double Expected = MeanDensity(Profile, Sector.MinPc, Sector.MaxPc) * Profile.TracerStarsAtUnitDensity;
	const uint32 Count = FMath::Min(PoissonCount(Stream, Expected), Profile.MaxStarsPerSector);
	const double DensityRef = FMath::Max(1e-6, PugSampleDensity(Profile, Center)) * 1.35;

	uint32 LocalIndex = 0;
	uint32 Attempts = 0;
	const uint32 MaxAttempts = FMath::Max(Count * 24u, 24u);
	while (static_cast<uint32>(Sector.Stars.Num()) < Count && Attempts < MaxAttempts)
	{
		++Attempts;
		const double Pos[3] = {
			Sector.MinPc[0] + Stream.NextF64() * Size,
			Sector.MinPc[1] + Stream.NextF64() * Size,
			Sector.MinPc[2] + Stream.NextF64() * Size
		};
		const double Density = PugSampleDensity(Profile, Pos);
		if (Stream.NextF64() > FMath::Clamp(Density / DensityRef, 0.0, 1.0))
		{
			continue;
		}
		const uint64 StarSeed = PugDeriveObjectSeed(SectorSeed, static_cast<uint64>(LocalIndex), "star");
		FPugStream StarStream = FPugStream::FromSeed(StarSeed);
		FStarRecord Star;
		Star.Id = StarSeed ^ (static_cast<uint64>(LocalIndex) * 0x9E37ull);
		Star.PositionPc[0] = Pos[0];
		Star.PositionPc[1] = Pos[1];
		Star.PositionPc[2] = Pos[2];
		Star.Stellar = GenerateStar(StarStream, Pos);
		Sector.Stars.Add(Star);
		++LocalIndex;
	}
	Sector.Stars.Sort([](const FStarRecord& A, const FStarRecord& B) { return A.Id < B.Id; });
	return Sector;
}

FLinearColor PugStarColor(double TeffK)
{
	const double T = FMath::Clamp(TeffK, 2500.0, 40000.0);
	const double X = (T - 2500.0) / (12000.0 - 2500.0);
	const FLinearColor Cool(1.0f, 0.45f, 0.25f);
	const FLinearColor Sun(1.0f, 0.92f, 0.75f);
	const FLinearColor Hot(0.65f, 0.78f, 1.0f);
	if (X < 1.0)
	{
		return FLinearColor::LerpUsingHSV(Cool, Sun, static_cast<float>(FMath::Clamp(X, 0.0, 1.0)));
	}
	return FLinearColor::LerpUsingHSV(Sun, Hot, static_cast<float>(FMath::Clamp((T - 12000.0) / 18000.0, 0.0, 1.0)));
}
