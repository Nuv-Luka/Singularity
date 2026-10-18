#include "SAtmosphereTweakPanel.h"

#include "SingularityAtmosphereSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
FText FormatValue(float V)
{
	return FText::FromString(FString::Printf(TEXT("%.2f"), V));
}
} // namespace

void SAtmosphereTweakPanel::Construct(const FArguments& InArgs)
{
	OnClosed = InArgs._OnClosed;

	QualityOptions = {
		MakeShared<FString>(TEXT("0 Orbital")),
		MakeShared<FString>(TEXT("1 Auto LUTs")),
		MakeShared<FString>(TEXT("2 Force LUTs")),
	};
	DebugOptions = {
		MakeShared<FString>(TEXT("0 Scatter")),
		MakeShared<FString>(TEXT("1 Mask")),
		MakeShared<FString>(TEXT("2 Segment")),
		MakeShared<FString>(TEXT("3 Identity")),
	};
	ColorPresetOptions = {
		MakeShared<FString>(TEXT("Neutral")),
		MakeShared<FString>(TEXT("Earth")),
		MakeShared<FString>(TEXT("Mars")),
		MakeShared<FString>(TEXT("Titan")),
		MakeShared<FString>(TEXT("Ice")),
		MakeShared<FString>(TEXT("Toxic")),
		MakeShared<FString>(TEXT("Sunset")),
		MakeShared<FString>(TEXT("Custom")),
	};

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(380.0f)
		.MaxDesiredHeight(720.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.05f, 0.08f, 0.92f))
			.Padding(FMargin(12.0f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Atmosphere Tweaks")))
						.ColorAndOpacity(FLinearColor(0.75f, 0.9f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("X")))
						.OnClicked_Lambda([this]()
						{
							OnClosed.ExecuteIfBound();
							return FReply::Handled();
						})
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("F7 toggle  ·  changes apply live")))
					.ColorAndOpacity(FLinearColor(0.55f, 0.6f, 0.65f))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
				[
					SNew(SSeparator)
				]

				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([]()
							{
								return SingularityAtmosphere::CVarEnable.GetValueOnGameThread() != 0
									? ECheckBoxState::Checked
									: ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([](ECheckBoxState State)
							{
								SingularityAtmosphere::CVarEnable->Set(
									State == ECheckBoxState::Checked ? 1 : 0, ECVF_SetByCode);
							})
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("Enable atmosphere")))
							]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
						[
							MakeIntCombo(
								FText::FromString(TEXT("Quality")),
								[]() { return SingularityAtmosphere::CVarQuality.GetValueOnGameThread(); },
								[](int32 V) { SingularityAtmosphere::CVarQuality->Set(V, ECVF_SetByCode); },
								QualityOptions)
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
						[
							MakeIntCombo(
								FText::FromString(TEXT("Debug")),
								[]() { return SingularityAtmosphere::CVarDebug.GetValueOnGameThread(); },
								[](int32 V) { SingularityAtmosphere::CVarDebug->Set(V, ECVF_SetByCode); },
								DebugOptions)
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
						[
							SNew(SSeparator)
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Color combination")))
							.ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.0f))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
						[
							MakeIntCombo(
								FText::FromString(TEXT("Preset")),
								[]() { return SingularityAtmosphere::CVarColorPreset.GetValueOnGameThread(); },
								[](int32 V) { SingularityAtmosphere::ApplyColorPreset(V); },
								ColorPresetOptions)
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							MakeRgbRow(
								FText::FromString(TEXT("Rayleigh tint")),
								[]() { return SingularityAtmosphere::CVarRayleighTintR.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarRayleighTintR->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarRayleighTintG.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarRayleighTintG->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarRayleighTintB.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarRayleighTintB->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							MakeRgbRow(
								FText::FromString(TEXT("Mie tint")),
								[]() { return SingularityAtmosphere::CVarMieTintR.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarMieTintR->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarMieTintG.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarMieTintG->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarMieTintB.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarMieTintB->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							MakeRgbRow(
								FText::FromString(TEXT("Ozone tint")),
								[]() { return SingularityAtmosphere::CVarOzoneTintR.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarOzoneTintR->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarOzoneTintG.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarOzoneTintG->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarOzoneTintB.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarOzoneTintB->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							MakeRgbRow(
								FText::FromString(TEXT("Disk haze tint")),
								[]() { return SingularityAtmosphere::CVarHazeTintR.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarHazeTintR->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarHazeTintG.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarHazeTintG->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								},
								[]() { return SingularityAtmosphere::CVarHazeTintB.GetValueOnGameThread(); },
								[](float V)
								{
									SingularityAtmosphere::CVarHazeTintB->Set(V, ECVF_SetByCode);
									SingularityAtmosphere::CVarColorPreset->Set(7, ECVF_SetByCode);
								})
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
						[
							SNew(SSeparator)
						]

						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Density")),
								[]() { return SingularityAtmosphere::CVarDensityScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarDensityScale->Set(V, ECVF_SetByCode); },
								0.0f, 5.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Rayleigh")),
								[]() { return SingularityAtmosphere::CVarRayleighScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarRayleighScale->Set(V, ECVF_SetByCode); },
								0.0f, 5.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Mie")),
								[]() { return SingularityAtmosphere::CVarMieScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarMieScale->Set(V, ECVF_SetByCode); },
								0.0f, 5.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Disk haze (fill face)")),
								[]() { return SingularityAtmosphere::CVarDiskHaze.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarDiskHaze->Set(V, ECVF_SetByCode); },
								0.5f, 10.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Disk opacity / transparency")),
								[]() { return SingularityAtmosphere::CVarDiskOpacity.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarDiskOpacity->Set(V, ECVF_SetByCode); },
								0.0f, 2.5f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Ozone / absorption")),
								[]() { return SingularityAtmosphere::CVarOzoneScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarOzoneScale->Set(V, ECVF_SetByCode); },
								0.0f, 5.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Height (thickness)")),
								[]() { return SingularityAtmosphere::CVarHeightScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarHeightScale->Set(V, ECVF_SetByCode); },
								0.2f, 3.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Rayleigh scale height")),
								[]() { return SingularityAtmosphere::CVarRayleighHeightScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarRayleighHeightScale->Set(V, ECVF_SetByCode); },
								0.2f, 3.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Mie scale height")),
								[]() { return SingularityAtmosphere::CVarMieHeightScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarMieHeightScale->Set(V, ECVF_SetByCode); },
								0.2f, 3.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Illuminance")),
								[]() { return SingularityAtmosphere::CVarIlluminanceScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarIlluminanceScale->Set(V, ECVF_SetByCode); },
								0.0f, 5.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Fade")),
								[]() { return SingularityAtmosphere::CVarFadeScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarFadeScale->Set(V, ECVF_SetByCode); },
								0.0f, 2.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Ground bounce")),
								[]() { return SingularityAtmosphere::CVarGroundBounceScale.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarGroundBounceScale->Set(V, ECVF_SetByCode); },
								0.0f, 5.0f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Mie G (−0.01 = descriptor)")),
								[]() { return SingularityAtmosphere::CVarMieG.GetValueOnGameThread(); },
								[](float V) { SingularityAtmosphere::CVarMieG->Set(V, ECVF_SetByCode); },
								-0.01f, 0.95f)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeFloatSlider(
								FText::FromString(TEXT("Max planets")),
								[]() { return static_cast<float>(SingularityAtmosphere::CVarMaxPlanets.GetValueOnGameThread()); },
								[](float V)
								{
									SingularityAtmosphere::CVarMaxPlanets->Set(
										FMath::Clamp(FMath::RoundToInt(V), 1, 4), ECVF_SetByCode);
								},
								1.0f, 4.0f)
						]
					]
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 6, 0)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(FText::FromString(TEXT("Reset defaults")))
						.OnClicked_Lambda([]()
						{
							SingularityAtmosphere::ResetLiveTweaks();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(FText::FromString(TEXT("Close (F7)")))
						.OnClicked_Lambda([this]()
						{
							OnClosed.ExecuteIfBound();
							return FReply::Handled();
						})
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> SAtmosphereTweakPanel::MakeFloatSlider(
	const FText& Label,
	TFunction<float()> Getter,
	TFunction<void(float)> Setter,
	float MinValue,
	float MaxValue)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.9f))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([Getter]() { return FormatValue(Getter()); })
				.ColorAndOpacity(FLinearColor(0.45f, 0.85f, 1.0f))
				.MinDesiredWidth(40.0f)
				.Justification(ETextJustify::Right)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 2)
		[
			SNew(SSlider)
			.Value_Lambda([Getter, MinValue, MaxValue]()
			{
				return FMath::GetMappedRangeValueClamped(
					FVector2D(MinValue, MaxValue), FVector2D(0.0, 1.0), Getter());
			})
			.OnValueChanged_Lambda([Setter, MinValue, MaxValue](float Normalized)
			{
				Setter(FMath::GetMappedRangeValueClamped(
					FVector2D(0.0, 1.0), FVector2D(MinValue, MaxValue), Normalized));
			})
		];
}

TSharedRef<SWidget> SAtmosphereTweakPanel::MakeIntCombo(
	const FText& Label,
	TFunction<int32()> Getter,
	TFunction<void(int32)> Setter,
	const TArray<TSharedPtr<FString>>& Options)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.45f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Label)
		]
		+ SHorizontalBox::Slot().FillWidth(0.55f)
		[
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&Options)
			.InitiallySelectedItem(Options.IsValidIndex(Getter()) ? Options[Getter()] : Options[0])
			.OnSelectionChanged_Lambda([Setter, Options](TSharedPtr<FString> Item, ESelectInfo::Type)
			{
				if (!Item.IsValid())
				{
					return;
				}
				for (int32 I = 0; I < Options.Num(); ++I)
				{
					if (Options[I] == Item)
					{
						Setter(I);
						break;
					}
				}
			})
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
			{
				return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : TEXT("")));
			})
			.Content()
			[
				SNew(STextBlock)
				.Text_Lambda([Getter, Options]()
				{
					const int32 I = FMath::Clamp(Getter(), 0, Options.Num() - 1);
					return FText::FromString(Options.IsValidIndex(I) ? *Options[I] : TEXT(""));
				})
			]
		];
}

