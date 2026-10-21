#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextQuadtree.h"

enum class EPlanetTerrainNextLodFadeState : uint8
{
	Stable = 0,
	WaitingForChildren,
	FadingToChildren,
	FadingToParent
};

/** One sibling-group parent↔children crossfade (shared Progress). */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextLodFade
{
	FPlanetTerrainNextNodeId Parent;
	EPlanetTerrainNextLodFadeState State = EPlanetTerrainNextLodFadeState::WaitingForChildren;
	/** 0 = parent coverage, 1 = children coverage. */
	float Progress = 0.0f;
	float DurationSeconds = 0.45f;
	/** +1 split (parent→children), -1 merge (children→parent). */
	int32 Direction = 1;
	uint64 GenerationEpoch = 0;
};

namespace PlanetTerrainNextLodFade
{
/** Deterministic reference for the material's shared-noise complementary mask. */
inline bool EvaluateComplementaryDither(float Progress, float Noise01, bool bChild)
{
	const float ParentThreshold = 1.0f - FMath::Clamp(Progress, 0.0f, 1.0f);
	const bool bParentAccepted = Noise01 < ParentThreshold;
	return bChild ? !bParentAccepted : bParentAccepted;
}

/** True if Desired contains all four children of Parent. */
SINGULARITYPLANETTERRAINNEXT_API bool DesiredHasAllChildren(
	const TSet<FPlanetTerrainNextNodeId>& Desired,
	const FPlanetTerrainNextNodeId& Parent);

/** True if all four children are resident (mesh ready). */
SINGULARITYPLANETTERRAINNEXT_API bool ChildrenResident(
	const FPlanetTerrainNextNodeId& Parent,
	TFunctionRef<bool(const FPlanetTerrainNextNodeId&)> IsResident);

/**
 * Build RenderSet from exclusive Desired plus active fade overlaps.
 * Outgoing geometry stays in Render until Progress completes.
 */
SINGULARITYPLANETTERRAINNEXT_API void BuildRenderSet(
	const TSet<FPlanetTerrainNextNodeId>& Desired,
	const TArray<FPlanetTerrainNextLodFade>& Fades,
	TSet<FPlanetTerrainNextNodeId>& OutRender);

/** Advance Progress; completes when Progress hits 0 or 1. Returns true if group finished. */
SINGULARITYPLANETTERRAINNEXT_API bool TickFade(FPlanetTerrainNextLodFade& Fade, float DeltaSeconds);

/** Parent opacity role: 0. Child role: 1. */
inline float RoleParent() { return 0.0f; }
inline float RoleChild() { return 1.0f; }
}
