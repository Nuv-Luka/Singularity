#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

struct FSingularityAtmospherePlanetProxy;

namespace SingularityAtmosphere
{
	extern TAutoConsoleVariable<int32> CVarEnable;
	extern TAutoConsoleVariable<int32> CVarDebug;
	extern TAutoConsoleVariable<int32> CVarQuality;
	extern TAutoConsoleVariable<int32> CVarMaxPlanets;
	extern TAutoConsoleVariable<int32> CVarShowTweaks;

	/** Live optical multipliers / overrides (defaults = identity). Safe to change at runtime. */
	extern TAutoConsoleVariable<float> CVarDensityScale;
	extern TAutoConsoleVariable<float> CVarRayleighScale;
	extern TAutoConsoleVariable<float> CVarMieScale;
	extern TAutoConsoleVariable<float> CVarOzoneScale;
	extern TAutoConsoleVariable<float> CVarHeightScale;
	extern TAutoConsoleVariable<float> CVarRayleighHeightScale;
	extern TAutoConsoleVariable<float> CVarMieHeightScale;
	extern TAutoConsoleVariable<float> CVarIlluminanceScale;
	extern TAutoConsoleVariable<float> CVarFadeScale;
	extern TAutoConsoleVariable<float> CVarGroundBounceScale;
	/** Mie G anisotropy override; < 0 means keep planet descriptor value. */
	extern TAutoConsoleVariable<float> CVarMieG;
	/** Density multiplier for rays that hit the planet (fills the disk, not only the limb). */
	extern TAutoConsoleVariable<float> CVarDiskHaze;
	/** Extra soft haze / opacity over the planet face. */
	extern TAutoConsoleVariable<float> CVarDiskOpacity;

	/** Color preset index (0=Neutral … see ApplyColorPreset). */
	extern TAutoConsoleVariable<int32> CVarColorPreset;

	/** Per-channel tint multipliers (1 = descriptor / neutral). */
	extern TAutoConsoleVariable<float> CVarRayleighTintR;
	extern TAutoConsoleVariable<float> CVarRayleighTintG;
	extern TAutoConsoleVariable<float> CVarRayleighTintB;
	extern TAutoConsoleVariable<float> CVarMieTintR;
	extern TAutoConsoleVariable<float> CVarMieTintG;
	extern TAutoConsoleVariable<float> CVarMieTintB;
	extern TAutoConsoleVariable<float> CVarOzoneTintR;
	extern TAutoConsoleVariable<float> CVarOzoneTintG;
	extern TAutoConsoleVariable<float> CVarOzoneTintB;
	extern TAutoConsoleVariable<float> CVarHazeTintR;
	extern TAutoConsoleVariable<float> CVarHazeTintG;
	extern TAutoConsoleVariable<float> CVarHazeTintB;

	inline bool IsEnabled()
	{
		return CVarEnable.GetValueOnAnyThread() != 0;
	}

	/** Apply console tweaks to a planet proxy and recompute AtmosphereProfileHash (LUT cache key). */
	SINGULARITYATMOSPHERE_API void ApplyLiveTweaksToPlanet(FSingularityAtmospherePlanetProxy& InOutPlanet);

	SINGULARITYATMOSPHERE_API float GetLiveFadeScale();

	/** Soft-disk haze RGB (game/render thread). */
	SINGULARITYATMOSPHERE_API FVector3f GetLiveHazeTint();

	/** Apply a named color combination (updates tint CVars + preset index). */
	SINGULARITYATMOSPHERE_API void ApplyColorPreset(int32 PresetIndex);

	/** One-line / multi-line summary for on-screen debug. */
	SINGULARITYATMOSPHERE_API FString FormatLiveTweaksForDisplay();

	SINGULARITYATMOSPHERE_API void ResetLiveTweaks();
}