TSharedRef<SWidget> SAtmosphereTweakPanel::MakeRgbRow(
	const FText& Label,
	TFunction<float()> GetR,
	TFunction<void(float)> SetR,
	TFunction<float()> GetG,
	TFunction<void(float)> SetG,
	TFunction<float()> GetB,
	TFunction<void(float)> SetB)
{
	auto Channel = [](const FText& ChanLabel, FLinearColor Accent, TFunction<float()> Getter, TFunction<void(float)> Setter) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(ChanLabel)
				.ColorAndOpacity(Accent)
				.MinDesiredWidth(12.0f)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.Value_Lambda([Getter]()
				{
					return FMath::GetMappedRangeValueClamped(FVector2D(0.0, 2.5), FVector2D(0.0, 1.0), Getter());
				})
				.OnValueChanged_Lambda([Setter](float Normalized)
				{
					Setter(FMath::GetMappedRangeValueClamped(FVector2D(0.0, 1.0), FVector2D(0.0, 2.5), Normalized));
				})
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.9f))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(40.0f)
				.HeightOverride(14.0f)
				[
					SNew(SColorBlock)
					.Color_Lambda([GetR, GetG, GetB]()
					{
						return FLinearColor(
							FMath::Clamp(GetR(), 0.0f, 1.0f),
							FMath::Clamp(GetG(), 0.0f, 1.0f),
							FMath::Clamp(GetB(), 0.0f, 1.0f),
							1.0f);
					})
					.ShowBackgroundForAlpha(false)
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Channel(FText::FromString(TEXT("R")), FLinearColor(1.0f, 0.35f, 0.35f), GetR, SetR)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Channel(FText::FromString(TEXT("G")), FLinearColor(0.35f, 1.0f, 0.4f), GetG, SetG)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			Channel(FText::FromString(TEXT("B")), FLinearColor(0.4f, 0.55f, 1.0f), GetB, SetB)
		];
}
