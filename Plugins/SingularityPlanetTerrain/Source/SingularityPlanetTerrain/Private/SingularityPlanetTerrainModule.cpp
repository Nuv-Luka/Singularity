#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogSingularityPlanetTerrain, Log, All);

class FSingularityPlanetTerrainModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SingularityPlanetTerrain"));
		if (!Plugin.IsValid())
		{
			UE_LOG(LogSingularityPlanetTerrain, Error, TEXT("SingularityPlanetTerrain plugin not found; shader mapping skipped."));
			return;
		}
		const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/SingularityPlanetTerrain"), PluginShaderDir);
		UE_LOG(LogSingularityPlanetTerrain, Log, TEXT("Mapped /SingularityPlanetTerrain -> %s"), *PluginShaderDir);
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FSingularityPlanetTerrainModule, SingularityPlanetTerrain);
