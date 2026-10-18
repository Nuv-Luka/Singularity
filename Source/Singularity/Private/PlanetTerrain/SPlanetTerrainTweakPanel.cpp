#include "SPlanetTerrainTweakPanel.h"
#include "PlanetTerrain/PlanetTerrainTweak.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

extern void PlanetTerrainTweakCopyDumpToClipboard();

namespace
{
FText FmtF(float V) { return FText::FromString(FString::Printf(TEXT("%.3g"), V)); }
FText FmtI(int32 V) { return FText::FromString(FString::Printf(TEXT("%d"), V)); }
} // namespace

TSharedRef<SWidget> SPlanetTerrainTweakPanel::SectionHeader(const FString& Title)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Title))
			.ColorAndOpacity(FLinearColor(0.75f, 0.9f, 1.0f))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(SSeparator)
		];
}

TSharedRef<SWidget> SPlanetTerrainTweakPanel::MakeCheck(const FString& Label, bool* Value)
{
	return SNew(SCheckBox)
		.IsChecked_Lambda([Value]()
		{
			return (Value && *Value) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([Value](ECheckBoxState State)
		{
			if (Value)
			{
				*Value = (State == ECheckBoxState::Checked);
				FPlanetTerrainTweak::Settings().bDirty = true;
				FPlanetTerrainTweak::ApplyToActive();
			}
		})
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.9f))
		];
}

TSharedRef<SWidget> SPlanetTerrainTweakPanel::MakeFloatSlider(const FString& Label, float* Value, float MinV, float MaxV)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.9f))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([Value]() { return Value ? FmtF(*Value) : FText(); })
				.ColorAndOpacity(FLinearColor(0.45f, 0.9f, 1.0f))
				.MinDesiredWidth(48.0f)
				.Justification(ETextJustify::Right)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value_Lambda([Value, MinV, MaxV]()
			{
				return Value ? FMath::GetMappedRangeValueClamped(
					FVector2D(MinV, MaxV), FVector2D(0.0, 1.0), *Value) : 0.0f;
			})
			.OnValueChanged_Lambda([Value, MinV, MaxV](float N)
			{
				if (Value)
				{
					*Value = FMath::GetMappedRangeValueClamped(
						FVector2D(0.0, 1.0), FVector2D(MinV, MaxV), N);
					FPlanetTerrainTweak::Settings().bDirty = true;
					FPlanetTerrainTweak::ApplyToActive();
				}
			})
		];
}

TSharedRef<SWidget> SPlanetTerrainTweakPanel::MakeIntSlider(const FString& Label, int32* Value, int32 MinV, int32 MaxV)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.9f))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([Value]() { return Value ? FmtI(*Value) : FText(); })
				.ColorAndOpacity(FLinearColor(0.45f, 0.9f, 1.0f))
				.MinDesiredWidth(40.0f)
				.Justification(ETextJustify::Right)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 2)
		[
			SNew(SSlider)
			.Value_Lambda([Value, MinV, MaxV]()
			{
				return Value ? FMath::GetMappedRangeValueClamped(
					FVector2D(static_cast<float>(MinV), static_cast<float>(MaxV)),
					FVector2D(0.0, 1.0), static_cast<float>(*Value)) : 0.0f;
			})
			.OnValueChanged_Lambda([Value, MinV, MaxV](float N)
			{
				if (Value)
				{
					*Value = FMath::RoundToInt(FMath::GetMappedRangeValueClamped(
						FVector2D(0.0, 1.0),
						FVector2D(static_cast<float>(MinV), static_cast<float>(MaxV)), N));
					FPlanetTerrainTweak::Settings().bDirty = true;
					FPlanetTerrainTweak::ApplyToActive();
				}
			})
		];
}

