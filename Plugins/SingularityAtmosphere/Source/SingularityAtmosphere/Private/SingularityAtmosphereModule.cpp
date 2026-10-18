#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogSingularityAtmosphere, Log, All);

class FSingularityAtmosphereModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SingularityAtmosphere"));
		if (!Plugin.IsValid())
		{
			UE_LOG(LogSingularityAtmosphere, Error, TEXT("SingularityAtmosphere plugin not found; shader mapping skipped."));
			return;
		}

		const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/SingularityAtmosphere"), PluginShaderDir);
		UE_LOG(LogSingularityAtmosphere, Log,
			TEXT("Mapped /SingularityAtmosphere -> %s (PostConfigInit). Upstream pin: see Plugins/SingularityAtmosphere/THIRD_PARTY.md"),
			*PluginShaderDir);
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FSingularityAtmosphereModule, SingularityAtmosphere);
