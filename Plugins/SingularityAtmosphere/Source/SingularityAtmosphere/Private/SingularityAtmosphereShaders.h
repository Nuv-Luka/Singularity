#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FSingularityAtmosphereIdentityCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSingularityAtmosphereIdentityCS);
	SHADER_USE_PARAMETER_STRUCT(FSingularityAtmosphereIdentityCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER(FUint32Vector2, ViewMin)
		SHADER_PARAMETER(FUint32Vector2, ViewSize)
		SHADER_PARAMETER(FVector2f, InputExtentInverse)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};

class FSingularityAtmosphereTransmittanceCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSingularityAtmosphereTransmittanceCS);
	SHADER_USE_PARAMETER_STRUCT(FSingularityAtmosphereTransmittanceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(FVector4f, RayleighScatterExp)
		SHADER_PARAMETER(FVector4f, MieScatterExp)
		SHADER_PARAMETER(FVector4f, MieExtinctionG)
		SHADER_PARAMETER(FVector3f, Absorption)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 16);
		OutEnvironment.SetDefine(TEXT("T_SAMPLES"), 40);
	}
};

class FSingularityAtmosphereMultiScatteringCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSingularityAtmosphereMultiScatteringCS);
	SHADER_USE_PARAMETER_STRUCT(FSingularityAtmosphereMultiScatteringCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, TransmittanceLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, MultiScatterFactor)
		SHADER_PARAMETER(FVector4f, RayleighScatterExp)
		SHADER_PARAMETER(FVector4f, MieScatterExp)
		SHADER_PARAMETER(FVector4f, MieExtinctionG)
		SHADER_PARAMETER(FVector3f, Absorption)
		SHADER_PARAMETER(FVector3f, GroundAlbedo)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
		OutEnvironment.SetDefine(TEXT("MS_SAMPLES"), 16);
		OutEnvironment.SetDefine(TEXT("DIR_COUNT"), 16);
	}
};

class FSingularityAtmosphereSingleScatterCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSingularityAtmosphereSingleScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FSingularityAtmosphereSingleScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, InputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, TransmittanceLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MultiScatteringLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteringSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SkyViewLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyViewSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D, AerialPerspectiveLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, AerialSampler)
		SHADER_PARAMETER(FUint32Vector2, ViewMin)
		SHADER_PARAMETER(FUint32Vector2, ViewSize)
		SHADER_PARAMETER(FVector2f, InputExtentInverse)
		SHADER_PARAMETER(FMatrix44f, ClipToTranslatedWorld)
		SHADER_PARAMETER(FVector3f, PlanetTranslatedCm)
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, Fade)
		SHADER_PARAMETER(FVector3f, SunDir)
		SHADER_PARAMETER(float, SunIlluminance)
		SHADER_PARAMETER(FVector4f, RayleighScatterExp)
		SHADER_PARAMETER(FVector4f, MieScatterExp)
		SHADER_PARAMETER(FVector4f, MieExtinctionG)
		SHADER_PARAMETER(FVector3f, Absorption)
		SHADER_PARAMETER(FVector4f, GroundAlbBounce)
		SHADER_PARAMETER(uint32, DebugMode)
		SHADER_PARAMETER(uint32, UseLuts)
		SHADER_PARAMETER(uint32, UseSkyView)
		SHADER_PARAMETER(uint32, UseAerial)
		SHADER_PARAMETER(float, ApKmPerSlice)
		SHADER_PARAMETER(float, DiskHaze)
		SHADER_PARAMETER(float, DiskOpacity)
		SHADER_PARAMETER(FVector3f, HazeTint)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
		OutEnvironment.SetDefine(TEXT("MAX_SAMPLES"), 24);
	}
};

class FSingularityAtmosphereSkyViewCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSingularityAtmosphereSkyViewCS);
	SHADER_USE_PARAMETER_STRUCT(FSingularityAtmosphereSkyViewCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, TransmittanceLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MultiScatteringLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteringSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputTexture)
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, ViewHeightKm)
		SHADER_PARAMETER(FVector3f, SunDirLocal)
		SHADER_PARAMETER(float, SunIlluminance)
		SHADER_PARAMETER(FVector4f, RayleighScatterExp)
		SHADER_PARAMETER(FVector4f, MieScatterExp)
		SHADER_PARAMETER(FVector4f, MieExtinctionG)
		SHADER_PARAMETER(FVector3f, Absorption)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 16);
		OutEnvironment.SetDefine(TEXT("SKY_SAMPLES"), 24);
	}
};

class FSingularityAtmosphereAerialPerspectiveCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSingularityAtmosphereAerialPerspectiveCS);
	SHADER_USE_PARAMETER_STRUCT(FSingularityAtmosphereAerialPerspectiveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, TransmittanceLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceSampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MultiScatteringLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteringSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, OutputTexture)
		SHADER_PARAMETER(float, BottomRadiusKm)
		SHADER_PARAMETER(float, TopRadiusKm)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(FVector3f, CamPosKm)
		SHADER_PARAMETER(FVector3f, CamRight)
		SHADER_PARAMETER(FVector3f, CamUp)
		SHADER_PARAMETER(FVector3f, CamForward)
		SHADER_PARAMETER(float, TanHalfFovY)
		SHADER_PARAMETER(float, Aspect)
		SHADER_PARAMETER(FVector3f, SunDir)
		SHADER_PARAMETER(float, SunIlluminance)
		SHADER_PARAMETER(float, ApKmPerSlice)
		SHADER_PARAMETER(FVector4f, RayleighScatterExp)
		SHADER_PARAMETER(FVector4f, MieScatterExp)
		SHADER_PARAMETER(FVector4f, MieExtinctionG)
		SHADER_PARAMETER(FVector3f, Absorption)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
	}
};
