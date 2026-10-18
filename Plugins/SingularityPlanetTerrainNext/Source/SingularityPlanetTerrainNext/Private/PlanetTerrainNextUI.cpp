#include "PlanetTerrainNextUI.h"
#include "SPlanetTerrainNextSpawnPanel.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"

namespace
{
TSharedPtr<SWidget> GSpawnWidget;
bool bSpawnUIOpen = false;

void RestoreGameMouse()
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
}
} // namespace

bool FPlanetTerrainNextUI::IsOpen()
{
	return bSpawnUIOpen;
}

void FPlanetTerrainNextUI::Close()
{
	if (!bSpawnUIOpen)
	{
		return;
	}
	if (GEngine && GEngine->GameViewport && GSpawnWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(GSpawnWidget.ToSharedRef());
	}
	GSpawnWidget.Reset();
	bSpawnUIOpen = false;
	RestoreGameMouse();
}

void FPlanetTerrainNextUI::Toggle()
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	if (bSpawnUIOpen)
	{
		Close();
		return;
	}

	TSharedRef<SWidget> Panel = SNew(SPlanetTerrainNextSpawnPanel)
		.OnClosed_Lambda([]()
		{
			FPlanetTerrainNextUI::Close();
		});

	TSharedRef<SWidget> Anchored =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(12.0f, 48.0f, 12.0f, 12.0f)
		[
			Panel
		];

	GSpawnWidget = Anchored;
	GEngine->GameViewport->AddViewportWidgetContent(GSpawnWidget.ToSharedRef(), 120);
	bSpawnUIOpen = true;

	if (UWorld* World = GEngine->GameViewport->GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			Mode.SetWidgetToFocus(GSpawnWidget);
			PC->SetInputMode(Mode);
			PC->bShowMouseCursor = true;
		}
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Panel, EFocusCause::SetDirectly);
	}
}
