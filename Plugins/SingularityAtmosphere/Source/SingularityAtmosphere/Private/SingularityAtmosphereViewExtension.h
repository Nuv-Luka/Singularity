#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SingularityAtmosphereTypes.h"
#include "SingularityAtmosphereLuts.h"
#include "HAL/CriticalSection.h"
#include "RenderTargetPool.h"

/**
 * Inserts atmosphere RDG compute at pre-tonemap AfterDOF.
 * M2–M5: orbital / sky-view / aerial hybrid.
 * M6: up to MaxPlanets disjoint limbs, farthest-first composite, LRU constant-LUT cache.
 */
class FSingularityAtmosphereViewExtension : public FSceneViewExtensionBase
{
public:
	FSingularityAtmosphereViewExtension(const FAutoRegister& AutoRegister);

	/** Game thread: replace the immutable snapshot consumed next frame on RT. */
	void SetSnapshot_GameThread(FSingularityAtmosphereViewSnapshot InSnapshot);

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

private:
	FScreenPassTexture PostProcessAfterDOF_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	mutable FCriticalSection SnapshotGuard;
	FSingularityAtmosphereViewSnapshot Snapshot;

	TArray<FSingularityAtmosphereLutCacheSlot> LutCache;
	uint32 LutCacheFrameCounter = 0;
};
