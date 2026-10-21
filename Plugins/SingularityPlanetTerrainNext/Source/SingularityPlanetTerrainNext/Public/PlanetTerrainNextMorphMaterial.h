#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Loads (or, in editor, creates) the terrain patch material (versioned asset).
 *
 * R11 (v7): MASKED + genuinely complementary screen-space LodFade dither.
 *   LodFade 0→1 shared progress; LodFadeRole 0=parent (1-T), 1=child (T).
 *   OpacityMask = DitherTemporalAA(EffectiveAlpha).
 *
 * Legacy morph WPO still present (gated by EnableLegacyMorph at runtime).
 * Detail projection params unchanged from v4.
 */
namespace PlanetTerrainNextMorphMaterial
{
inline constexpr int32 MaterialFormatVersion = 7;

inline constexpr TCHAR Path[] =
	TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v5.M_PlanetTerrainNextPatch_v5");
inline constexpr TCHAR TransitionPath[] =
	TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v7.M_PlanetTerrainNextPatch_v7");
inline constexpr TCHAR StablePath[] =
	TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v6_Stable.M_PlanetTerrainNextPatch_v6_Stable");

/** Legacy name retained as the transition material accessor. */
SINGULARITYPLANETTERRAINNEXT_API UMaterialInterface* GetOrCreate();
SINGULARITYPLANETTERRAINNEXT_API UMaterialInterface* GetOrCreateTransition();
SINGULARITYPLANETTERRAINNEXT_API UMaterialInterface* GetOrCreateStable();
}
