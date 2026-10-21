#include "SPlanetTerrainNextSpawnPanel.h"

#include "PlanetTerrainNextComponent.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextMoonActor.h"
#include "PlanetTerrainNextSettings.h"
#include "GameFramework/PlayerController.h"
#include "PlanetTerrainNextUI.h"
#include "SingularityPlanetTerrainNextMode.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
UWorld* GetViewportWorld()
{
	return (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetWorld() : nullptr;
}

FSlateColor PanelTextColor()
{
	return FSlateColor(FLinearColor(0.92f, 0.92f, 0.90f));
}

FSlateColor DimTextColor()
{
	return FSlateColor(FLinearColor(0.70f, 0.70f, 0.66f));
}
} // namespace

TSharedRef<SWidget> SPlanetTerrainNextSpawnPanel::SectionHeader(const FString& Title) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Title))
		.ColorAndOpacity(PanelTextColor())
		.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11));
}

TSharedRef<SWidget> SPlanetTerrainNextSpawnPanel::MakeButton(const FString& Label, FOnClicked OnClicked) const
{
	return SNew(SButton)
		.ContentPadding(FMargin(10.0f, 6.0f))
		.OnClicked(OnClicked)
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		];
}

void SPlanetTerrainNextSpawnPanel::Construct(const FArguments& InArgs)
{
	OnClosed = InArgs._OnClosed;
	Descriptor = FPlanetTerrainNextDescriptor{};
	DistanceRadii = 3.0;
	LastStatus = TEXT("Spawn a true-scale moon. In Next mode it runs the R2 quadtree terrain.");

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(380.0f)
		.MaxDesiredHeight(900.0f)
		[
			SNew(SBorder)
			.Padding(14.0f)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Planet Terrain Next")))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))
					.ColorAndOpacity(PanelTextColor())
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("F6 toggle  ·  R2 quadtree terrain (CPU patches)")))
					.ColorAndOpacity(DimTextColor())
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 8.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SectionHeader(TEXT("Moon"))]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Radius (km)")))
								.ColorAndOpacity(DimTextColor())
							]
							+ SHorizontalBox::Slot().FillWidth(0.45f)
							[
								SNew(SNumericEntryBox<double>)
								.Value_Lambda([this]()
								{
									return TOptional<double>(Descriptor.RadiusMeters / 1000.0);
								})
								.OnValueCommitted_Lambda([this](double Value, ETextCommit::Type)
								{
									Descriptor.RadiusMeters = FMath::Clamp(Value, 10.0, 10000.0) * 1000.0;
								})
								.MinValue(10.0)
								.MaxValue(10000.0)
								.MinSliderValue(100.0)
								.MaxSliderValue(4000.0)
								.AllowSpin(true)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Cells / face")))
								.ColorAndOpacity(DimTextColor())
							]
							+ SHorizontalBox::Slot().FillWidth(0.45f)
							[
								SNew(SNumericEntryBox<int32>)
								.Value_Lambda([this]()
								{
									return TOptional<int32>(Descriptor.CellsPerEdge);
								})
								.OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type)
								{
									Descriptor.CellsPerEdge = FMath::Clamp(Value, 4, 64);
								})
								.MinValue(4)
								.MaxValue(64)
								.MinSliderValue(8)
								.MaxSliderValue(32)
								.AllowSpin(true)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
						[
							SectionHeader(TEXT("Geology (opt-in)"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([this]()
							{
								return Descriptor.bEnableGeologicalBiomes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
							{
								Descriptor.bEnableGeologicalBiomes = State == ECheckBoxState::Checked;
							})
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("Enable geological terrain")))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								static const TCHAR* Names[] = {TEXT("Mixed Surface"), TEXT("Young Mare"), TEXT("Ancient Highland")};
								return FText::FromString(FString::Printf(TEXT("Preset: %s"), Names[FMath::Clamp(Descriptor.GeologyPreset, 0, 2)]));
							})
							.ColorAndOpacity(DimTextColor())
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)
							[
								MakeButton(TEXT("Mixed"), FOnClicked::CreateLambda([this]()
								{
									Descriptor.GeologyPreset = 0;
									return FReply::Handled();
								}))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)
							[
								MakeButton(TEXT("Young Mare"), FOnClicked::CreateLambda([this]()
								{
									Descriptor.GeologyPreset = 1;
									return FReply::Handled();
								}))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)
							[
								MakeButton(TEXT("Highland"), FOnClicked::CreateLambda([this]()
								{
									Descriptor.GeologyPreset = 2;
									return FReply::Handled();
								}))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("Distance (radii)")))
								.ColorAndOpacity(DimTextColor())
							]
							+ SHorizontalBox::Slot().FillWidth(0.45f)
							[
								SNew(SNumericEntryBox<double>)
								.Value_Lambda([this]()
								{
									return TOptional<double>(DistanceRadii);
								})
								.OnValueCommitted_Lambda([this](double Value, ETextCommit::Type)
								{
									DistanceRadii = FMath::Clamp(Value, 1.2, 40.0);
								})
								.MinValue(1.2)
								.MaxValue(40.0)
								.AllowSpin(true)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
						[
							SectionHeader(TEXT("Spawn"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							MakeButton(TEXT("Spawn in front of camera"), FOnClicked::CreateSP(this, &SPlanetTerrainNextSpawnPanel::OnSpawnInFront))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							MakeButton(TEXT("Spawn 1 AU along +X"), FOnClicked::CreateSP(this, &SPlanetTerrainNextSpawnPanel::OnSpawnFar))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							MakeButton(TEXT("Spawn at world origin"), FOnClicked::CreateSP(this, &SPlanetTerrainNextSpawnPanel::OnSpawnAtOrigin))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							MakeButton(TEXT("Destroy all Next moons"), FOnClicked::CreateSP(this, &SPlanetTerrainNextSpawnPanel::OnDestroyAll))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
						[
							SectionHeader(TEXT("Renderer mode"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(STextBlock)
							.Text(this, &SPlanetTerrainNextSpawnPanel::ModeText)
							.ColorAndOpacity(DimTextColor())
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								MakeButton(TEXT("Legacy"), FOnClicked::CreateSP(this, &SPlanetTerrainNextSpawnPanel::OnSetModeLegacy))
							]
							+ SHorizontalBox::Slot().FillWidth(0.5f).Padding(4.0f, 0.0f, 0.0f, 0.0f)
							[
								MakeButton(TEXT("Next"), FOnClicked::CreateSP(this, &SPlanetTerrainNextSpawnPanel::OnSetModeNext))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
						[
							SectionHeader(TEXT("Fly to altitude (nearest moon)"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeAltitudeButton(TEXT("1000 km"), 1.0e6)]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeAltitudeButton(TEXT("100 km"), 1.0e5)]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeAltitudeButton(TEXT("10 km"), 1.0e4)]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeAltitudeButton(TEXT("1 km"), 1.0e3)]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeAltitudeButton(TEXT("100 m"), 100.0)]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeAltitudeButton(TEXT("10 m"), 10.0)]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
						[
							SectionHeader(TEXT("LOD (R2)"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("Target pixel error"), TEXT("singularity.TerrainNext.TargetPixelError"), 0.25f, 64.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("Max level"), TEXT("singularity.TerrainNext.MaxLevel"), 0, 24)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("Max patches"), TEXT("singularity.TerrainNext.MaxPatches"), 6, 16384)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("Build budget (ms)"), TEXT("singularity.TerrainNext.BuildBudgetMs"), 0.0f, 50.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("Peripheral LOD (1=off)"), TEXT("singularity.TerrainNext.PeripheralLodScale"), 0.25f, 1.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("Deny child (-1 off)"), TEXT("singularity.TerrainNext.DenyChildIndex"), -1, 3)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Freeze LOD"), TEXT("singularity.TerrainNext.FreezeLod"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("R10 Diag (frozen)"), TEXT("singularity.TerrainNext.Diag.Enable"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("Diag mode 0/1"), TEXT("singularity.TerrainNext.Diag.Mode"), 0, 1)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("Diag level"), TEXT("singularity.TerrainNext.Diag.Level"), 0, 12)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Diag height"), TEXT("singularity.TerrainNext.Diag.Height"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Force fresh PMC"), TEXT("singularity.TerrainNext.ForceFreshMeshSection"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Dithered LOD"), TEXT("singularity.TerrainNext.EnableDitheredLOD"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Legacy morph"), TEXT("singularity.TerrainNext.EnableLegacyMorph"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("LOD fade (s)"), TEXT("singularity.TerrainNext.LodTransitionSeconds"), 0.05f, 2.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Pause builds"), TEXT("singularity.TerrainNext.PauseBuilds"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("On-screen stats"), TEXT("singularity.TerrainNext.ShowStats"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Shader morph"), TEXT("singularity.TerrainNext.MorphEnabled"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Skirts"), TEXT("singularity.TerrainNext.SkirtsEnabled"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Morph WPO (off=CPU)"), TEXT("singularity.TerrainNext.MorphWpoEnabled"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("CPU morph max LOD"), TEXT("singularity.TerrainNext.MorphCpuMaxLevel"), -1, 12)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarCheckRow(TEXT("Disk tile cache"), TEXT("singularity.TerrainNext.DiskCache"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("Disk cache max MB"), TEXT("singularity.TerrainNext.DiskCacheMaxMB"), 0, 8192)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("Morph end factor"), TEXT("singularity.TerrainNext.MorphEnd"), 1.01f, 8.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("Morph smooth (s)"), TEXT("singularity.TerrainNext.MorphSmooth"), 0.0f, 2.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("Morph reveal (s)"), TEXT("singularity.TerrainNext.MorphReveal"), 0.0f, 2.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarIntRow(TEXT("Morph handoff frames"), TEXT("singularity.TerrainNext.MorphHandoffFrames"), 1, 60)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
						[
							MakeCVarFloatRow(TEXT("Force morph (-1 off)"), TEXT("singularity.TerrainNext.MorphForce"), -1.0f, 1.0f)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeDebugColorButton(TEXT("Regolith"), 0)]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeDebugColorButton(TEXT("LOD"), 1)]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeDebugColorButton(TEXT("Face"), 2)]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(1.0f)[MakeDebugColorButton(TEXT("Morph"), 3)]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
						[
							SectionHeader(TEXT("Terrain"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(STextBlock)
							.Text(this, &SPlanetTerrainNextSpawnPanel::TerrainStatsText)
							.ColorAndOpacity(PanelTextColor())
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 4.0f)
						[
							SectionHeader(TEXT("Status"))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
						[
							SNew(STextBlock)
							.Text(this, &SPlanetTerrainNextSpawnPanel::StatusText)
							.ColorAndOpacity(PanelTextColor())
							.AutoWrapText(true)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					MakeButton(TEXT("Close (F6)"), FOnClicked::CreateSP(this, &SPlanetTerrainNextSpawnPanel::OnClose))
				]
			]
		]
	];
}

APlanetTerrainNextMoon* SPlanetTerrainNextSpawnPanel::SpawnWithPlacement(EPlanetTerrainNextSpawnPlacement Placement)
{
	UWorld* World = GetViewportWorld();
	if (!World)
	{
		LastStatus = TEXT("No game viewport world.");
		return nullptr;
	}

	FPlanetTerrainNextSpawnRequest Request;
	Request.Descriptor = Descriptor;
	Request.Placement = Placement;
	Request.DistanceRadii = DistanceRadii;

	APlanetTerrainNextMoon* Moon = FPlanetTerrainNextSpawn::Spawn(World, Request);
	if (!Moon)
	{
		LastStatus = TEXT("Spawn failed.");
		return nullptr;
	}

	const FVector Center = Moon->GetActorLocation();
	LastStatus = FString::Printf(
		TEXT("Spawned R=%.0f km  (%.0f uu)  at (%.0f, %.0f, %.0f)"),
		Descriptor.RadiusMeters / 1000.0,
		PlanetTerrainNext::MetersToUu(Descriptor.RadiusMeters),
		Center.X, Center.Y, Center.Z);
	return Moon;
}

FReply SPlanetTerrainNextSpawnPanel::OnSpawnInFront()
{
	SpawnWithPlacement(EPlanetTerrainNextSpawnPlacement::InFrontOfCamera);
	return FReply::Handled();
}

FReply SPlanetTerrainNextSpawnPanel::OnSpawnFar()
{
	SpawnWithPlacement(EPlanetTerrainNextSpawnPlacement::OneAuAlongX);
	return FReply::Handled();
}

FReply SPlanetTerrainNextSpawnPanel::OnSpawnAtOrigin()
{
	SpawnWithPlacement(EPlanetTerrainNextSpawnPlacement::WorldOrigin);
	return FReply::Handled();
}

FReply SPlanetTerrainNextSpawnPanel::OnDestroyAll()
{
	const int32 Count = FPlanetTerrainNextSpawn::DestroyAll(GetViewportWorld());
	LastStatus = FString::Printf(TEXT("Destroyed %d Next moon(s)."), Count);
	return FReply::Handled();
}

FReply SPlanetTerrainNextSpawnPanel::OnClose()
{
	OnClosed.ExecuteIfBound();
	return FReply::Handled();
}

FReply SPlanetTerrainNextSpawnPanel::OnSetModeLegacy()
{
	PlanetTerrainNextSettings::SetCVar(TEXT("singularity.TerrainRenderer.Mode"), TEXT("0"));
	LastStatus = TEXT("Renderer mode = Legacy.");
	return FReply::Handled();
}

FReply SPlanetTerrainNextSpawnPanel::OnSetModeNext()
{
	PlanetTerrainNextSettings::SetCVar(TEXT("singularity.TerrainRenderer.Mode"), TEXT("1"));
	LastStatus = TEXT("Renderer mode = Next.");
	return FReply::Handled();
}

TSharedRef<SWidget> SPlanetTerrainNextSpawnPanel::MakeCVarFloatRow(const FString& Label, const TCHAR* CVarName, float MinValue, float MaxValue) const
{
	const FString Name(CVarName);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(DimTextColor())
		]
		+ SHorizontalBox::Slot().FillWidth(0.45f)
		[
			SNew(SNumericEntryBox<float>)
			.Value_Lambda([Name]()
			{
				const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
				return CVar ? TOptional<float>(CVar->GetFloat()) : TOptional<float>();
			})
			.OnValueCommitted_Lambda([Name, MinValue, MaxValue](float Value, ETextCommit::Type)
			{
				PlanetTerrainNextSettings::SetCVar(*Name, FString::SanitizeFloat(FMath::Clamp(Value, MinValue, MaxValue)));
			})
			.MinValue(MinValue)
			.MaxValue(MaxValue)
			.AllowSpin(false)
		];
}

TSharedRef<SWidget> SPlanetTerrainNextSpawnPanel::MakeCVarIntRow(const FString& Label, const TCHAR* CVarName, int32 MinValue, int32 MaxValue) const
{
	const FString Name(CVarName);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.55f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(DimTextColor())
		]
		+ SHorizontalBox::Slot().FillWidth(0.45f)
		[
			SNew(SNumericEntryBox<int32>)
			.Value_Lambda([Name]()
			{
				const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
				return CVar ? TOptional<int32>(CVar->GetInt()) : TOptional<int32>();
			})
			.OnValueCommitted_Lambda([Name, MinValue, MaxValue](int32 Value, ETextCommit::Type)
			{
				PlanetTerrainNextSettings::SetCVar(*Name, FString::FromInt(FMath::Clamp(Value, MinValue, MaxValue)));
			})
			.MinValue(MinValue)
			.MaxValue(MaxValue)
			.AllowSpin(false)
		];
}

