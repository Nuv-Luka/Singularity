#pragma once

#include "CoreMinimal.h"

/** Generator 0.4.0, packed the same way as galaxy-core. */
inline uint64 PugGeneratorVersion()
{
	return (0ull << 32) | (4ull << 16) | 0ull;
}

inline constexpr double PugTau = 6.28318530717958647692;

inline uint64 PugSplitMix64(uint64 X)
{
	X += 0x9E3779B97F4A7C15ull;
	uint64 Z = X;
	Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
	Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
	return Z ^ (Z >> 31);
}

struct FPugHasher
{
	uint64 State = 0x243F6A8885A308D3ull;

	void WriteU64(uint64 Value)
	{
		State = PugSplitMix64(State ^ Value);
	}

	void WriteBytes(const uint8* Bytes, int32 Len)
	{
		uint8 Chunk[8];
		for (int32 Offset = 0; Offset < Len; Offset += 8)
		{
			FMemory::Memzero(Chunk, 8);
			const int32 N = FMath::Min(8, Len - Offset);
			FMemory::Memcpy(Chunk, Bytes + Offset, N);
			uint64 Word = 0;
			FMemory::Memcpy(&Word, Chunk, 8);
			WriteU64(Word);
		}
		WriteU64(static_cast<uint64>(Len));
	}

	uint64 Finish() const
	{
		return PugSplitMix64(State);
	}
};

inline uint64 PugTag(const ANSICHAR* Text)
{
	FPugHasher Hasher;
	Hasher.WriteBytes(reinterpret_cast<const uint8*>(Text), FCStringAnsi::Strlen(Text));
	return Hasher.Finish();
}

inline uint64 PugDoubleBits(double Value)
{
	uint64 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	return Bits;
}

struct FPugStream
{
	uint64 State = 0;

	static FPugStream FromRaw(uint64 Raw)
	{
		FPugStream Stream;
		Stream.State = PugSplitMix64(Raw);
		return Stream;
	}

	static FPugStream FromSeed(uint64 Seed)
	{
		return FromRaw(Seed ^ PugGeneratorVersion());
	}

	uint64 NextU64()
	{
		const uint64 Value = PugSplitMix64(State);
		State += 0x9E3779B97F4A7C15ull;
		return Value;
	}

	double NextF64()
	{
		const uint64 Bits = NextU64() >> 11;
		return static_cast<double>(Bits) * (1.0 / static_cast<double>(1ull << 53));
	}
};

inline uint64 PugDeriveSectorSeed(uint64 GalaxySeed, uint64 GalaxyId, uint64 SectorId)
{
	FPugHasher Hasher;
	Hasher.WriteU64(GalaxySeed);
	Hasher.WriteU64(GalaxyId);
	Hasher.WriteU64(SectorId);
	Hasher.WriteU64(PugGeneratorVersion());
	Hasher.WriteU64(PugTag("sector"));
	return Hasher.Finish();
}

inline uint64 PugDeriveObjectSeed(uint64 Parent, uint64 ObjectId, const ANSICHAR* Domain)
{
	FPugHasher Hasher;
	Hasher.WriteU64(Parent);
	Hasher.WriteU64(ObjectId);
	Hasher.WriteU64(PugGeneratorVersion());
	Hasher.WriteU64(PugTag(Domain));
	return Hasher.Finish();
}

struct FSectorCoord
{
	int16 X = 0;
	int16 Y = 0;
	int16 Z = 0;

	uint64 ToSectorId() const
	{
		const uint64 LaneX = static_cast<uint16>(X);
		const uint64 LaneY = static_cast<uint16>(Y);
		const uint64 LaneZ = static_cast<uint16>(Z);
		return (LaneX << 32) | (LaneY << 16) | LaneZ;
	}

	static FSectorCoord FromSectorId(uint64 SectorId)
	{
		FSectorCoord Coord;
		Coord.X = static_cast<int16>(static_cast<uint16>((SectorId >> 32) & 0xFFFFu));
		Coord.Y = static_cast<int16>(static_cast<uint16>((SectorId >> 16) & 0xFFFFu));
		Coord.Z = static_cast<int16>(static_cast<uint16>(SectorId & 0xFFFFu));
		return Coord;
	}