void SPlanetTerrainTweakPanel::Construct(const FArguments& InArgs)
{
	OnClosed = InArgs._OnClosed;
	FPlanetTerrainTweakSettings& S = FPlanetTerrainTweak::Settings();

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(420.0f)
		.MaxDesiredHeight(820.0f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
			.BorderBackgroundColor(FLinearColor(0.03f, 0.04f, 0.06f, 0.94f))
			.Padding(FMargin(12.0f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Planet Terrain Tweaks")))
						.ColorAndOpacity(FLinearColor(0.8f, 0.92f, 1.0f))
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Dump")))
						.ToolTipText(FText::FromString(TEXT("Copy all values to clipboard")))
						.OnClicked_Lambda([]()
						{
							PlanetTerrainTweakCopyDumpToClipboard();
							return FReply::Handled();
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Reset")))
						.OnClicked_Lambda([]()
						{
							FPlanetTerrainTweak::Settings().ResetToDefaults();
							FPlanetTerrainTweak::Settings().bDirty = true;
							FPlanetTerrainTweak::ApplyToActive();
							return FReply::Handled();
						})
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
					.Text(FText::FromString(TEXT("F6 toggle  ·  live on nearest moon  ·  Dump when happy")))
					.ColorAndOpacity(FLinearColor(0.55f, 0.6f, 0.65f))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
				[
					MakeCheck(TEXT("Apply live to ActiveTerrain"), &S.bApplyLive)
				]

				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("Screen-space LOD"))]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Use screen-space LOD"), &S.bUseScreenSpaceLod)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Target pixel error"), &S.TargetPixelError, 0.5f, 12.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Peripheral pixel error"), &S.PeripheralPixelError, 1.0f, 20.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Prefetch pixel error"), &S.PrefetchPixelError, 2.0f, 40.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Fovea inner radius"), &S.FoveaInnerRadius, 0.05f, 0.8f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Fovea outer radius"), &S.FoveaOuterRadius, 0.2f, 1.5f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Geom err amp frac"), &S.GeometricErrorAmpFrac, 0.05f, 1.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Geom err size frac"), &S.GeometricErrorSizeFrac, 0.02f, 0.6f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("LOD distance factor"), &S.LodDistanceFactor, 0.5f, 8.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Merge hysteresis"), &S.MergeHysteresis, 1.0f, 3.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Cull angular margin (deg)"), &S.CullAngularMarginDeg, 0.0f, 30.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Frustum min dot"), &S.FrustumMinDot, -1.0f, 0.5f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Frustum prefetch min dot"), &S.FrustumPrefetchMinDot, -1.0f, 0.5f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Horizon relief margin (km)"), &S.HorizonReliefMarginKm, 0.0f, 50.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Activate distance (×R)"), &S.ActivateDistanceRadii, 1.0f, 40.0f)]

						+ SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("Depth / patch budget"))]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Use altitude MaxLevel bands"), &S.bUseAltitudeBands)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band0 MaxLevel (>1000km)"), &S.BandMaxLevel[0], 2, 16)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band1 MaxLevel (100–1000km)"), &S.BandMaxLevel[1], 2, 16)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band2 MaxLevel (10–100km)"), &S.BandMaxLevel[2], 2, 16)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band3 MaxLevel (1–10km)"), &S.BandMaxLevel[3], 2, 16)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band4 MaxLevel (<1km)"), &S.BandMaxLevel[4], 2, 16)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band0 MinNeighborhood"), &S.BandMinNeigh[0], 0, 10)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band1 MinNeighborhood"), &S.BandMinNeigh[1], 0, 10)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band2 MinNeighborhood"), &S.BandMinNeigh[2], 0, 10)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band3 MinNeighborhood"), &S.BandMinNeigh[3], 0, 10)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Band4 MinNeighborhood"), &S.BandMinNeigh[4], 0, 10)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("MaxLevel override (if bands off)"), &S.MaxLevelOverride, 2, 16)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("MinNeighborhood override"), &S.MinNeighborhoodOverride, 0, 12)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Max active patches"), &S.MaxActivePatches, 32, 512)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Patch resolution"), &S.PatchResolution, 8, 64)]

						+ SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("Mesh / workers / residency"))]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Upload budget (ms/frame)"), &S.UploadBudgetMs, 0.25f, 12.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Morph duration (s)"), &S.MorphDurationSeconds, 0.0f, 2.5f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Geomorph enabled"), &S.bGeomorph)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("GPU mesh grace frames"), &S.GpuMeshGraceFrames, 0, 1200)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("Max workers"), &S.MaxWorkers, 1, 12)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Force CPU bake"), &S.bForceCpu)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Enable collision cook"), &S.bEnableCollision)]
						+ SVerticalBox::Slot().AutoHeight()[MakeFloatSlider(TEXT("Distant shell scale"), &S.ShellScale, 0.95f, 1.0f)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Force shell visible"), &S.bForceShell)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Pause generation"), &S.bPauseGeneration)]
						+ SVerticalBox::Slot().AutoHeight()[MakeIntSlider(TEXT("World-origin rebase cooldown"), &S.WorldOriginRebaseCooldownFrames, 0, 600)]

						+ SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("Debug overlays"))]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show terrain HUD"), &S.bShowHud)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show requested tiles"), &S.bShowRequested)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show culled tiles"), &S.bShowCulled)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show coverage"), &S.bShowCoverage)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show rejection reason"), &S.bShowRejection)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show morph alpha"), &S.bShowMorph)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show foveation"), &S.bShowFoveation)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show seams"), &S.bShowSeams)]
						+ SVerticalBox::Slot().AutoHeight()[MakeCheck(TEXT("Show faces"), &S.bShowFaces)]

						+ SVerticalBox::Slot().AutoHeight().Padding(0, 16)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("When it looks right → Dump → paste values in chat")))
							.ColorAndOpacity(FLinearColor(0.5f, 0.75f, 0.55f))
							.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
						]
					]
				]
			]
		]
	];
}
