#include "Pug/PugCore.h"

namespace
{
double RemEuclid(double Value, double Mod)
{
	double R = FMath::Fmod(Value, Mod);
	if (R < 0.0)
	{
		R += Mod;
	}
	return R;
}

double PeriodDays(double SemiMajorAu, double StarMassSolar)
{
	const double M = FMath::Max(0.08, StarMassSolar);
	const double A = FMath::Max(1e-4, SemiMajorAu);
	return FMath::Sqrt(A * A * A / M) * 365.25;
}

double SolveKepler(double MeanAnomaly, double Eccentricity)
{
	double E = Eccentricity > 0.8 ? PI : MeanAnomaly;
	for (int32 Step = 0; Step < 12; ++Step)
	{
		const double F = E - Eccentricity * FMath::Sin(E) - MeanAnomaly;
		const double Fp = FMath::Max(1e-9, 1.0 - Eccentricity * FMath::Cos(E));
		const double De = F / Fp;
		E -= De;
		if (FMath::Abs(De) < 1e-10)
		{
			break;
		}
	}
	return E;
}

double SnowLineAu(double LuminositySolar)
{
	return 2.7 * FMath::Sqrt(FMath::Max(1e-4, LuminositySolar));
}

double EquilibriumTempK(double LuminositySolar, double SemiMajorAu, double Albedo)
{
	const double A = FMath::Max(0.05, SemiMajorAu);
	const double Lum = FMath::Max(1e-6, LuminositySolar);
	const double OneMinusA = FMath::Clamp(1.0 - Albedo, 0.05, 0.95);
	return 278.0 * FMath::Pow(Lum, 0.25) / FMath::Sqrt(A) * FMath::Pow(OneMinusA, 0.25);
}

double MassRadiusEarth(double MassEarth, EPlanetClass Class)
{
	const double M = FMath::Max(0.01, MassEarth);
	switch (Class)
	{
	case EPlanetClass::Rocky:
		return FMath::Clamp(FMath::Pow(M, 0.27), 0.4, 2.2);
	case EPlanetClass::IceWorld:
		return FMath::Clamp(1.15 * FMath::Pow(M, 0.25), 0.8, 4.0);
	case EPlanetClass::GasGiant:
		if (M < 50.0)
		{
			return FMath::Clamp(4.0 * FMath::Pow(M, 0.2), 3.0, 12.0);
		}
		return FMath::Clamp(11.0 * FMath::Pow(M / 100.0, 0.05), 8.0, 16.0);
	case EPlanetClass::IceGiant:
		return FMath::Clamp(3.5 * FMath::Pow(M, 0.18), 2.5, 8.0);
	case EPlanetClass::Dwarf:
	default:
		return FMath::Clamp(0.9 * FMath::Pow(M, 0.3), 0.2, 1.2);
	}
}

double AlbedoFor(EPlanetClass Class)
{
	switch (Class)
	{
	case EPlanetClass::Rocky: return 0.25;
	case EPlanetClass::IceWorld:
	case EPlanetClass::Dwarf: return 0.45;
	case EPlanetClass::IceGiant: return 0.35;
	case EPlanetClass::GasGiant:
	default: return 0.3;
	}
}

uint32 PlanetCountTarget(FPugStream& Stream, double StarMass)
{
	double Base;
	if (StarMass < 0.45)
	{
		Base = 2.0 + 3.0 * Stream.NextF64();
	}
	else if (StarMass < 1.4)
	{
		Base = 4.0 + 4.0 * Stream.NextF64();
	}
	else
	{
		Base = 3.0 + 5.0 * Stream.NextF64();
	}
	return static_cast<uint32>(FMath::Clamp(FMath::RoundToDouble(Base), 1.0, 10.0));
}

EPlanetClass ClassifyPlanet(FPugStream& Stream, double A, double Snow, double StarMass)
{
	const double U = Stream.NextF64();
	if (A < Snow * 0.85)
	{
		if (A < 0.12 && StarMass > 0.5)
		{
			if (U < 0.48) return EPlanetClass::GasGiant;
			if (U < 0.88) return EPlanetClass::Rocky;
			return EPlanetClass::Dwarf;
		}
		if ((A < 0.45 && StarMass > 0.55 && U < 0.28) || (U < 0.12 && StarMass > 0.65 && A < 0.7))
		{
			return EPlanetClass::GasGiant;
		}
		return U < 0.75 ? EPlanetClass::Rocky : EPlanetClass::Dwarf;
	}
	if (A < Snow * 2.5)
	{
		if (U < 0.35) return EPlanetClass::IceGiant;
		if (U < 0.55) return EPlanetClass::GasGiant;
		if (U < 0.85) return EPlanetClass::IceWorld;
		return EPlanetClass::Dwarf;
	}
	if (U < 0.45) return EPlanetClass::GasGiant;
	if (U < 0.7) return EPlanetClass::IceGiant;
	if (U < 0.9) return EPlanetClass::IceWorld;
	return EPlanetClass::Dwarf;
}

double SampleMass(FPugStream& Stream, EPlanetClass Class)
{
	switch (Class)
	{
	case EPlanetClass::Dwarf: return 0.01 + Stream.NextF64() * 0.15;
	case EPlanetClass::Rocky: return 0.2 + FMath::Pow(Stream.NextF64(), 1.4) * 8.0;
	case EPlanetClass::IceWorld: return 1.0 + Stream.NextF64() * 12.0;
	case EPlanetClass::IceGiant: return 8.0 + Stream.NextF64() * 25.0;
	case EPlanetClass::GasGiant:
	default: return 50.0 + FMath::Pow(Stream.NextF64(), 0.7) * 400.0;
	}
}

TArray<FMoonRecord> GenerateMoons(FPugStream& Stream, uint64 StarId, uint32 PlanetIndex, EPlanetClass Class, double PlanetMassEarth)
{
	uint32 Count = 0;
	if (Class == EPlanetClass::GasGiant)
	{
		Count = Stream.NextF64() < 0.75 ? 1u + static_cast<uint32>(Stream.NextF64() * 3.0) : 0u;
	}
	else if (Class == EPlanetClass::IceGiant)
	{
		Count = Stream.NextF64() < 0.45 ? 1u : 0u;
	}

	TArray<FMoonRecord> Moons;
	for (uint32 I = 0; I < Count; ++I)
	{
		const uint64 MoonParent = StarId ^ ((static_cast<uint64>(PlanetIndex) + 1ull) * 17ull);
		const uint64 MoonSeed = PugDeriveObjectSeed(MoonParent, static_cast<uint64>(I) + 100ull, "moon");
		FPugStream MoonStream = FPugStream::FromSeed(MoonSeed);
		FMoonRecord Moon;
		Moon.MassEarth = 0.001 + MoonStream.NextF64() * 0.02 * FMath::Sqrt(PlanetMassEarth / 100.0);
		Moon.RadiusEarth = MassRadiusEarth(Moon.MassEarth, EPlanetClass::Dwarf);
		Moon.Orbit.SemiMajorAu = 0.01 + MoonStream.NextF64() * 0.04;
		Moon.Orbit.Eccentricity = 0.01 + MoonStream.NextF64() * 0.05;
		Moon.Orbit.InclinationRad = FMath::DegreesToRadians(0.5 + MoonStream.NextF64() * 4.0);
		Moon.Orbit.LongAscNodeRad = MoonStream.NextF64() * PugTau;
		Moon.Orbit.ArgPeriapsisRad = MoonStream.NextF64() * PugTau;
		Moon.Orbit.MeanAnomaly0Rad = MoonStream.NextF64() * PugTau;
		Moon.Orbit.PeriodDays = PeriodDays(Moon.Orbit.SemiMajorAu, FMath::Max(1e-8, PlanetMassEarth / 332946.0));
		Stream.NextF64();
		Moon.Id = MoonSeed; // stable derived seed (was synthetic 1000+index)
		Moons.Add(Moon);
	}
	return Moons;
}
}

