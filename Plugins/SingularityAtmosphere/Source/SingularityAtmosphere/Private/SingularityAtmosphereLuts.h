#pragma once

#include "CoreMinimal.h"
#include "RenderGraphDefinitions.h"
#include "SingularityAtmosphereTypes.h"

class FRDGBuilder;
class FGlobalShaderMap;

struct FSingularityAtmosphereLutCacheSlot
{
	uint32 Hash = 0;
	TRefCountPtr<IPooledRenderTarget> Transmittance;
	TRefCountPtr<IPooledRenderTarget> MultiScattering;
	uint32 LastUsedFrame = 0;
};

struct FSingularityAtmosphereLutTextures
{
	FRDGTextureRef Transmittance = nullptr;
	FRDGTextureRef MultiScattering = nullptr;
	FRDGTextureRef SkyView = nullptr;
	FRDGTextureRef AerialPerspective = nullptr;
	bool bValid = false;
	bool bSkyViewValid = false;
	bool bAerialValid = false;
};

struct FSingularityAtmosphereViewDependentBakeParams
{
	float ViewHeightKm = 0.0f;
	FVector3f SunDirLocal = FVector3f(0.0f, 0.0f, 1.0f);
	FVector3f CamPosKm = FVector3f::ZeroVector;
	FVector3f CamRight = FVector3f(0.0f, 1.0f, 0.0f);
	FVector3f CamUp = FVector3f(0.0f, 0.0f, 1.0f);
	FVector3f CamForward = FVector3f(1.0f, 0.0f, 0.0f);
	float TanHalfFovY = 0.5f;
	float Aspect = 1.777f;
	FVector3f SunDir = FVector3f(0.0f, 0.0f, 1.0f);
	float SunIlluminance = 1.0f;
	float ApKmPerSlice = 4.0f;
	bool bBakeSkyView = false;
	bool bBakeAerial = false;
};

/** Max pooled constant-LUT profiles kept for multi-planet (VRAM budget). */
constexpr int32 GSingularityAtmosphereLutCacheCapacity = 4;

/** Bake or reuse profile-keyed transmittance (256×64) + multi-scattering (32×32) LUTs.
 *  Optionally bake view-dependent sky-view (192×108) and aerial (32³) when near/inside. */
FSingularityAtmosphereLutTextures AddSingularityAtmosphereLutPasses(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FSingularityAtmospherePlanetProxy& Planet,
	float RayleighExp,
	float MieExp,
	TArray<FSingularityAtmosphereLutCacheSlot>& InOutCache,
	uint32 FrameCounter,
	const FSingularityAtmosphereViewDependentBakeParams* ViewDependent = nullptr);
