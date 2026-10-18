#include "PlanetTerrain/PlanetTerrainTweak.h"
#include "PlanetTerrain/PlanetTerrainComponent.h"
#include "PlanetTerrain/PlanetTerrainDebug.h"
#include "PlanetTerrain/PlanetTerrainScheduler.h"
#include "PlanetTerrain/PlanetTerrainQuadtree.h"
#include "SPlanetTerrainTweakPanel.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"

namespace
{
FPlanetTerrainTweakSettings GTweakSettings;
TSharedPtr<SWidget> GTweakWidget;
bool bTweakUIOpen = false;
} // namespace

void FPlanetTerrainTweakSettings::ResetToDefaults()
{
	*this = FPlanetTerrainTweakSettings{};
	bDirty = true;
}

FString FPlanetTerrainTweakSettings::DumpText() const
{
	FString S;
	S += TEXT("=== PlanetTerrain Tweak Dump ===\n");
	S += FString::Printf(TEXT("bApplyLive=%d\n"), bApplyLive ? 1 : 0);
	S += FString::Printf(TEXT("bUseScreenSpaceLod=%d\n"), bUseScreenSpaceLod ? 1 : 0);
	S += FString::Printf(TEXT("TargetPixelError=%.3f\n"), TargetPixelError);
	S += FString::Printf(TEXT("PeripheralPixelError=%.3f\n"), PeripheralPixelError);
	S += FString::Printf(TEXT("PrefetchPixelError=%.3f\n"), PrefetchPixelError);
	S += FString::Printf(TEXT("FoveaInnerRadius=%.3f\n"), FoveaInnerRadius);
	S += FString::Printf(TEXT("FoveaOuterRadius=%.3f\n"), FoveaOuterRadius);
	S += FString::Printf(TEXT("GeometricErrorAmpFrac=%.3f\n"), GeometricErrorAmpFrac);
	S += FString::Printf(TEXT("GeometricErrorSizeFrac=%.3f\n"), GeometricErrorSizeFrac);
	S += FString::Printf(TEXT("LodDistanceFactor=%.3f\n"), LodDistanceFactor);
	S += FString::Printf(TEXT("MergeHysteresis=%.3f\n"), MergeHysteresis);
	S += FString::Printf(TEXT("CullAngularMarginDeg=%.3f\n"), CullAngularMarginDeg);
	S += FString::Printf(TEXT("FrustumMinDot=%.3f\n"), FrustumMinDot);
	S += FString::Printf(TEXT("FrustumPrefetchMinDot=%.3f\n"), FrustumPrefetchMinDot);
	S += FString::Printf(TEXT("HorizonReliefMarginKm=%.3f\n"), HorizonReliefMarginKm);
	S += FString::Printf(TEXT("ActivateDistanceRadii=%.3f\n"), ActivateDistanceRadii);
	S += FString::Printf(TEXT("bUseAltitudeBands=%d\n"), bUseAltitudeBands ? 1 : 0);
	S += FString::Printf(TEXT("BandMaxLevel=%d,%d,%d,%d,%d\n"),
		BandMaxLevel[0], BandMaxLevel[1], BandMaxLevel[2], BandMaxLevel[3], BandMaxLevel[4]);
	S += FString::Printf(TEXT("BandMinNeigh=%d,%d,%d,%d,%d\n"),
		BandMinNeigh[0], BandMinNeigh[1], BandMinNeigh[2], BandMinNeigh[3], BandMinNeigh[4]);
	S += FString::Printf(TEXT("MaxLevelOverride=%d\n"), MaxLevelOverride);
	S += FString::Printf(TEXT("MinNeighborhoodOverride=%d\n"), MinNeighborhoodOverride);
	S += FString::Printf(TEXT("MaxActivePatches=%d\n"), MaxActivePatches);
	S += FString::Printf(TEXT("PatchResolution=%d\n"), PatchResolution);
	S += FString::Printf(TEXT("UploadBudgetMs=%.2f\n"), UploadBudgetMs);
	S += FString::Printf(TEXT("MorphDurationSeconds=%.2f\n"), MorphDurationSeconds);
	S += FString::Printf(TEXT("bGeomorph=%d\n"), bGeomorph ? 1 : 0);
	S += FString::Printf(TEXT("GpuMeshGraceFrames=%d\n"), GpuMeshGraceFrames);
	S += FString::Printf(TEXT("MaxWorkers=%d\n"), MaxWorkers);
	S += FString::Printf(TEXT("bForceCpu=%d\n"), bForceCpu ? 1 : 0);
	S += FString::Printf(TEXT("bEnableCollision=%d\n"), bEnableCollision ? 1 : 0);
	S += FString::Printf(TEXT("ShellScale=%.4f\n"), ShellScale);
	S += FString::Printf(TEXT("bForceShell=%d\n"), bForceShell ? 1 : 0);
	S += FString::Printf(TEXT("bPauseGeneration=%d\n"), bPauseGeneration ? 1 : 0);
	S += FString::Printf(TEXT("WorldOriginRebaseCooldownFrames=%d\n"), WorldOriginRebaseCooldownFrames);
	return S;
}

void FPlanetTerrainTweakSettings::ApplyTo(UPlanetTerrainComponent& Terrain) const
{
	if (!bApplyLive)
	{
		return;
	}
	Terrain.ApplyTweakSettings(*this);
}

FPlanetTerrainTweakSettings& FPlanetTerrainTweak::Settings()
{
	return GTweakSettings;
}

bool FPlanetTerrainTweak::IsUIOpen()
{
	return bTweakUIOpen;
}

void FPlanetTerrainTweak::ApplyToActive()
{
	if (UPlanetTerrainComponent* T = FPlanetTerrainDebug::ActiveTerrain.Get())
	{
		GTweakSettings.ApplyTo(*T);
	}
}

void FPlanetTerrainTweak::ToggleUI()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	auto RestoreGameMouse = []()
	{
		if (GEngine && GEngine->GameViewport)
		{
			if (UWorld* World = GEngine->GameViewport->GetWorld())
			{
				if (APlayerController* PC = World->GetFirstPlayerController())
				{
					FInputModeGameOnly Mode;
					PC->SetInputMode(Mode);
					PC->bShowMouseCursor = false;
				}
			}
		}
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
	};

	if (bTweakUIOpen && GTweakWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(GTweakWidget.ToSharedRef());
		GTweakWidget.Reset();
		bTweakUIOpen = false;
		RestoreGameMouse();
		return;
	}

	TSharedRef<SWidget> Panel = SNew(SPlanetTerrainTweakPanel)
		.OnClosed_Lambda([]()
		{
			FPlanetTerrainTweak::ToggleUI();
		});

	TSharedRef<SWidget> Anchored =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(12, 48, 12, 12)
		[
			Panel
		];

	GTweakWidget = Anchored;
	GEngine->GameViewport->AddViewportWidgetContent(GTweakWidget.ToSharedRef(), 110);
	bTweakUIOpen = true;

	// Release look-capture so Slate sliders/checkboxes receive the mouse (same as F7 atmosphere).
	if (UWorld* World = GEngine->GameViewport->GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			Mode.SetWidgetToFocus(GTweakWidget);
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
		}
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Panel, EFocusCause::SetDirectly);
	}
}

void PlanetTerrainTweakCopyDumpToClipboard()
{
	const FString Text = FPlanetTerrainTweak::Settings().DumpText();
	FPlatformApplicationMisc::ClipboardCopy(*Text);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Text);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(7720, 4.0f, FColor::Green,
			TEXT("PlanetTerrain tweak dump copied to clipboard"));
	}
}