const TCHAR* PugPlanetClassName(EPlanetClass Class)
{
	switch (Class)
	{
	case EPlanetClass::Rocky: return TEXT("rocky");
	case EPlanetClass::IceWorld: return TEXT("ice");
	case EPlanetClass::IceGiant: return TEXT("ice-giant");
	case EPlanetClass::GasGiant: return TEXT("gas-giant");
	case EPlanetClass::Dwarf:
	default: return TEXT("dwarf");
	}
}

FVector PugKeplerPositionAu(const FKeplerElements& Elements, double TimeDays)
{
	const double N = PugTau / FMath::Max(1e-9, Elements.PeriodDays);
	const double Mean = RemEuclid(Elements.MeanAnomaly0Rad + N * TimeDays, PugTau);
	const double E = SolveKepler(Mean, Elements.Eccentricity);
	const double CosE = FMath::Cos(E);
	const double SinE = FMath::Sin(E);
	const double Ecc = Elements.Eccentricity;
	const double A = Elements.SemiMajorAu;
	const double XOrb = A * (CosE - Ecc);
	const double YOrb = A * FMath::Sqrt(1.0 - Ecc * Ecc) * SinE;
	const double CosW = FMath::Cos(Elements.ArgPeriapsisRad);
	const double SinW = FMath::Sin(Elements.ArgPeriapsisRad);
	const double CosO = FMath::Cos(Elements.LongAscNodeRad);
	const double SinO = FMath::Sin(Elements.LongAscNodeRad);
	const double CosI = FMath::Cos(Elements.InclinationRad);
	const double SinI = FMath::Sin(Elements.InclinationRad);
	const double X = (CosO * CosW - SinO * SinW * CosI) * XOrb + (-CosO * SinW - SinO * CosW * CosI) * YOrb;
	const double Y = (SinO * CosW + CosO * SinW * CosI) * XOrb + (-SinO * SinW + CosO * CosW * CosI) * YOrb;
	const double Z = (SinW * SinI) * XOrb + (CosW * SinI) * YOrb;
	return FVector(X, Y, Z);
}

