#include "SingularityAtmosphereLuts.h"

#include "SingularityAtmosphereShaders.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "Shader.h"

namespace
{
constexpr int32 GTransmittanceSizeX = 256;
constexpr int32 GTransmittanceSizeY = 64;
constexpr int32 GMultiScatterSize = 32;
constexpr int32 GSkyViewSizeX = 192;
constexpr int32 GSkyViewSizeY = 108;
constexpr int32 GAerialSize = 32;

void FillMediumParams(
	const FSingularityAtmospherePlanetProxy& Planet,
	float RayleighExp,
	float MieExp,
	float& OutBottom,
	float& OutTop,
	float& OutDensity,
	FVector4f& OutRay,
	FVector4f& OutMieS,
	FVector4f& OutMieE,
	FVector3f& OutAbs)
{
	OutBottom = static_cast<float>(Planet.VisualBottomRadiusKm);
	OutTop = static_cast<float>(Planet.VisualTopRadiusKm);
	OutDensity = Planet.DensityScale;
	OutRay = FVector4f(Planet.RayleighScatteringPerKm.X, Planet.RayleighScatteringPerKm.Y, Planet.RayleighScatteringPerKm.Z, RayleighExp);
	OutMieS = FVector4f(Planet.MieScatteringPerKm.X, Planet.MieScatteringPerKm.Y, Planet.MieScatteringPerKm.Z, MieExp);
	OutMieE = FVector4f(Planet.MieExtinctionPerKm.X, Planet.MieExtinctionPerKm.Y, Planet.MieExtinctionPerKm.Z, Planet.MieAnisotropyG);
	OutAbs = Planet.AbsorptionPerKm;
}

FSingularityAtmosphereLutCacheSlot* FindOrAllocateSlot(
	TArray<FSingularityAtmosphereLutCacheSlot>& Cache,
	uint32 Hash,
	uint32 FrameCounter)
{
	for (FSingularityAtmosphereLutCacheSlot& Slot : Cache)
	{
		if (Slot.Hash == Hash && Slot.Transmittance.IsValid() && Slot.MultiScattering.IsValid())
		{
			Slot.LastUsedFrame = FrameCounter;
			return &Slot;
		}
	}

	if (Cache.Num() < GSingularityAtmosphereLutCacheCapacity)
	{
		FSingularityAtmosphereLutCacheSlot& Slot = Cache.AddDefaulted_GetRef();
		Slot.Hash = Hash;
		Slot.LastUsedFrame = FrameCounter;
		return &Slot;
	}

	int32 LruIndex = 0;
	for (int32 I = 1; I < Cache.Num(); ++I)
	{
		if (Cache[I].LastUsedFrame < Cache[LruIndex].LastUsedFrame)
		{
			LruIndex = I;
		}
	}
	FSingularityAtmosphereLutCacheSlot& Slot = Cache[LruIndex];
	Slot.Hash = Hash;
	Slot.Transmittance.SafeRelease();
	Slot.MultiScattering.SafeRelease();
	Slot.LastUsedFrame = FrameCounter;
	return &Slot;
}
} // namespace

uint32 ComputeSingularityAtmosphereProfileHash(const FSingularityAtmospherePlanetProxy& Planet)
{
	uint32 Hash = GetTypeHash(static_cast<float>(Planet.VisualBottomRadiusKm));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<float>(Planet.VisualTopRadiusKm)));
	Hash = HashCombine(Hash, GetTypeHash(Planet.DensityScale));
	Hash = HashCombine(Hash, GetTypeHash(Planet.RayleighScatteringPerKm.X));
	Hash = HashCombine(Hash, GetTypeHash(Planet.RayleighScatteringPerKm.Y));
	Hash = HashCombine(Hash, GetTypeHash(Planet.RayleighScatteringPerKm.Z));
	Hash = HashCombine(Hash, GetTypeHash(Planet.RayleighScaleHeightKm));
	Hash = HashCombine(Hash, GetTypeHash(Planet.MieScatteringPerKm.X));
	Hash = HashCombine(Hash, GetTypeHash(Planet.MieExtinctionPerKm.X));
	Hash = HashCombine(Hash, GetTypeHash(Planet.MieScaleHeightKm));
	Hash = HashCombine(Hash, GetTypeHash(Planet.MieAnisotropyG));
	Hash = HashCombine(Hash, GetTypeHash(Planet.AbsorptionPerKm.X));
	Hash = HashCombine(Hash, GetTypeHash(Planet.GroundAlbedo.X));
	return Hash;
}

