using UnrealBuildTool;
using System.Collections.Generic;

public class SingularityTarget : TargetRules
{
	public SingularityTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("Singularity");
	}
}