FPlanetarySystem PugGenerateSystem(uint64 StarId, const FStellarProperties& Stellar)
{
	const uint64 SystemSeed = PugDeriveObjectSeed(StarId, 0, "system");
	FPugStream Stream = FPugStream::FromSeed(SystemSeed);
	FPlanetarySystem System;
	System.StarId = StarId;
	System.StarMassSolar = Stellar.MassSolar;
	System.StarLuminositySolar = Stellar.LuminositySolar;
	System.SnowLineAu = SnowLineAu(Stellar.LuminositySolar);

	const double Inner = FMath::Clamp(0.15 * FMath::Sqrt(Stellar.MassSolar), 0.05, 0.4);
	const double Outer = FMath::Clamp(45.0 * FMath::Sqrt(Stellar.MassSolar), 12.0, 80.0);
	const uint32 TargetCount = PlanetCountTarget(Stream, Stellar.MassSolar);
	double A = Inner * (1.0 + 0.4 * Stream.NextF64());
	if (Stellar.MassSolar > 0.55 && Stream.NextF64() < 0.38)
	{
		A = FMath::Min(A, 0.028 + 0.07 * Stream.NextF64());
	}

	uint32 Index = 0;
	while (static_cast<uint32>(System.Planets.Num()) < TargetCount && A < Outer)
	{
		EPlanetClass Class = ClassifyPlanet(Stream, A, System.SnowLineAu, Stellar.MassSolar);
		if (A < 0.1 && Class != EPlanetClass::GasGiant && Stream.NextF64() < 0.65)
		{
			Class = EPlanetClass::GasGiant;
		}
		const double Mass = SampleMass(Stream, Class);
		const double Radius = MassRadiusEarth(Mass, Class);
		const double EccBase = (Class == EPlanetClass::GasGiant || Class == EPlanetClass::IceGiant) ? 0.05 : 0.08;
		const double Ecc = FMath::Clamp(EccBase + Stream.NextF64() * 0.18, 0.0, 0.4);
		const double InclDeg = Class == EPlanetClass::GasGiant
			? 1.0 + Stream.NextF64() * 6.0
			: 1.5 + Stream.NextF64() * 10.0;

		FPlanetRecord Planet;
		Planet.Class = Class;
		Planet.MassEarth = Mass;
		Planet.RadiusEarth = Radius;
		Planet.Orbit.SemiMajorAu = A;
		Planet.Orbit.Eccentricity = Ecc;
		Planet.Orbit.InclinationRad = FMath::DegreesToRadians(InclDeg);
		Planet.Orbit.LongAscNodeRad = Stream.NextF64() * PugTau;
		Planet.Orbit.ArgPeriapsisRad = Stream.NextF64() * PugTau;
		Planet.Orbit.MeanAnomaly0Rad = Stream.NextF64() * PugTau;
		Planet.Orbit.PeriodDays = PeriodDays(A, Stellar.MassSolar);
		if (!Planet.Orbit.Validate())
		{
			break;
		}

		Planet.EquilibriumTempK = EquilibriumTempK(Stellar.LuminositySolar, A, AlbedoFor(Class));
		if (A < 0.4 * FMath::Sqrt(Stellar.MassSolar))
		{
			Planet.Hazards.bTidalLock = true;
		}
		if (Planet.EquilibriumTempK > 400.0 || Planet.EquilibriumTempK < 150.0)
		{
			Planet.Hazards.bExtremeTemp = true;
		}
		if (Class == EPlanetClass::GasGiant || Class == EPlanetClass::IceGiant)
		{
			Planet.Hazards.bThickAtmosphere = true;
			if (Stream.NextF64() < 0.35)
			{
				Planet.Hazards.bRingSystem = true;
			}
		}
		if (Stellar.LuminositySolar > 4.0 && A < System.SnowLineAu)
		{
			Planet.Hazards.bHighRadiation = true;
		}
		Planet.Moons = GenerateMoons(Stream, StarId, Index, Class, Mass);
		const uint64 PlanetSeed = PugDeriveObjectSeed(StarId, static_cast<uint64>(Index) + 1ull, "planet");
		Planet.Id = PlanetSeed;
		System.Planets.Add(Planet);

		const double Hill = A * FMath::Pow(Mass / (3.0 * Stellar.MassSolar * 332946.0), 1.0 / 3.0);
		const double Spacing = (7.5 + 5.0 * Stream.NextF64()) * FMath::Max(0.05, Hill);
		A += FMath::Max(Spacing, 0.15 * A);
		++Index;
	}
	return System;
}
