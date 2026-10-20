#include "PlanetTerrainNextLodFade.h"

namespace PlanetTerrainNextLodFade
{
bool DesiredHasAllChildren(
	const TSet<FPlanetTerrainNextNodeId>& Desired,
	const FPlanetTerrainNextNodeId& Parent)
{
	for (int32 C = 0; C < 4; ++C)
	{
		if (!Desired.Contains(Parent.Child(C)))
		{
			return false;
		}
	}
	return true;
}

bool ChildrenResident(
	const FPlanetTerrainNextNodeId& Parent,
	TFunctionRef<bool(const FPlanetTerrainNextNodeId&)> IsResident)
{
	for (int32 C = 0; C < 4; ++C)
	{
		if (!IsResident(Parent.Child(C)))
		{
			return false;
		}
	}
	return true;
}

void BuildRenderSet(
	const TSet<FPlanetTerrainNextNodeId>& Desired,
	const TArray<FPlanetTerrainNextLodFade>& Fades,
	TSet<FPlanetTerrainNextNodeId>& OutRender)
{
	OutRender = Desired;
	for (const FPlanetTerrainNextLodFade& Fade : Fades)
	{
		if (Fade.State == EPlanetTerrainNextLodFadeState::WaitingForChildren
			|| Fade.State == EPlanetTerrainNextLodFadeState::Stable)
		{
			continue;
		}
		OutRender.Add(Fade.Parent);
		for (int32 C = 0; C < 4; ++C)
		{
			OutRender.Add(Fade.Parent.Child(C));
		}
	}
}

bool TickFade(FPlanetTerrainNextLodFade& Fade, float DeltaSeconds)
{
	if (Fade.State != EPlanetTerrainNextLodFadeState::FadingToChildren
		&& Fade.State != EPlanetTerrainNextLodFadeState::FadingToParent)
	{
		return false;
	}
	const float Dur = FMath::Max(Fade.DurationSeconds, 0.01f);
	Fade.Progress = FMath::Clamp(Fade.Progress + Fade.Direction * (DeltaSeconds / Dur), 0.0f, 1.0f);
	if (Fade.Direction > 0 && Fade.Progress >= 1.0f - KINDA_SMALL_NUMBER)
	{
		Fade.Progress = 1.0f;
		Fade.State = EPlanetTerrainNextLodFadeState::Stable;
		return true;
	}
	if (Fade.Direction < 0 && Fade.Progress <= KINDA_SMALL_NUMBER)
	{
		Fade.Progress = 0.0f;
		Fade.State = EPlanetTerrainNextLodFadeState::Stable;
		return true;
	}
	return false;
}
}
