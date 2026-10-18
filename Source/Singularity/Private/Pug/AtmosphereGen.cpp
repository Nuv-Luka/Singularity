#include "Pug/PugCore.h"

namespace
{
bool ClassifyAtmosphere(const FPlanetRecord& Planet, EPugAtmosphereKind& OutKind)
{
	switch (Planet.Class)
	{
	case EPlanetClass::GasGiant:
	case EPlanetClass::IceGiant:
		OutKind = EPugAtmosphereKind::HydrogenGiant;
		return true;
	case EPlanetClass::IceWorld:
		OutKind = EPugAtmosphereKind::ThinIcy;
		return true;
	case EPlanetClass::Dwarf:
		return false;
	case EPlanetClass::Rocky:
		if (Planet.Hazards.bThickAtmosphere)
		{
			OutKind = EPugAtmosphereKind::DenseHazy;
			return true;
		}
		if (Planet.EquilibriumTempK >= 220.0 && Planet.EquilibriumTempK < 320.0)
		{
			OutKind = EPugAtmosphereKind::EarthLike;
			return true;
		}
		if ((Planet.EquilibriumTempK >= 150.0 && Planet.EquilibriumTempK < 220.0)
			|| (Planet.EquilibriumTempK >= 320.0 && Planet.EquilibriumTempK < 450.0))
		{
			OutKind = EPugAtmosphereKind::ThinDusty;
			return true;
		}
		if (Planet.EquilibriumTempK >= 450.0)
		{
			OutKind = EPugAtmosphereKind::DenseHazy;
			return true;
		}
		return false;
	default:
		return false;
	}
}

FLinearColor LimbForKind(EPugAtmosphereKind Kind, double TeqK)
{
	const float Hot = static_cast<float>(FMath::Clamp((TeqK - 255.0) / 350.0, -0.35, 0.55));
	switch (Kind)
	{
	case EPugAtmosphereKind::ThinDusty: return FLinearColor(0.85f, 0.45f, 0.22f);
	case EPugAtmosphereKind::DenseHazy: return FLinearColor(0.85f, 0.78f, 0.55f);
	case EPugAtmosphereKind::HydrogenGiant:
		return FLinearColor(
			FMath::Clamp(0.98f + Hot * 0.02f, 0.9f, 1.0f),
			FMath::Clamp(0.78f - Hot * 0.08f, 0.55f, 0.95f),
			FMath::Clamp(0.38f - Hot * 0.1f, 0.2f, 0.55f));
	case EPugAtmosphereKind::ThinIcy: return FLinearColor(0.45f, 0.7f, 0.95f);
	case EPugAtmosphereKind::EarthLike:
	default: return FLinearColor(0.25f, 0.55f, 1.0f);
	}
}
}

