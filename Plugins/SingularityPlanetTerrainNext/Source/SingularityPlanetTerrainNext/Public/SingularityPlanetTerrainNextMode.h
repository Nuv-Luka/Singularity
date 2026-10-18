#pragma once

#include "CoreMinimal.h"

/**
 * Which landable-moon renderer is allowed to own visibility, collision,
 * job scheduling, and GPU residency. Legacy and Next never run together.
 * Default is Legacy so the existing (currently disconnected) terrain path
 * stays the selected implementation until Next passes its gates.
 */
enum class ESingularityTerrainRendererMode : uint8
{
	Legacy = 0,
	Next = 1
};

/** Reads `singularity.TerrainRenderer.Mode` (0 = Legacy, 1 = Next). Any other value is Legacy. */
SINGULARITYPLANETTERRAINNEXT_API ESingularityTerrainRendererMode SingularityGetTerrainRendererMode();

SINGULARITYPLANETTERRAINNEXT_API bool SingularityTerrainRendererIsNext();