	static FSectorCoord FromPositionPc(const double PositionPc[3], double SectorSizePc)
	{
		const double Size = FMath::Max(1.0, SectorSizePc);
		FSectorCoord Coord;
		Coord.X = static_cast<int16>(FMath::FloorToInt(PositionPc[0] / Size));
		Coord.Y = static_cast<int16>(FMath::FloorToInt(PositionPc[1] / Size));
		Coord.Z = static_cast<int16>(FMath::FloorToInt(PositionPc[2] / Size));
		return Coord;
	}

	bool operator==(const FSectorCoord& Other) const
	{
		return X == Other.X && Y == Other.Y && Z == Other.Z;
	}
};

inline uint32 GetTypeHash(const FSectorCoord& Coord)
{
	return GetTypeHash(Coord.ToSectorId());
}

enum class ESpectralClass : uint8
{
	O, B, A, F, G, K, M
};

enum class EStarStage : uint8
{
	MainSequence,
	PostMainSequence
};

enum class EPlanetClass : uint8
{
	Rocky,
	IceWorld,
	IceGiant,
	GasGiant,
	Dwarf
};

struct FStellarProperties
{
	double MassSolar = 1.0;
	double AgeGyr = 4.5;
	double MetallicityFeH = 0.0;
	double TeffK = 5772.0;
	double LuminositySolar = 1.0;
	double RadiusSolar = 1.0;
	ESpectralClass Spectral = ESpectralClass::G;
	EStarStage Stage = EStarStage::MainSequence;
};

struct FStarRecord
{
	uint64 Id = 0;
	double PositionPc[3] = {0, 0, 0};
	FStellarProperties Stellar;
};

struct FKeplerElements
{
	double SemiMajorAu = 1.0;
	double Eccentricity = 0.0;
	double InclinationRad = 0.0;
	double LongAscNodeRad = 0.0;
	double ArgPeriapsisRad = 0.0;
	double MeanAnomaly0Rad = 0.0;
	double PeriodDays = 365.25;

	bool Validate() const
	{
		return SemiMajorAu > 0.0
			&& Eccentricity >= 0.0 && Eccentricity < 0.95
			&& PeriodDays > 0.0
			&& FMath::IsFinite(InclinationRad)
			&& FMath::IsFinite(LongAscNodeRad)
			&& FMath::IsFinite(ArgPeriapsisRad)
			&& FMath::IsFinite(MeanAnomaly0Rad);
	}
};

struct FHazardFlags
{
	bool bTidalLock = false;
	bool bHighRadiation = false;
	bool bExtremeTemp = false;
	bool bThickAtmosphere = false;
	bool bRingSystem = false;
};

struct FMoonRecord
{
	uint64 Id = 0;
	double MassEarth = 0.01;
	double RadiusEarth = 0.3;
	FKeplerElements Orbit;
};

struct FPlanetRecord
{
	uint64 Id = 0;
	EPlanetClass Class = EPlanetClass::Rocky;
	double MassEarth = 1.0;
	double RadiusEarth = 1.0;
	FKeplerElements Orbit;
	double EquilibriumTempK = 278.0;
	FHazardFlags Hazards;
	TArray<FMoonRecord> Moons;
};

struct FPlanetarySystem
{
	uint64 StarId = 0;
	double StarMassSolar = 1.0;
	double StarLuminositySolar = 1.0;
	double SnowLineAu = 2.7;
	TArray<FPlanetRecord> Planets;
};

struct FGalaxyProfile
{
	double SectorSizePc = 250.0;
	uint32 MaxStarsPerSector = 96;
	double TracerStarsAtUnitDensity = 32.0;
	double LocalRadiusPc = 8000.0;
	double DiskRho0 = 1.0;
	double DiskRadialScalePc = 3500.0;
	double DiskVerticalScalePc = 300.0;
	double DiskTruncationRadiusPc = 15500.0;
	double DiskEdgeSoftnessPc = 2800.0;
	double BulgeRho0 = 3.5;
	double BulgeScalePc = 700.0;
	double SpiralAmplitude = 0.65;
	uint32 SpiralArms = 2;
	double SpiralPitchRad = 0.22;
	double SpiralPhaseRad = 0.35;
};

