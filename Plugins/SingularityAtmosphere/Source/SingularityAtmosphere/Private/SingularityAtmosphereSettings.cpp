#include "SingularityAtmosphereSettings.h"
#include "SingularityAtmosphereTypes.h"
#include "SingularityAtmosphereSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace SingularityAtmosphere
{
	TAutoConsoleVariable<int32> CVarEnable(
		TEXT("r.SingularityAtmosphere.Enable"),
		1,
		TEXT("Enable SingularityAtmosphere post-process compute passes (1=on, 0=off / no subscription)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarDebug(
		TEXT("r.SingularityAtmosphere.Debug"),
		0,
		TEXT("0=scatter, 1=atmosphere mask, 2=segment length, 3=forced identity."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarQuality(
		TEXT("r.SingularityAtmosphere.Quality"),
		1,
		TEXT("0=orbital raymarch only, 1=auto sky-view/aerial when near/inside, 2=force sky-view/aerial."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarMaxPlanets(
		TEXT("r.SingularityAtmosphere.MaxPlanets"),
		2,
		TEXT("Max concurrent planet atmospheres (1–4). Nearest N within 30× body radius; farthest-first composite."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarShowTweaks(
		TEXT("r.SingularityAtmosphere.ShowTweaks"),
		0,
		TEXT("1=print live atmosphere tweak CVars on screen (system view)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarDensityScale(
		TEXT("r.SingularityAtmosphere.DensityScale"),
		1.0f,
		TEXT("Live multiplier on atmosphere density (optical depth). Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarRayleighScale(
		TEXT("r.SingularityAtmosphere.RayleighScale"),
		1.0f,
		TEXT("Live multiplier on Rayleigh scattering coefficients. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarMieScale(
		TEXT("r.SingularityAtmosphere.MieScale"),
		1.0f,
		TEXT("Live multiplier on Mie scattering + extinction. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarOzoneScale(
		TEXT("r.SingularityAtmosphere.OzoneScale"),
		1.0f,
		TEXT("Live multiplier on absorption (ozone) coefficients. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarHeightScale(
		TEXT("r.SingularityAtmosphere.HeightScale"),
		1.0f,
		TEXT("Live multiplier on atmosphere thickness (top-bottom). Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarRayleighHeightScale(
		TEXT("r.SingularityAtmosphere.RayleighHeightScale"),
		1.0f,
		TEXT("Live multiplier on Rayleigh scale height. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarMieHeightScale(
		TEXT("r.SingularityAtmosphere.MieHeightScale"),
		1.0f,
		TEXT("Live multiplier on Mie scale height. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarIlluminanceScale(
		TEXT("r.SingularityAtmosphere.IlluminanceScale"),
		1.0f,
		TEXT("Live multiplier on star illuminance into the atmosphere. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarFadeScale(
		TEXT("r.SingularityAtmosphere.FadeScale"),
		1.0f,
		TEXT("Live multiplier on atmosphere composite fade. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarGroundBounceScale(
		TEXT("r.SingularityAtmosphere.GroundBounceScale"),
		1.0f,
		TEXT("Live multiplier on ground bounce term. Default 1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarMieG(
		TEXT("r.SingularityAtmosphere.MieG"),
		-1.0f,
		TEXT("Mie anisotropy G override in [0,0.95]. Negative = use planet descriptor."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarDiskHaze(
		TEXT("r.SingularityAtmosphere.DiskHaze"),
		3.5f,
		TEXT("Density multiplier for planet-surface rays (fills disk haze). 1=physical thin, ~3–5=visible face."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarDiskOpacity(
		TEXT("r.SingularityAtmosphere.DiskOpacity"),
		1.0f,
		TEXT("Extra soft haze/opacity over the planet face (0=off, 1=default, 2=heavy)."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarColorPreset(
		TEXT("r.SingularityAtmosphere.ColorPreset"),
		0,
		TEXT("0=Neutral 1=Earth 2=Mars 3=Titan 4=Ice 5=Toxic 6=Sunset 7=Custom. Use SingularityAtmosphere.ColorPreset <n>."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<float> CVarRayleighTintR(TEXT("r.SingularityAtmosphere.RayleighTintR"), 1.0f, TEXT("Rayleigh R tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarRayleighTintG(TEXT("r.SingularityAtmosphere.RayleighTintG"), 1.0f, TEXT("Rayleigh G tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarRayleighTintB(TEXT("r.SingularityAtmosphere.RayleighTintB"), 1.0f, TEXT("Rayleigh B tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarMieTintR(TEXT("r.SingularityAtmosphere.MieTintR"), 1.0f, TEXT("Mie R tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarMieTintG(TEXT("r.SingularityAtmosphere.MieTintG"), 1.0f, TEXT("Mie G tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarMieTintB(TEXT("r.SingularityAtmosphere.MieTintB"), 1.0f, TEXT("Mie B tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarOzoneTintR(TEXT("r.SingularityAtmosphere.OzoneTintR"), 1.0f, TEXT("Ozone/absorption R tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarOzoneTintG(TEXT("r.SingularityAtmosphere.OzoneTintG"), 1.0f, TEXT("Ozone/absorption G tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarOzoneTintB(TEXT("r.SingularityAtmosphere.OzoneTintB"), 1.0f, TEXT("Ozone/absorption B tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarHazeTintR(TEXT("r.SingularityAtmosphere.HazeTintR"), 1.0f, TEXT("Disk haze R tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarHazeTintG(TEXT("r.SingularityAtmosphere.HazeTintG"), 1.0f, TEXT("Disk haze G tint."), ECVF_RenderThreadSafe);
	TAutoConsoleVariable<float> CVarHazeTintB(TEXT("r.SingularityAtmosphere.HazeTintB"), 1.0f, TEXT("Disk haze B tint."), ECVF_RenderThreadSafe);

	static void SetTint3(
		TAutoConsoleVariable<float>& R,
		TAutoConsoleVariable<float>& G,
		TAutoConsoleVariable<float>& B,
		float Rv, float Gv, float Bv)
	{
		R->Set(Rv, ECVF_SetByCode);
		G->Set(Gv, ECVF_SetByCode);
		B->Set(Bv, ECVF_SetByCode);
	}

	void ApplyColorPreset(int32 PresetIndex)
	{
		const int32 Clamped = FMath::Clamp(PresetIndex, 0, 7);
		CVarColorPreset->Set(Clamped, ECVF_SetByCode);
		if (Clamped == 7)
		{
			return; // Custom — keep current RGB tints
		}
		switch (Clamped)
		{
		case 1: // Earth
			SetTint3(CVarRayleighTintR, CVarRayleighTintG, CVarRayleighTintB, 0.75f, 1.05f, 1.65f);
			SetTint3(CVarMieTintR, CVarMieTintG, CVarMieTintB, 1.0f, 1.0f, 1.05f);
			SetTint3(CVarOzoneTintR, CVarOzoneTintG, CVarOzoneTintB, 1.0f, 1.15f, 0.55f);
			SetTint3(CVarHazeTintR, CVarHazeTintG, CVarHazeTintB, 0.45f, 0.72f, 1.2f);
			break;
		case 2: // Mars
			SetTint3(CVarRayleighTintR, CVarRayleighTintG, CVarRayleighTintB, 1.55f, 0.85f, 0.4f);
			SetTint3(CVarMieTintR, CVarMieTintG, CVarMieTintB, 1.45f, 0.9f, 0.5f);
			SetTint3(CVarOzoneTintR, CVarOzoneTintG, CVarOzoneTintB, 1.2f, 0.9f, 0.7f);
			SetTint3(CVarHazeTintR, CVarHazeTintG, CVarHazeTintB, 1.35f, 0.7f, 0.35f);
			break;
		case 3: // Titan
			SetTint3(CVarRayleighTintR, CVarRayleighTintG, CVarRayleighTintB, 1.1f, 0.85f, 0.35f);
			SetTint3(CVarMieTintR, CVarMieTintG, CVarMieTintB, 1.25f, 0.95f, 0.55f);
			SetTint3(CVarOzoneTintR, CVarOzoneTintG, CVarOzoneTintB, 1.0f, 0.85f, 0.5f);
			SetTint3(CVarHazeTintR, CVarHazeTintG, CVarHazeTintB, 1.15f, 0.75f, 0.3f);
			break;
		case 4: // Ice
			SetTint3(CVarRayleighTintR, CVarRayleighTintG, CVarRayleighTintB, 0.55f, 0.9f, 1.45f);
			SetTint3(CVarMieTintR, CVarMieTintG, CVarMieTintB, 0.85f, 0.95f, 1.15f);
			SetTint3(CVarOzoneTintR, CVarOzoneTintG, CVarOzoneTintB, 0.8f, 1.0f, 1.2f);
			SetTint3(CVarHazeTintR, CVarHazeTintG, CVarHazeTintB, 0.55f, 0.85f, 1.25f);
			break;
		case 5: // Toxic
			SetTint3(CVarRayleighTintR, CVarRayleighTintG, CVarRayleighTintB, 0.45f, 1.45f, 0.55f);
			SetTint3(CVarMieTintR, CVarMieTintG, CVarMieTintB, 0.7f, 1.3f, 0.6f);
			SetTint3(CVarOzoneTintR, CVarOzoneTintG, CVarOzoneTintB, 0.6f, 1.2f, 0.5f);
			SetTint3(CVarHazeTintR, CVarHazeTintG, CVarHazeTintB, 0.5f, 1.35f, 0.45f);
			break;
		case 6: // Sunset
			SetTint3(CVarRayleighTintR, CVarRayleighTintG, CVarRayleighTintB, 1.55f, 0.75f, 0.35f);
			SetTint3(CVarMieTintR, CVarMieTintG, CVarMieTintB, 1.4f, 0.85f, 0.45f);
			SetTint3(CVarOzoneTintR, CVarOzoneTintG, CVarOzoneTintB, 1.1f, 0.9f, 0.7f);
			SetTint3(CVarHazeTintR, CVarHazeTintG, CVarHazeTintB, 1.5f, 0.55f, 0.22f);
			break;
		default: // Neutral
			SetTint3(CVarRayleighTintR, CVarRayleighTintG, CVarRayleighTintB, 1.0f, 1.0f, 1.0f);
			SetTint3(CVarMieTintR, CVarMieTintG, CVarMieTintB, 1.0f, 1.0f, 1.0f);
			SetTint3(CVarOzoneTintR, CVarOzoneTintG, CVarOzoneTintB, 1.0f, 1.0f, 1.0f);
			SetTint3(CVarHazeTintR, CVarHazeTintG, CVarHazeTintB, 1.0f, 1.0f, 1.0f);
			break;
		}
	}

	FVector3f GetLiveHazeTint()
	{
		return FVector3f(
			FMath::Max(CVarHazeTintR.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarHazeTintG.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarHazeTintB.GetValueOnAnyThread(), 0.0f));
	}

	void ApplyLiveTweaksToPlanet(FSingularityAtmospherePlanetProxy& InOutPlanet)
	{
		const float Density = FMath::Max(CVarDensityScale.GetValueOnAnyThread(), 0.0f);
		const float Rayleigh = FMath::Max(CVarRayleighScale.GetValueOnAnyThread(), 0.0f);
		const float Mie = FMath::Max(CVarMieScale.GetValueOnAnyThread(), 0.0f);
		const float Ozone = FMath::Max(CVarOzoneScale.GetValueOnAnyThread(), 0.0f);
		const float Height = FMath::Max(CVarHeightScale.GetValueOnAnyThread(), 0.05f);
		const float RayH = FMath::Max(CVarRayleighHeightScale.GetValueOnAnyThread(), 0.05f);
		const float MieH = FMath::Max(CVarMieHeightScale.GetValueOnAnyThread(), 0.05f);
		const float Illum = FMath::Max(CVarIlluminanceScale.GetValueOnAnyThread(), 0.0f);
		const float Bounce = FMath::Max(CVarGroundBounceScale.GetValueOnAnyThread(), 0.0f);
		const float MieGOverride = CVarMieG.GetValueOnAnyThread();

		const FVector3f RayTint(
			FMath::Max(CVarRayleighTintR.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarRayleighTintG.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarRayleighTintB.GetValueOnAnyThread(), 0.0f));
		const FVector3f MieTint(
			FMath::Max(CVarMieTintR.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarMieTintG.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarMieTintB.GetValueOnAnyThread(), 0.0f));
		const FVector3f OzoneTint(
			FMath::Max(CVarOzoneTintR.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarOzoneTintG.GetValueOnAnyThread(), 0.0f),
			FMath::Max(CVarOzoneTintB.GetValueOnAnyThread(), 0.0f));

		InOutPlanet.DensityScale *= Density;
		InOutPlanet.RayleighScatteringPerKm *= Rayleigh * RayTint;
		InOutPlanet.MieScatteringPerKm *= Mie * MieTint;
		InOutPlanet.MieExtinctionPerKm *= Mie * MieTint;
		InOutPlanet.AbsorptionPerKm *= Ozone * OzoneTint;
		InOutPlanet.RayleighScaleHeightKm = FMath::Max(InOutPlanet.RayleighScaleHeightKm * RayH, 0.25f);
		InOutPlanet.MieScaleHeightKm = FMath::Max(InOutPlanet.MieScaleHeightKm * MieH, 0.1f);
		InOutPlanet.StarIlluminanceLinear *= Illum;
		InOutPlanet.GroundBounce = FMath::Max(InOutPlanet.GroundBounce * Bounce, 0.0f);

		const double Bottom = InOutPlanet.VisualBottomRadiusKm;
		const double Thickness = FMath::Max(InOutPlanet.VisualTopRadiusKm - Bottom, 1.0);
		InOutPlanet.VisualTopRadiusKm = Bottom + Thickness * static_cast<double>(Height);
		if (InOutPlanet.VisualTopRadiusKm <= Bottom * 1.001)
		{
			InOutPlanet.VisualTopRadiusKm = Bottom * 1.001;
		}

		if (MieGOverride >= 0.0f)
		{
			InOutPlanet.MieAnisotropyG = FMath::Clamp(MieGOverride, 0.0f, 0.95f);
		}

		InOutPlanet.AtmosphereProfileHash = ComputeSingularityAtmosphereProfileHash(InOutPlanet);
	}

	float GetLiveFadeScale()
	{
		return FMath::Max(CVarFadeScale.GetValueOnAnyThread(), 0.0f);
	}

	FString FormatLiveTweaksForDisplay()
	{
		return FString::Printf(
			TEXT("AtmTweaks dens=%.2f ray=%.2f mie=%.2f diskH=%.2f diskO=%.2f H=%.2f illum=%.2f fade=%.2f mieG=%s"),
			CVarDensityScale.GetValueOnAnyThread(),
			CVarRayleighScale.GetValueOnAnyThread(),
			CVarMieScale.GetValueOnAnyThread(),
			CVarDiskHaze.GetValueOnAnyThread(),
			CVarDiskOpacity.GetValueOnAnyThread(),
			CVarHeightScale.GetValueOnAnyThread(),
			CVarIlluminanceScale.GetValueOnAnyThread(),
			CVarFadeScale.GetValueOnAnyThread(),
			CVarMieG.GetValueOnAnyThread() < 0.0f
				? TEXT("desc")
				: *FString::Printf(TEXT("%.2f"), CVarMieG.GetValueOnAnyThread()));
	}

	void ResetLiveTweaks()
	{
		CVarDensityScale->Set(1.0f, ECVF_SetByCode);
		CVarRayleighScale->Set(1.0f, ECVF_SetByCode);
		CVarMieScale->Set(1.0f, ECVF_SetByCode);
		CVarOzoneScale->Set(1.0f, ECVF_SetByCode);
		CVarHeightScale->Set(1.0f, ECVF_SetByCode);
		CVarRayleighHeightScale->Set(1.0f, ECVF_SetByCode);
		CVarMieHeightScale->Set(1.0f, ECVF_SetByCode);
		CVarIlluminanceScale->Set(1.0f, ECVF_SetByCode);
		CVarFadeScale->Set(1.0f, ECVF_SetByCode);
		CVarGroundBounceScale->Set(1.0f, ECVF_SetByCode);
		CVarMieG->Set(-1.0f, ECVF_SetByCode);
		CVarDiskHaze->Set(3.5f, ECVF_SetByCode);
		CVarDiskOpacity->Set(1.0f, ECVF_SetByCode);
		ApplyColorPreset(0);
	}

	static FAutoConsoleCommand GCmdResetTweaks(
		TEXT("SingularityAtmosphere.ResetTweaks"),
		TEXT("Reset all r.SingularityAtmosphere.* live optical multipliers to defaults."),
		FConsoleCommandDelegate::CreateStatic(&ResetLiveTweaks));

	static FAutoConsoleCommand GCmdColorPreset(
		TEXT("SingularityAtmosphere.ColorPreset"),
		TEXT("Apply atmosphere color combo: 0 Neutral, 1 Earth, 2 Mars, 3 Titan, 4 Ice, 5 Toxic, 6 Sunset."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			ApplyColorPreset(Index);
		}));

	static FAutoConsoleCommand GCmdPrintTweaks(
		TEXT("SingularityAtmosphere.PrintTweaks"),
		TEXT("Print current live atmosphere tweak CVars to the log."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UE_LOG(LogTemp, Display, TEXT("%s"), *FormatLiveTweaksForDisplay());
			UE_LOG(LogTemp, Display,
				TEXT("CVars: DensityScale … MieG | ResetTweaks | ToggleUI (F7) | ShowTweaks 1"));
		}));

	static FAutoConsoleCommand GCmdToggleUI(
		TEXT("SingularityAtmosphere.ToggleUI"),
		TEXT("Toggle the on-screen atmosphere tweak panel (also F7 in PIE)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (!GEngine)
			{
				return;
			}
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UWorld* World = Ctx.World();
				if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE
					&& World->WorldType != EWorldType::GamePreview))
				{
					continue;
				}
				if (USingularityAtmosphereSubsystem* Atm = World->GetSubsystem<USingularityAtmosphereSubsystem>())
				{
					Atm->ToggleTweakUI();
					return;
				}
			}
		}));
}
