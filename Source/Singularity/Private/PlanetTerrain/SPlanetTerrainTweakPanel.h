#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE(FOnPlanetTerrainTweakPanelClosed);

class SPlanetTerrainTweakPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanetTerrainTweakPanel) {}
		SLATE_EVENT(FOnPlanetTerrainTweakPanelClosed, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> SectionHeader(const FString& Title);
	TSharedRef<SWidget> MakeFloatSlider(const FString& Label, float* Value, float MinV, float MaxV);
	TSharedRef<SWidget> MakeIntSlider(const FString& Label, int32* Value, int32 MinV, int32 MaxV);
	TSharedRef<SWidget> MakeCheck(const FString& Label, bool* Value);

	FOnPlanetTerrainTweakPanelClosed OnClosed;
};