struct FGeneratedSector
{
	uint64 SectorId = 0;
	double MinPc[3] = {0, 0, 0};
	double MaxPc[3] = {0, 0, 0};
	TArray<FStarRecord> Stars;
};

enum class EPugCubeFace : uint8
{
	PosX = 0,
	NegX = 1,
	PosY = 2,
	NegY = 3,
	PosZ = 4,
	NegZ = 5
};

enum class ERockPreset : uint8
{
	Rocky,
	Cratered
};

/** Unreal: 1 uu = 1 cm. These convert SI / astronomy units into uu. */
inline constexpr double PugCmPerM = 100.0;
inline constexpr double PugEarthRadiusM = 6'371'000.0;
inline constexpr double PugSolarRadiusM = 6.957e8;
inline constexpr double PugAuM = 149'597'870'700.0;
inline constexpr double PugPcM = 3.0856775814913673e16;

inline double PugMToUu(double Meters) { return Meters * PugCmPerM; }
inline double PugEarthRadiiToUu(double RadiusEarth) { return PugMToUu(RadiusEarth * PugEarthRadiusM); }
inline double PugSolarRadiiToUu(double RadiusSolar) { return PugMToUu(RadiusSolar * PugSolarRadiusM); }
inline double PugAuToUu(double Au) { return PugMToUu(Au * PugAuM); }
inline double PugPcToUu(double Pc) { return PugMToUu(Pc * PugPcM); }
inline float PugSphereMeshScale(double RadiusUu) { return static_cast<float>(RadiusUu / 50.0); }

enum class EPugAtmosphereKind : uint8
{
	EarthLike,
	ThinDusty,
	DenseHazy,
	HydrogenGiant,
	ThinIcy
};

struct FPugAtmosphereDescriptor
{
	EPugAtmosphereKind Kind = EPugAtmosphereKind::EarthLike;
	double InnerRadiusM = PugEarthRadiusM;
	double OuterRadiusM = PugEarthRadiusM + 100'000.0;
	FLinearColor GroundAlbedo = FLinearColor(0.3f, 0.3f, 0.3f);
	FLinearColor RayleighScattering = FLinearColor(5.802e-6f, 13.558e-6f, 33.100e-6f);
	float RayleighScaleHeightM = 8000.0f;
	FLinearColor MieScattering = FLinearColor(0.444e-6f, 0.444e-6f, 0.444e-6f);
	FLinearColor MieAbsorption = FLinearColor(3.996e-6f, 3.996e-6f, 3.996e-6f);
	float MieScaleHeightM = 1200.0f;
	float MieAsymmetry = 0.8f;
	FLinearColor OzoneAbsorption = FLinearColor(0.650e-6f, 1.881e-6f, 0.085e-6f);
	float ArtisticIntensity = 1.0f;
	FLinearColor LimbColor = FLinearColor(0.25f, 0.55f, 1.0f);
};

double PugSampleDensity(const FGalaxyProfile& Profile, const double PositionPc[3]);
FGeneratedSector PugGenerateSector(uint64 GalaxySeed, uint64 GalaxyId, FSectorCoord Coord, const FGalaxyProfile& Profile);
FPlanetarySystem PugGenerateSystem(uint64 StarId, const FStellarProperties& Stellar);
FLinearColor PugBakeTileAlbedo(uint64 PlanetId, EPlanetClass Class, double EquilibriumTempK, EPugCubeFace Face, double U, double V);
const TCHAR* PugPlanetClassName(EPlanetClass Class);
FLinearColor PugStarColor(double TeffK);
FVector PugKeplerPositionAu(const FKeplerElements& Elements, double TimeDays);
bool PugAtmosphereForPlanet(const FPlanetRecord& Planet, FPugAtmosphereDescriptor& OutDescriptor);
