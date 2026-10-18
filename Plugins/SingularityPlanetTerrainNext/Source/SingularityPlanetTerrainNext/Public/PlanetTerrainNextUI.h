#pragma once

#include "CoreMinimal.h"

/** Viewport overlay for spawning / destroying R1 Next moons (F6). */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextUI
{
	static void Toggle();
	static bool IsOpen();
	static void Close();
};
