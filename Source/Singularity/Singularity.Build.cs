using UnrealBuildTool;

public class Singularity : ModuleRules
{
	public Singularity(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"ProceduralMeshComponent",
			"SingularityAtmosphere",
			"SingularityPlanetTerrain",
			"SingularityPlanetTerrainNext",
			"Slate",
			"SlateCore",
			"ApplicationCore"
		});
	}
}
