#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Loads (or, in editor, creates) the terrain patch material (versioned asset).
 *
 * R10.2 (v5): MASKED + complementary LodFade dither (Cesium-style).
 *   LodFade 0→1 shared progress; LodFadeRole 0=parent (1-T), 1=child (T).
 *   OpacityMask = DitherTemporalAA(EffectiveAlpha).
 *
 * Legacy morph WPO still present (gated by EnableLegacyMorph at runtime).
 * Detail projection params unchanged from v4.
 */
namespace PlanetTerrainNextMorphMaterial
{
inline constexpr int32 MaterialFormatVersion = 5;

inline constexpr TCHAR Path[] =
	TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v5.M_PlanetTerrainNextPatch_v5");

SINGULARITYPLANETTERRAINNEXT_API UMaterialInterface* GetOrCreate();
}
