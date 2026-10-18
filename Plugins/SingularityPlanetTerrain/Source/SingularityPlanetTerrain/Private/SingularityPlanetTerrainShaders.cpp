#include "SingularityPlanetTerrainShaders.h"
#include "DataDrivenShaderPlatformInfo.h"

IMPLEMENT_GLOBAL_SHADER(
	FSingularityPlanetTerrainHeightBakeCS,
	"/SingularityPlanetTerrain/Private/PlanetTerrainHeightBakeCS.usf",
	"MainCS",
	SF_Compute);
