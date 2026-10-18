#include "SingularityPlanetTerrainBake.h"
#include "SingularityPlanetTerrainShaders.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "RHICommandList.h"
#include "Async/Async.h"
#include "Shader.h"

bool SingularityPlanetTerrainIsGpuBakeAvailable()
{
	return GIsRHIInitialized && !GUsingNullRHI;
}

bool SingularityPlanetTerrainBakeHeights(
	const FSingularityPlanetTerrainBakeUniforms& Uniforms,
	uint8 Face,
	float U0, float V0, float U1, float V1,
	int32 Resolution,
	float FineSpacingM,
	float CoarseSpacingM,
	TArray<float>& OutFine,
	TArray<float>& OutCoarse)
{
	if (!SingularityPlanetTerrainIsGpuBakeAvailable() || !IsInGameThread())
	{
		return false;
	}

	const int32 Res = FMath::Max(2, Resolution);
	const int32 N = Res + 1;
	OutFine.SetNumUninitialized(N * N);
	OutCoarse.SetNumUninitialized(N * N);

	struct FBakeResult
	{
		TArray<FVector2f> Pixels;
		bool bOk = false;
	};
	TSharedRef<FBakeResult, ESPMode::ThreadSafe> Result = MakeShared<FBakeResult, ESPMode::ThreadSafe>();
	Result->Pixels.SetNumZeroed(N * N);

	const FSingularityPlanetTerrainBakeUniforms U = Uniforms;
	const uint8 FaceCopy = Face;
	const FVector4f UvRect(U0, V0, U1, V1);

	ENQUEUE_RENDER_COMMAND(SingularityPlanetTerrainHeightBake)(
		[Result, N, Res, FaceCopy, UvRect, FineSpacingM, CoarseSpacingM, U](FRHICommandListImmediate& RHICmdList)
		{
			FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			if (!ShaderMap)
			{
				return;
			}
			TShaderMapRef<FSingularityPlanetTerrainHeightBakeCS> ComputeShader(ShaderMap);
			if (!ComputeShader.IsValid())
			{
				return;
			}

			FRDGBuilder GraphBuilder(RHICmdList);
			const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
				FIntPoint(N, N),
				PF_G32R32F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef HeightTex = GraphBuilder.CreateTexture(Desc, TEXT("SingularityPlanetTerrain.HeightBake"));
			FRDGTextureUAVRef HeightUAV = GraphBuilder.CreateUAV(HeightTex);

			FSingularityPlanetTerrainHeightBakeCS::FParameters* Pass =
				GraphBuilder.AllocParameters<FSingularityPlanetTerrainHeightBakeCS::FParameters>();
			Pass->OutHeights = HeightUAV;
			Pass->FaceIndex = FaceCopy;
			Pass->UvRect = UvRect;
			Pass->Resolution = static_cast<uint32>(Res);
			Pass->FineSpacingM = FineSpacingM;
			Pass->CoarseSpacingM = CoarseSpacingM;
			Pass->AmplitudeM = U.AmplitudeM;
			Pass->RadiusM = U.RadiusM;
			Pass->ContinentFreq = U.ContinentFreq;
			Pass->MountainFreq = U.MountainFreq;
			Pass->DetailFreq = U.DetailFreq;
			Pass->CraterFreq0 = U.CraterFreq0;
			Pass->CraterDensity = U.CraterDensity;
			Pass->CraterWeight = U.CraterWeight;
			Pass->MountainWeight = U.MountainWeight;
			Pass->NoiseSeed = U.NoiseSeed;

			const FIntVector GroupCount(
				FMath::DivideAndRoundUp(N, 8),
				FMath::DivideAndRoundUp(N, 8),
				1);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SingularityPlanetTerrain_HeightBakeCS"),
				ComputeShader,
				Pass,
				GroupCount);

			FRHIGPUTextureReadback Readback(TEXT("SingularityPlanetTerrain.HeightReadback"));
			AddEnqueueCopyPass(GraphBuilder, &Readback, HeightTex);
			GraphBuilder.Execute();
			RHICmdList.SubmitAndBlockUntilGPUIdle();

			int32 RowPitchInPixels = 0;
			if (void* Data = Readback.Lock(RowPitchInPixels))
			{
				const FVector2f* Base = static_cast<const FVector2f*>(Data);
				for (int32 Y = 0; Y < N; ++Y)
				{
					const FVector2f* Src = Base + Y * RowPitchInPixels;
					for (int32 X = 0; X < N; ++X)
					{
						Result->Pixels[Y * N + X] = Src[X];
					}
				}
				Readback.Unlock();
				Result->bOk = true;
			}
		});

	FlushRenderingCommands();

	if (!Result->bOk)
	{
		return false;
	}

	for (int32 I = 0; I < N * N; ++I)
	{
		OutFine[I] = Result->Pixels[I].X;
		OutCoarse[I] = Result->Pixels[I].Y;
	}
	return true;
}
