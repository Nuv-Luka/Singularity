#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Loads (or, in editor, creates) the R9.3 morph + detail material (versioned asset).
 *
 * Vertex inputs from ProceduralMesh:
 *   Position = fine (cm, patch-local)
 *   UV0 = patch UV [0,1]
 *   UV1.xy + UV2.x = normalized (Coarse - Fine) / MorphScaleCm
 *   VertexColor.rgb = coarse normal * 0.5 + 0.5
 *
 * Scalar Morph: 0 = fine, 1 = parent-compatible coarse.
 * Scalar MorphScaleCm: max |Coarse-Fine| in cm (MID per patch).
 * Vector Color: fallback albedo (debug / pre-detail).
 * DetailAlbedo / DetailNormal: root-face (or ancestor) textures.
 * DetailUVOffset.xy + DetailUVScale: FaceUV = PatchUV * Scale + Offset.
 * DetailBlend: 0 = Color only, 1 = detail maps.
 * WPO = TransformLocalVectorToWorld(NormDelta * Morph * MorphScaleCm).
 */
namespace PlanetTerrainNextMorphMaterial
{
/** Bump when graph contract changes so GetOrCreate does not reuse a stale asset. */
inline constexpr int32 MaterialFormatVersion = 4;

inline constexpr TCHAR Path[] =
	TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v4.M_PlanetTerrainNextPatch_v4");

SINGULARITYPLANETTERRAINNEXT_API UMaterialInterface* GetOrCreate();
}