FSingularityAtmosphereLutTextures AddSingularityAtmosphereLutPasses(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FSingularityAtmospherePlanetProxy& Planet,
	float RayleighExp,
	float MieExp,
	TArray<FSingularityAtmosphereLutCacheSlot>& InOutCache,
	uint32 FrameCounter,
	const FSingularityAtmosphereViewDependentBakeParams* ViewDependent)
{
	FSingularityAtmosphereLutTextures Out;
	if (!ShaderMap)
	{
		return Out;
	}

	const uint32 Hash = Planet.AtmosphereProfileHash != 0
		? Planet.AtmosphereProfileHash
		: ComputeSingularityAtmosphereProfileHash(Planet);

	float Bottom, Top, Density;
	FVector4f Ray, MieS, MieE;
	FVector3f Abs;
	FillMediumParams(Planet, RayleighExp, MieExp, Bottom, Top, Density, Ray, MieS, MieE, Abs);

	FSingularityAtmosphereLutCacheSlot* Slot = FindOrAllocateSlot(InOutCache, Hash, FrameCounter);
	FRDGTextureRef Transmittance = nullptr;
	FRDGTextureRef MultiScattering = nullptr;

	if (Slot && Slot->Transmittance.IsValid() && Slot->MultiScattering.IsValid())
	{
		Transmittance = GraphBuilder.RegisterExternalTexture(Slot->Transmittance, TEXT("SingularityAtm.TransmittanceCached"));
		MultiScattering = GraphBuilder.RegisterExternalTexture(Slot->MultiScattering, TEXT("SingularityAtm.MultiScatterCached"));
	}
	else
	{
		RDG_EVENT_SCOPE(GraphBuilder, "SingularityAtmosphere.BakeLuts");

		const FRDGTextureDesc TransDesc = FRDGTextureDesc::Create2D(
			FIntPoint(GTransmittanceSizeX, GTransmittanceSizeY),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		Transmittance = GraphBuilder.CreateTexture(TransDesc, TEXT("SingularityAtm.Transmittance"));

		{
			auto* Params = GraphBuilder.AllocParameters<FSingularityAtmosphereTransmittanceCS::FParameters>();
			Params->OutputTexture = GraphBuilder.CreateUAV(Transmittance);
			Params->BottomRadiusKm = Bottom;
			Params->TopRadiusKm = Top;
			Params->DensityScale = Density;
			Params->RayleighScatterExp = Ray;
			Params->MieScatterExp = MieS;
			Params->MieExtinctionG = MieE;
			Params->Absorption = Abs;

			const TShaderMapRef<FSingularityAtmosphereTransmittanceCS> CS(ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SingularityAtmosphere_TransmittanceCS"),
				CS,
				Params,
				FComputeShaderUtils::GetGroupCount(FIntPoint(GTransmittanceSizeX, GTransmittanceSizeY), 16));
		}

		const FRDGTextureDesc MsDesc = FRDGTextureDesc::Create2D(
			FIntPoint(GMultiScatterSize, GMultiScatterSize),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		MultiScattering = GraphBuilder.CreateTexture(MsDesc, TEXT("SingularityAtm.MultiScatter"));

		{
			auto* Params = GraphBuilder.AllocParameters<FSingularityAtmosphereMultiScatteringCS::FParameters>();
			Params->TransmittanceLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Transmittance));
			Params->TransmittanceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params->OutputTexture = GraphBuilder.CreateUAV(MultiScattering);
			Params->BottomRadiusKm = Bottom;
			Params->TopRadiusKm = Top;
			Params->DensityScale = Density;
			Params->MultiScatterFactor = 1.0f;
			Params->RayleighScatterExp = Ray;
			Params->MieScatterExp = MieS;
			Params->MieExtinctionG = MieE;
			Params->Absorption = Abs;
			Params->GroundAlbedo = Planet.GroundAlbedo;

			const TShaderMapRef<FSingularityAtmosphereMultiScatteringCS> CS(ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SingularityAtmosphere_MultiScatteringCS"),
				CS,
				Params,
				FComputeShaderUtils::GetGroupCount(FIntPoint(GMultiScatterSize, GMultiScatterSize), 8));
		}

		if (Slot)
		{
			GraphBuilder.QueueTextureExtraction(Transmittance, &Slot->Transmittance);
			GraphBuilder.QueueTextureExtraction(MultiScattering, &Slot->MultiScattering);
			Slot->Hash = Hash;
			Slot->LastUsedFrame = FrameCounter;
		}
	}

	Out.Transmittance = Transmittance;
	Out.MultiScattering = MultiScattering;
	Out.bValid = (Transmittance != nullptr && MultiScattering != nullptr);

	if (ViewDependent && Out.bValid && ViewDependent->bBakeSkyView)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "SingularityAtmosphere.BakeSkyView");
		const FRDGTextureDesc SkyDesc = FRDGTextureDesc::Create2D(
			FIntPoint(GSkyViewSizeX, GSkyViewSizeY),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef SkyView = GraphBuilder.CreateTexture(SkyDesc, TEXT("SingularityAtm.SkyView"));

		auto* Params = GraphBuilder.AllocParameters<FSingularityAtmosphereSkyViewCS::FParameters>();
		Params->TransmittanceLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Transmittance));
		Params->TransmittanceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->MultiScatteringLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MultiScattering));
		Params->MultiScatteringSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->OutputTexture = GraphBuilder.CreateUAV(SkyView);
		Params->BottomRadiusKm = Bottom;
		Params->TopRadiusKm = Top;
		Params->DensityScale = Density;
		Params->ViewHeightKm = FMath::Clamp(ViewDependent->ViewHeightKm, Bottom + 0.001f, Top - 0.001f);
		Params->SunDirLocal = ViewDependent->SunDirLocal.GetSafeNormal();
		Params->SunIlluminance = ViewDependent->SunIlluminance;
		Params->RayleighScatterExp = Ray;
		Params->MieScatterExp = MieS;
		Params->MieExtinctionG = MieE;
		Params->Absorption = Abs;

		const TShaderMapRef<FSingularityAtmosphereSkyViewCS> CS(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SingularityAtmosphere_SkyViewCS"),
			CS,
			Params,
			FComputeShaderUtils::GetGroupCount(FIntPoint(GSkyViewSizeX, GSkyViewSizeY), 16));

		Out.SkyView = SkyView;
		Out.bSkyViewValid = true;
	}

	if (ViewDependent && Out.bValid && ViewDependent->bBakeAerial)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "SingularityAtmosphere.BakeAerial");
		const FRDGTextureDesc ApDesc = FRDGTextureDesc::Create3D(
			FIntVector(GAerialSize, GAerialSize, GAerialSize),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV);
		FRDGTextureRef Aerial = GraphBuilder.CreateTexture(ApDesc, TEXT("SingularityAtm.Aerial"));

		auto* Params = GraphBuilder.AllocParameters<FSingularityAtmosphereAerialPerspectiveCS::FParameters>();
		Params->TransmittanceLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Transmittance));
		Params->TransmittanceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->MultiScatteringLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MultiScattering));
		Params->MultiScatteringSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->OutputTexture = GraphBuilder.CreateUAV(Aerial);
		Params->BottomRadiusKm = Bottom;
		Params->TopRadiusKm = Top;
		Params->DensityScale = Density;
		Params->CamPosKm = ViewDependent->CamPosKm;
		Params->CamRight = ViewDependent->CamRight;
		Params->CamUp = ViewDependent->CamUp;
		Params->CamForward = ViewDependent->CamForward;
		Params->TanHalfFovY = ViewDependent->TanHalfFovY;
		Params->Aspect = ViewDependent->Aspect;
		Params->SunDir = ViewDependent->SunDir.GetSafeNormal();
		Params->SunIlluminance = ViewDependent->SunIlluminance;
		Params->ApKmPerSlice = ViewDependent->ApKmPerSlice;
		Params->RayleighScatterExp = Ray;
		Params->MieScatterExp = MieS;
		Params->MieExtinctionG = MieE;
		Params->Absorption = Abs;

		const TShaderMapRef<FSingularityAtmosphereAerialPerspectiveCS> CS(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SingularityAtmosphere_AerialPerspectiveCS"),
			CS,
			Params,
			FComputeShaderUtils::GetGroupCount(FIntVector(GAerialSize, GAerialSize, GAerialSize), FIntVector(8, 8, 4)));

		Out.AerialPerspective = Aerial;
		Out.bAerialValid = true;
	}

	return Out;
}
