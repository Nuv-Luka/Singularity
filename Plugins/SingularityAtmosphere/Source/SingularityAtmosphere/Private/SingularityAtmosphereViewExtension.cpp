#include "SingularityAtmosphereViewExtension.h"

#include "SingularityAtmosphereSettings.h"
#include "SingularityAtmosphereShaders.h"
#include "SingularityAtmosphereLuts.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "SceneView.h"
#include "RHIStaticStates.h"

DEFINE_LOG_CATEGORY_STATIC(LogSingularityAtmosphereVE, Log, All);

FSingularityAtmosphereViewExtension::FSingularityAtmosphereViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FSingularityAtmosphereViewExtension::SetSnapshot_GameThread(FSingularityAtmosphereViewSnapshot InSnapshot)
{
	FScopeLock Lock(&SnapshotGuard);
	Snapshot = MoveTemp(InSnapshot);
}

void FSingularityAtmosphereViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (Pass != EPostProcessingPass::AfterDOF)
	{
		return;
	}

	if (!SingularityAtmosphere::IsEnabled())
	{
		return;
	}

	FSingularityAtmosphereViewSnapshot Local;
	{
		FScopeLock Lock(&SnapshotGuard);
		Local = Snapshot;
	}

	const int32 DebugMode = SingularityAtmosphere::CVarDebug.GetValueOnAnyThread();
	const bool bWantIdentity = (DebugMode == 3);
	const bool bWantScatter = Local.bActive && Local.Planets.Num() > 0 && Local.Fade > 0.01f;

	if (!bWantIdentity && !bWantScatter)
	{
		return;
	}

	InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(
		this,
		&FSingularityAtmosphereViewExtension::PostProcessAfterDOF_RenderThread));
}