TSharedRef<SWidget> SPlanetTerrainNextSpawnPanel::MakeCVarCheckRow(const FString& Label, const TCHAR* CVarName) const
{
	const FString Name(CVarName);
	return SNew(SCheckBox)
		.IsChecked_Lambda([Name]()
		{
			const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
			return CVar && CVar->GetBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([Name](ECheckBoxState State)
		{
			PlanetTerrainNextSettings::SetCVar(*Name, State == ECheckBoxState::Checked ? TEXT("1") : TEXT("0"));
		})
		[
			SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(PanelTextColor())
		];
}

TSharedRef<SWidget> SPlanetTerrainNextSpawnPanel::MakeAltitudeButton(const FString& Label, double AltitudeM)
{
	return SNew(SButton)
		.ContentPadding(FMargin(6.0f, 4.0f))
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([this, AltitudeM, Label]()
		{
			LastStatus = FPlanetTerrainNextSpawn::PlaceCameraAtAltitude(GetViewportWorld(), AltitudeM)
				? FString::Printf(TEXT("Camera placed %s above the nearest Next moon."), *Label)
				: FString(TEXT("No Next moon or no player pawn."));
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		];
}

TSharedRef<SWidget> SPlanetTerrainNextSpawnPanel::MakeDebugColorButton(const FString& Label, int32 Mode) const
{
	return SNew(SButton)
		.ContentPadding(FMargin(6.0f, 4.0f))
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([Mode]()
		{
			PlanetTerrainNextSettings::SetCVar(TEXT("singularity.TerrainNext.DebugColor"), FString::FromInt(Mode));
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(FText::FromString(Label))
		];
}

FText SPlanetTerrainNextSpawnPanel::TerrainStatsText() const
{
	UWorld* World = GetViewportWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		return FText::FromString(TEXT("No player."));
	}
	FVector View;
	FRotator Rotation;
	Controller->GetPlayerViewPoint(View, Rotation);
	const APlanetTerrainNextMoon* Moon = FPlanetTerrainNextSpawn::FindNearest(World, View);
	const UPlanetTerrainNextComponent* Terrain = Moon ? Moon->GetTerrain() : nullptr;
	if (!Terrain || !Terrain->IsTerrainActive())
	{
		return FText::FromString(TEXT("No active Next terrain."));
	}
	const FPlanetTerrainNextFrameStats& S = Terrain->GetFrameStats();
	return FText::FromString(FString::Printf(
		TEXT("Altitude %.3f km\nDrawn %d patches, deepest L%d\nNodes %d, resident %d, slots %d\nPending %d, built %d this frame\nBuild p95 %.2f ms, tick %.2f ms\nCoverage: %s"),
		S.AltitudeM / 1000.0, S.Drawn, S.MaxDrawnLevel, S.Nodes, S.Resident, S.Slots,
		S.Pending, S.BuiltThisFrame, S.P95BuildMs, S.UpdateMs,
		Terrain->HasFullCoverage() ? TEXT("all six roots resident") : TEXT("MISSING ROOT")));
}

FText SPlanetTerrainNextSpawnPanel::StatusText() const
{
	return FText::FromString(LastStatus);
}

FText SPlanetTerrainNextSpawnPanel::ModeText() const
{
	return FText::FromString(FString::Printf(
		TEXT("Current: %s  (legacy jobs stay off while Next is selected)"),
		SingularityTerrainRendererIsNext() ? TEXT("Next") : TEXT("Legacy")));
}
