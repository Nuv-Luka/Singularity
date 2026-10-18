#include "PlanetTerrainNextLog.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogPlanetTerrainNext);

class FSingularityPlanetTerrainNextModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SingularityPlanetTerrainNext"));
		if (Plugin.IsValid())
		{
			const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/SingularityPlanetTerrainNext"), ShaderDir);
			UE_LOG(LogPlanetTerrainNext, Log, TEXT("Mapped /SingularityPlanetTerrainNext -> %s"), *ShaderDir);
		}
		else
		{
			UE_LOG(LogPlanetTerrainNext, Error, TEXT("Plugin descriptor not found; shader mapping skipped."));
		}
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FSingularityPlanetTerrainNextModule, SingularityPlanetTerrainNext);
