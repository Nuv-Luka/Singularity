#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainComponent.h"

/** Console/HUD helpers for planet terrain diagnostics. */
struct FPlanetTerrainDebug
{
	static void DrawHud(UCanvas* Canvas, UPlanetTerrainComponent* Terrain);
	static void RegisterConsoleCommands();
	static void UnregisterConsoleCommands();

	static TWeakObjectPtr<UPlanetTerrainComponent> ActiveTerrain;
	static bool bFreezeLod;
	static bool bForceCpu;
	static bool bShowFaces;
	static bool bShowSeams;
	static bool bShowCollision;
	static bool bGeomorph;

	/** Emergency repair / streaming v2 debug modes. */
	static bool bForceShellVisible;
	static bool bShowRequestedTiles;
	static bool bShowCulledTiles;
	static bool bShowCoverage;
	static bool bShowRejectionReason;
	static bool bShowMorphAlpha;
	static bool bShowFoveation;
	static bool bPauseGeneration;
};

void PlanetTerrainRegisterDebugCommands();
void PlanetTerrainUnregisterDebugCommands();
