#include "SingularityPlanetTerrainNextMode.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarSingularityTerrainRendererMode(
	TEXT("singularity.TerrainRenderer.Mode"),
	0,
	TEXT("Landable moon renderer. 0=Legacy (existing PlanetTerrain). 1=Next (SingularityPlanetTerrainNext). Exactly one owns the moon. Default Legacy."),
	ECVF_Default);

ESingularityTerrainRendererMode SingularityGetTerrainRendererMode()
{
	return CVarSingularityTerrainRendererMode.GetValueOnAnyThread() == static_cast<int32>(ESingularityTerrainRendererMode::Next)
		? ESingularityTerrainRendererMode::Next
		: ESingularityTerrainRendererMode::Legacy;
}

bool SingularityTerrainRendererIsNext()
{
	return SingularityGetTerrainRendererMode() == ESingularityTerrainRendererMode::Next;
}