FScreenPassTexture FSingularityAtmosphereViewExtension::PostProcessAfterDOF_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(
		GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid() || !SceneColor.Texture)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const FIntRect ViewRect = SceneColor.ViewRect;
	const FIntPoint ViewSize = ViewRect.Size();
	if (ViewSize.X <= 0 || ViewSize.Y <= 0)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	FSingularityAtmosphereViewSnapshot Local;
	{
		FScopeLock Lock(&SnapshotGuard);
		Local = Snapshot;
	}

	const int32 DebugMode = SingularityAtmosphere::CVarDebug.GetValueOnRenderThread();
	const FIntPoint Extent = SceneColor.Texture->Desc.Extent;
	const FVector2f ExtentInv(
		Extent.X > 0 ? 1.0f / static_cast<float>(Extent.X) : 0.0f,
		Extent.Y > 0 ? 1.0f / static_cast<float>(Extent.Y) : 0.0f);
	const FUint32Vector2 ViewMinU(static_cast<uint32>(ViewRect.Min.X), static_cast<uint32>(ViewRect.Min.Y));
	const FUint32Vector2 ViewSizeU(static_cast<uint32>(ViewSize.X), static_cast<uint32>(ViewSize.Y));
	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ViewSize, FComputeShaderUtils::kGolden2DGroupSize);

	FScreenPassRenderTarget PipelineOutput = Inputs.OverrideOutput;
	if (!PipelineOutput.IsValid())
	{
		PipelineOutput = FScreenPassRenderTarget::CreateFromInput(
			GraphBuilder,
			SceneColor,
			View.GetOverwriteLoadAction(),
			TEXT("SingularityAtmosphere.Out"));
	}

	auto DispatchToUav = [&](FRDGTextureRef InputTex, FRDGTextureRef Uav, TFunctionRef<void(FSingularityAtmosphereSingleScatterCS::FParameters&)> Fill)
	{
		auto* Params = GraphBuilder.AllocParameters<FSingularityAtmosphereSingleScatterCS::FParameters>();
		Params->InputTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(InputTex));
		Params->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->OutputTexture = GraphBuilder.CreateUAV(Uav);
		Params->ViewMin = ViewMinU;
		Params->ViewSize = ViewSizeU;
		Params->InputExtentInverse = ExtentInv;
		Fill(*Params);

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
		const TShaderMapRef<FSingularityAtmosphereSingleScatterCS> CS(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SingularityAtmosphere_SingleScatterCS"),
			CS,
			Params,
			GroupCount);
	};

	// Debug 3 = forced identity gate.
	if (DebugMode == 3)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "SingularityAtmosphere.Identity");
		const FRDGTextureDesc UavDesc = FRDGTextureDesc::Create2D(
			Extent, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
		FRDGTextureRef Uav = GraphBuilder.CreateTexture(UavDesc, TEXT("SingularityAtmosphere.UAV"));

		auto* Params = GraphBuilder.AllocParameters<FSingularityAtmosphereIdentityCS::FParameters>();
		Params->InputTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SceneColor.Texture));
		Params->InputSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->OutputTexture = GraphBuilder.CreateUAV(Uav);
		Params->ViewMin = ViewMinU;
		Params->ViewSize = ViewSizeU;
		Params->InputExtentInverse = ExtentInv;
		const TShaderMapRef<FSingularityAtmosphereIdentityCS> CS(GetGlobalShaderMap(View.GetFeatureLevel()));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SingularityAtmosphere_IdentityCS"),
			CS,
			Params,
			GroupCount);

		AddDrawTexturePass(
			GraphBuilder,
			FScreenPassViewInfo(View),
			FScreenPassTexture(Uav, ViewRect),
			PipelineOutput);
		return FScreenPassTexture(PipelineOutput);
	}

	if (!Local.bActive || Local.Planets.Num() == 0 || Local.Fade <= 0.01f)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const int32 MaxPlanets = FMath::Clamp(SingularityAtmosphere::CVarMaxPlanets.GetValueOnRenderThread(), 1, 4);
	const int32 Quality = SingularityAtmosphere::CVarQuality.GetValueOnRenderThread();
	const FMatrix44f ClipToTranslatedWorld(View.ViewMatrices.GetClipToTranslatedWorld());
	const FVector PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
	const FVector3d CamWorld(
		View.ViewMatrices.GetViewOrigin().X,
		View.ViewMatrices.GetViewOrigin().Y,
		View.ViewMatrices.GetViewOrigin().Z);

	FVector3f CamForward = Local.Camera.Forward;
	FVector3f CamRight = Local.Camera.Right;
	FVector3f CamUp = Local.Camera.Up;
	float TanHalfFovY = Local.Camera.TanHalfFovY;
	float Aspect = Local.Camera.Aspect;
	{
		const FVector UseFwd = View.GetViewDirection();
		if (!UseFwd.IsNearlyZero())
		{
			const FVector FwdN = UseFwd.GetSafeNormal();
			CamForward = FVector3f(FwdN);
			const FVector WorldUp(0.0, 0.0, 1.0);
			FVector Right = FVector::CrossProduct(FwdN, WorldUp);
			if (Right.IsNearlyZero())
			{
				Right = FVector::CrossProduct(FwdN, FVector(0.0, 1.0, 0.0));
			}
			const FVector RightN = Right.GetSafeNormal();
			CamRight = FVector3f(RightN);
			CamUp = FVector3f(FVector::CrossProduct(RightN, FwdN).GetSafeNormal());
		}
		const FVector2D HalfFov = View.ViewMatrices.ComputeHalfFieldOfViewPerAxis();
		if (HalfFov.Y > 1e-4)
		{
			TanHalfFovY = FMath::Tan(static_cast<float>(HalfFov.Y));
		}
		if (ViewSize.Y > 0)
		{
			Aspect = static_cast<float>(ViewSize.X) / static_cast<float>(ViewSize.Y);
		}
	}

	// Sort farthest → nearest so nearer limbs composite last.
	struct FPlanetOrder
	{
		int32 Index = 0;
		double DistCm = 0.0;
	};
	TArray<FPlanetOrder, TInlineAllocator<4>> Order;
	const int32 PlanetCount = FMath::Min(Local.Planets.Num(), MaxPlanets);
	Order.Reserve(PlanetCount);
	for (int32 I = 0; I < PlanetCount; ++I)
	{
		FPlanetOrder Entry;
		Entry.Index = I;
		Entry.DistCm = (CamWorld - Local.Planets[I].PlanetCenterWorldCm).Size();
		Order.Add(Entry);
	}
	Order.Sort([](const FPlanetOrder& A, const FPlanetOrder& B) { return A.DistCm > B.DistCm; });

	// Nearest planet index in snapshot (for sky/aerial hybrid only).
	int32 NearestSnapIndex = 0;
	double NearestDist = TNumericLimits<double>::Max();
	for (const FPlanetOrder& Entry : Order)
	{
		if (Entry.DistCm < NearestDist)
		{
			NearestDist = Entry.DistCm;
			NearestSnapIndex = Entry.Index;
		}
	}

	++LutCacheFrameCounter;
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	FRDGTextureRef Current = SceneColor.Texture;
	constexpr float ApKmPerSlice = 4.0f;

	RDG_EVENT_SCOPE(GraphBuilder, "SingularityAtmosphere.MultiPlanet");

	const float LiveFadeScale = SingularityAtmosphere::GetLiveFadeScale();

	for (const FPlanetOrder& Entry : Order)
	{
		FSingularityAtmospherePlanetProxy Planet = Local.Planets[Entry.Index];
		SingularityAtmosphere::ApplyLiveTweaksToPlanet(Planet);
		const bool bIsNearest = (Entry.Index == NearestSnapIndex);

		const float Illum = FMath::Max3(
			Planet.StarIlluminanceLinear.X,
			Planet.StarIlluminanceLinear.Y,
			Planet.StarIlluminanceLinear.Z);
		const float RayExp = -1.0f / FMath::Max(Planet.RayleighScaleHeightKm, 0.5f);
		const float MieExp = -1.0f / FMath::Max(Planet.MieScaleHeightKm, 0.2f);
		const float BottomKm = static_cast<float>(Planet.VisualBottomRadiusKm);
		const float TopKm = static_cast<float>(Planet.VisualTopRadiusKm);

		const FVector3d PlanetTranslated =
			Planet.PlanetCenterWorldCm
			+ FVector3d(PreViewTranslation.X, PreViewTranslation.Y, PreViewTranslation.Z);
		const FVector3f PlanetTranslatedCm(
			static_cast<float>(PlanetTranslated.X),
			static_cast<float>(PlanetTranslated.Y),
			static_cast<float>(PlanetTranslated.Z));

		const double CamHeightKm = Entry.DistCm / FMath::Max(Planet.VisualKmToWorldCm, 1.0);
		const bool bNearOrInside = CamHeightKm < static_cast<double>(TopKm) * 1.35;
		// Sky/aerial only for the nearest body when quality allows (cost + uniqueness).
		const bool bWantViewLuts = bIsNearest && ((Quality >= 2) || (Quality == 1 && bNearOrInside));

		const FVector3d CamRelCm = CamWorld - Planet.PlanetCenterWorldCm;
		const FVector3f CamPosKm(
			static_cast<float>(CamRelCm.X / Planet.VisualKmToWorldCm),
			static_cast<float>(CamRelCm.Y / Planet.VisualKmToWorldCm),
			static_cast<float>(CamRelCm.Z / Planet.VisualKmToWorldCm));

		FVector3f Zenith = CamPosKm.GetSafeNormal();
		if (Zenith.IsNearlyZero())
		{
			Zenith = FVector3f(0.0f, 0.0f, 1.0f);
		}
		const float CosSunZenith = FVector3f::DotProduct(Planet.DirectionToStar, Zenith);
		const float SinSunZenith = FMath::Sqrt(FMath::Max(1.0f - CosSunZenith * CosSunZenith, 0.0f));
		const FVector3f SunDirLocal(SinSunZenith, 0.0f, CosSunZenith);

		FSingularityAtmosphereViewDependentBakeParams ViewBake;
		ViewBake.bBakeSkyView = bWantViewLuts;
		ViewBake.bBakeAerial = bWantViewLuts;
		ViewBake.ViewHeightKm = static_cast<float>(FMath::Clamp(CamHeightKm, static_cast<double>(BottomKm) + 0.001, static_cast<double>(TopKm) - 0.001));
		ViewBake.SunDirLocal = SunDirLocal;
		ViewBake.CamPosKm = CamPosKm;
		ViewBake.CamRight = CamRight;
		ViewBake.CamUp = CamUp;
		ViewBake.CamForward = CamForward;
		ViewBake.TanHalfFovY = TanHalfFovY;
		ViewBake.Aspect = Aspect;
		ViewBake.SunDir = Planet.DirectionToStar;
		ViewBake.SunIlluminance = Illum;
		ViewBake.ApKmPerSlice = ApKmPerSlice;

		const FSingularityAtmosphereLutTextures Luts = AddSingularityAtmosphereLutPasses(
			GraphBuilder,
			ShaderMap,
			Planet,
			RayExp,
			MieExp,
			LutCache,
			LutCacheFrameCounter,
			bWantViewLuts ? &ViewBake : nullptr);

		const FRDGTextureDesc UavDesc = FRDGTextureDesc::Create2D(
			Extent, PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable);
		FRDGTextureRef Uav = GraphBuilder.CreateTexture(UavDesc, TEXT("SingularityAtmosphere.PlanetUAV"));

		DispatchToUav(Current, Uav, [&](FSingularityAtmosphereSingleScatterCS::FParameters& Params)
		{
			FRDGTextureRef Transmittance = Luts.Transmittance;
			FRDGTextureRef MultiScatter = Luts.MultiScattering;
			if (!Transmittance || !MultiScatter)
			{
				const FRDGTextureDesc DummyDesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				Transmittance = GraphBuilder.CreateTexture(DummyDesc, TEXT("SingularityAtm.DummyT"));
				MultiScatter = GraphBuilder.CreateTexture(DummyDesc, TEXT("SingularityAtm.DummyMS"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Transmittance), FVector4f(1, 1, 1, 1));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MultiScatter), FVector4f(0, 0, 0, 0));
			}

			FRDGTextureRef SkyView = Luts.SkyView;
			if (!SkyView)
			{
				const FRDGTextureDesc DummyDesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				SkyView = GraphBuilder.CreateTexture(DummyDesc, TEXT("SingularityAtm.DummySky"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SkyView), FVector4f(0, 0, 0, 0));
			}

			FRDGTextureRef Aerial = Luts.AerialPerspective;
			if (!Aerial)
			{
				const FRDGTextureDesc DummyDesc = FRDGTextureDesc::Create3D(
					FIntVector(1, 1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				Aerial = GraphBuilder.CreateTexture(DummyDesc, TEXT("SingularityAtm.DummyAp"));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Aerial), FVector4f(0, 0, 0, 0));
			}

			Params.TransmittanceLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Transmittance));
			Params.TransmittanceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params.MultiScatteringLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(MultiScatter));
			Params.MultiScatteringSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params.SkyViewLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SkyView));
			Params.SkyViewSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params.AerialPerspectiveLut = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Aerial));
			Params.AerialSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			Params.ClipToTranslatedWorld = ClipToTranslatedWorld;
			Params.PlanetTranslatedCm = PlanetTranslatedCm;
			Params.BottomRadiusKm = BottomKm;
			Params.TopRadiusKm = TopKm;
			Params.DensityScale = Planet.DensityScale;
			Params.Fade = FMath::Clamp(Local.Fade * LiveFadeScale, 0.0f, 2.0f);
			Params.SunDir = Planet.DirectionToStar;
			Params.SunIlluminance = Illum;
			Params.RayleighScatterExp = FVector4f(
				Planet.RayleighScatteringPerKm.X,
				Planet.RayleighScatteringPerKm.Y,
				Planet.RayleighScatteringPerKm.Z,
				RayExp);
			Params.MieScatterExp = FVector4f(
				Planet.MieScatteringPerKm.X,
				Planet.MieScatteringPerKm.Y,
				Planet.MieScatteringPerKm.Z,
				MieExp);
			Params.MieExtinctionG = FVector4f(
				Planet.MieExtinctionPerKm.X,
				Planet.MieExtinctionPerKm.Y,
				Planet.MieExtinctionPerKm.Z,
				Planet.MieAnisotropyG);
			Params.Absorption = Planet.AbsorptionPerKm;
			Params.GroundAlbBounce = FVector4f(
				Planet.GroundAlbedo.X,
				Planet.GroundAlbedo.Y,
				Planet.GroundAlbedo.Z,
				Planet.GroundBounce);
			Params.DebugMode = static_cast<uint32>(FMath::Clamp(DebugMode, 0, 2));
			Params.UseLuts = Luts.bValid ? 1u : 0u;
			Params.UseSkyView = Luts.bSkyViewValid ? 1u : 0u;
			Params.UseAerial = Luts.bAerialValid ? 1u : 0u;
			Params.ApKmPerSlice = ApKmPerSlice;
			Params.DiskHaze = SingularityAtmosphere::CVarDiskHaze.GetValueOnRenderThread();
			Params.DiskOpacity = SingularityAtmosphere::CVarDiskOpacity.GetValueOnRenderThread();
			Params.HazeTint = SingularityAtmosphere::GetLiveHazeTint();
		});

		Current = Uav;
	}

	AddDrawTexturePass(
		GraphBuilder,
		FScreenPassViewInfo(View),
		FScreenPassTexture(Current, ViewRect),
		PipelineOutput);
	return FScreenPassTexture(PipelineOutput);
}
