#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PlanetTerrainNextTypes.h"
#include "PlanetTerrainNextSpawn.h"

DECLARE_DELEGATE(FOnPlanetTerrainNextSpawnPanelClosed);

class SPlanetTerrainNextSpawnPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanetTerrainNextSpawnPanel) {}
		SLATE_EVENT(FOnPlanetTerrainNextSpawnPanelClosed, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> SectionHeader(const FString& Title) const;
	TSharedRef<SWidget> MakeButton(const FString& Label, FOnClicked OnClicked) const;

	FReply OnSpawnInFront();
	FReply OnSpawnFar();
	FReply OnSpawnAtOrigin();
	FReply OnDestroyAll();
	FReply OnClose();
	FReply OnSetModeLegacy();
	FReply OnSetModeNext();

	TSharedRef<SWidget> MakeCVarFloatRow(const FString& Label, const TCHAR* CVarName, float MinValue, float MaxValue) const;
	TSharedRef<SWidget> MakeCVarIntRow(const FString& Label, const TCHAR* CVarName, int32 MinValue, int32 MaxValue) const;
	TSharedRef<SWidget> MakeCVarCheckRow(const FString& Label, const TCHAR* CVarName) const;
	TSharedRef<SWidget> MakeAltitudeButton(const FString& Label, double AltitudeM);
	TSharedRef<SWidget> MakeDebugColorButton(const FString& Label, int32 Mode) const;

	APlanetTerrainNextMoon* SpawnWithPlacement(EPlanetTerrainNextSpawnPlacement Placement);
	FText StatusText() const;
	FText ModeText() const;
	FText TerrainStatsText() const;

	FPlanetTerrainNextDescriptor Descriptor;
	double DistanceRadii = 3.0;
	FString LastStatus;
	FOnPlanetTerrainNextSpawnPanelClosed OnClosed;
};
