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

bool AdvanceSpatialFade(
	FPlanetTerrainNextLodFade& Fade,
	double DistanceToPatchM,
	const FVector3d& CameraM,
	bool bTeleport)
{
	if (Fade.State != EPlanetTerrainNextLodFadeState::FadingToChildren
		&& Fade.State != EPlanetTerrainNextLodFadeState::FadingToParent)
	{
		return false;
	}
	Fade.bSpatial = true;
	Fade.DistanceSpanM = FMath::Max(Fade.DistanceSpanM, 1.0);
	if (!Fade.bHasCameraSample)
	{
		Fade.LastCameraM = CameraM;
		Fade.bHasCameraSample = true;
	}
	const double CameraTravelM = (CameraM - Fade.LastCameraM).Length();
	const bool bCut = bTeleport || CameraTravelM > FMath::Max(Fade.DistanceSpanM * 8.0, 10000.0);
	Fade.LastCameraM = CameraM;

	if (bCut)
	{
		const bool bReachedDestination = Fade.Direction > 0
			? DistanceToPatchM <= Fade.AnchorDistanceM - Fade.DistanceSpanM
			: DistanceToPatchM >= Fade.AnchorDistanceM + Fade.DistanceSpanM;
		Fade.Progress = bReachedDestination ? (Fade.Direction > 0 ? 1.0f : 0.0f) : (Fade.Direction > 0 ? 0.0f : 1.0f);
		Fade.State = EPlanetTerrainNextLodFadeState::Stable;
		return true;
	}

	const double Alpha = Fade.Direction > 0
		? (Fade.AnchorDistanceM - DistanceToPatchM) / Fade.DistanceSpanM
		: 1.0 - (DistanceToPatchM - Fade.AnchorDistanceM) / Fade.DistanceSpanM;
	Fade.Progress = static_cast<float>(FMath::Clamp(Alpha, 0.0, 1.0));
	if ((Fade.Direction > 0 && Fade.Progress >= 1.0f - KINDA_SMALL_NUMBER)
		|| (Fade.Direction < 0 && Fade.Progress <= KINDA_SMALL_NUMBER))
	{
		Fade.Progress = Fade.Direction > 0 ? 1.0f : 0.0f;
		Fade.State = EPlanetTerrainNextLodFadeState::Stable;
		return true;
	}
	return false;
}

void ReverseSpatialFade(FPlanetTerrainNextLodFade& Fade, double DistanceToPatchM, int32 NewDirection)
{
	if (!Fade.bSpatial || NewDirection == 0 || NewDirection == Fade.Direction)
	{
		Fade.Direction = NewDirection == 0 ? Fade.Direction : NewDirection;
		return;
	}
	const double Progress = FMath::Clamp(static_cast<double>(Fade.Progress), 0.0, 1.0);
	Fade.Direction = NewDirection;
	if (NewDirection > 0)
	{
		Fade.AnchorDistanceM = DistanceToPatchM + Progress * Fade.DistanceSpanM;
	}
	else
	{
		Fade.AnchorDistanceM = DistanceToPatchM - (1.0 - Progress) * Fade.DistanceSpanM;
	}
}
}
