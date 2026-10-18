#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Loads (or, in editor, creates) the R3 morph material.
 *
 * Vertex inputs from ProceduralMesh:
 *   Position = fine (cm, patch-local)
 *   UV1.xy + UV2.x = coarse - fine (cm)
 *   VertexColor.rgb = coarse normal * 0.5 + 0.5
 *
 * Scalar parameter Morph: 0 = fine, 1 = parent-compatible coarse (DSE convention).
 * Vector parameter Color: albedo.
 * WPO = TransformLocalVectorToWorld((Coarse - Fine) * Morph). No mesh rebuilds.
 */
namespace PlanetTerrainNextMorphMaterial
{
inline constexpr TCHAR Path[] = TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch.M_PlanetTerrainNextPatch");

SINGULARITYPLANETTERRAINNEXT_API UMaterialInterface* GetOrCreate();
}