bool PugAtmosphereForPlanet(const FPlanetRecord& Planet, FPugAtmosphereDescriptor& OutDescriptor)
{
	EPugAtmosphereKind Kind;
	if (!ClassifyAtmosphere(Planet, Kind))
	{
		return false;
	}

	const float IntensityHint = Kind == EPugAtmosphereKind::HydrogenGiant ? 0.34f
		: Kind == EPugAtmosphereKind::DenseHazy ? 1.35f
		: Kind == EPugAtmosphereKind::ThinDusty ? 0.8f
		: Kind == EPugAtmosphereKind::ThinIcy ? 0.7f
		: 1.25f;

	const double Inner = FMath::Max(50'000.0, Planet.RadiusEarth * PugEarthRadiusM);
	const FLinearColor EarthRayleigh(5.802e-6f, 13.558e-6f, 33.100e-6f);
	const FLinearColor EarthMieS(0.444e-6f, 0.444e-6f, 0.444e-6f);
	const FLinearColor EarthMieA(3.996e-6f, 3.996e-6f, 3.996e-6f);
	const FLinearColor EarthOzone(0.650e-6f, 1.881e-6f, 0.085e-6f);

	OutDescriptor = {};
	OutDescriptor.Kind = Kind;
	OutDescriptor.InnerRadiusM = Inner;
	OutDescriptor.LimbColor = LimbForKind(Kind, Planet.EquilibriumTempK);

	switch (Kind)
	{
	case EPugAtmosphereKind::EarthLike:
		OutDescriptor.OuterRadiusM = Inner + 100'000.0;
		OutDescriptor.GroundAlbedo = FLinearColor(0.3f, 0.3f, 0.3f);
		OutDescriptor.RayleighScattering = EarthRayleigh;
		OutDescriptor.RayleighScaleHeightM = 8000.0f;
		OutDescriptor.MieScattering = EarthMieS;
		OutDescriptor.MieAbsorption = EarthMieA;
		OutDescriptor.MieScaleHeightM = 1200.0f;
		OutDescriptor.MieAsymmetry = 0.8f;
		OutDescriptor.OzoneAbsorption = EarthOzone;
		OutDescriptor.ArtisticIntensity = FMath::Clamp(IntensityHint, 0.7f, 1.2f);
		break;
	case EPugAtmosphereKind::ThinDusty:
		OutDescriptor.OuterRadiusM = Inner + 80'000.0;
		OutDescriptor.GroundAlbedo = FLinearColor(0.15f, 0.1f, 0.08f);
		OutDescriptor.RayleighScattering = FLinearColor(EarthRayleigh.R * 0.15f, EarthRayleigh.G * 0.12f, EarthRayleigh.B * 0.1f);
		OutDescriptor.RayleighScaleHeightM = 11000.0f;
		OutDescriptor.MieScattering = FLinearColor(2.5e-6f, 1.8e-6f, 1.0e-6f);
		OutDescriptor.MieAbsorption = FLinearColor(1.2e-6f, 0.9e-6f, 0.6e-6f);
		OutDescriptor.MieScaleHeightM = 8000.0f;
		OutDescriptor.MieAsymmetry = 0.7f;
		OutDescriptor.ArtisticIntensity = FMath::Clamp(IntensityHint, 0.45f, 0.9f);
		break;
	case EPugAtmosphereKind::DenseHazy:
		OutDescriptor.OuterRadiusM = Inner + 180'000.0;
		OutDescriptor.GroundAlbedo = FLinearColor(0.55f, 0.5f, 0.35f);
		OutDescriptor.RayleighScattering = FLinearColor(EarthRayleigh.R * 0.4f, EarthRayleigh.G * 0.35f, EarthRayleigh.B * 0.25f);
		OutDescriptor.RayleighScaleHeightM = 10000.0f;
		OutDescriptor.MieScattering = FLinearColor(4.0e-6f, 3.5e-6f, 2.5e-6f);
		OutDescriptor.MieAbsorption = FLinearColor(2.5e-6f, 2.2e-6f, 1.8e-6f);
		OutDescriptor.MieScaleHeightM = 3000.0f;
		OutDescriptor.MieAsymmetry = 0.75f;
		OutDescriptor.ArtisticIntensity = FMath::Clamp(IntensityHint, 0.85f, 1.3f);
		break;
	case EPugAtmosphereKind::HydrogenGiant:
		OutDescriptor.OuterRadiusM = Inner + Inner * 0.11;
		OutDescriptor.GroundAlbedo = FLinearColor(0.45f, 0.35f, 0.25f);
		OutDescriptor.RayleighScattering = FLinearColor(EarthRayleigh.R * 0.38f, EarthRayleigh.G * 0.32f, EarthRayleigh.B * 0.26f);
		OutDescriptor.RayleighScaleHeightM = 26000.0f;
		OutDescriptor.MieScattering = FLinearColor(0.48e-6f, 0.38e-6f, 0.24e-6f);
		OutDescriptor.MieAbsorption = FLinearColor(0.22e-6f, 0.18e-6f, 0.12e-6f);
		OutDescriptor.MieScaleHeightM = 10000.0f;
		OutDescriptor.MieAsymmetry = 0.82f;
		OutDescriptor.ArtisticIntensity = FMath::Clamp(IntensityHint, 0.24f, 0.45f);
		break;
	case EPugAtmosphereKind::ThinIcy:
		OutDescriptor.OuterRadiusM = Inner + 40'000.0;
		OutDescriptor.GroundAlbedo = FLinearColor(0.55f, 0.6f, 0.7f);
		OutDescriptor.RayleighScattering = FLinearColor(EarthRayleigh.R * 0.35f, EarthRayleigh.G * 0.4f, EarthRayleigh.B * 0.5f);
		OutDescriptor.RayleighScaleHeightM = 7000.0f;
		OutDescriptor.MieScattering = FLinearColor(0.2e-6f, 0.2e-6f, 0.25e-6f);
		OutDescriptor.MieAbsorption = FLinearColor(0.15e-6f, 0.15e-6f, 0.15e-6f);
		OutDescriptor.MieScaleHeightM = 2000.0f;
		OutDescriptor.MieAsymmetry = 0.6f;
		OutDescriptor.ArtisticIntensity = FMath::Clamp(IntensityHint, 0.35f, 0.7f);
		break;
	}
	return true;
}
