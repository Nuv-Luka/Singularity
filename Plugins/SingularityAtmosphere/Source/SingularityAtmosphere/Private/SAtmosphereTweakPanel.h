#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE(FOnAtmosphereTweakPanelClosed);

/** Live atmosphere optical tweak panel (writes r.SingularityAtmosphere.* CVars). */
class SAtmosphereTweakPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAtmosphereTweakPanel) {}
		SLATE_EVENT(FOnAtmosphereTweakPanelClosed, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> MakeFloatSlider(
		const FText& Label,
		TFunction<float()> Getter,
		TFunction<void(float)> Setter,
		float MinValue,
		float MaxValue);

	TSharedRef<SWidget> MakeIntCombo(
		const FText& Label,
		TFunction<int32()> Getter,
		TFunction<void(int32)> Setter,
		const TArray<TSharedPtr<FString>>& Options);

	TSharedRef<SWidget> MakeRgbRow(
		const FText& Label,
		TFunction<float()> GetR,
		TFunction<void(float)> SetR,
		TFunction<float()> GetG,
		TFunction<void(float)> SetG,
		TFunction<float()> GetB,
		TFunction<void(float)> SetB);

	FOnAtmosphereTweakPanelClosed OnClosed;
	TArray<TSharedPtr<FString>> QualityOptions;
	TArray<TSharedPtr<FString>> DebugOptions;
	TArray<TSharedPtr<FString>> ColorPresetOptions;
};
