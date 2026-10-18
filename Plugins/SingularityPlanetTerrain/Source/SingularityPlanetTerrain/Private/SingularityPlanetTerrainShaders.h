#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FSingularityPlanetTerrainHeightBakeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSingularityPlanetTerrainHeightBakeCS);
	SHADER_USE_PARAMETER_STRUCT(FSingularityPlanetTerrainHeightBakeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutHeights)
		SHADER_PARAMETER(uint32, FaceIndex)
		SHADER_PARAMETER(FVector4f, UvRect)
		SHADER_PARAMETER(uint32, Resolution)
		SHADER_PARAMETER(float, FineSpacingM)
		SHADER_PARAMETER(float, CoarseSpacingM)
		SHADER_PARAMETER(float, AmplitudeM)
		SHADER_PARAMETER(float, RadiusM)
		SHADER_PARAMETER(float, ContinentFreq)
		SHADER_PARAMETER(float, MountainFreq)
		SHADER_PARAMETER(float, DetailFreq)
		SHADER_PARAMETER(float, CraterFreq0)
		SHADER_PARAMETER(float, CraterDensity)
		SHADER_PARAMETER(float, CraterWeight)
		SHADER_PARAMETER(float, MountainWeight)
		SHADER_PARAMETER(uint32, NoiseSeed)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};
