#include "SingularityAtmosphereSubsystem.h"

#include "SingularityAtmosphereViewExtension.h"
#include "SAtmosphereTweakPanel.h"
#include "SceneViewExtension.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"

void USingularityAtmosphereSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ViewExtension = FSceneViewExtensions::NewExtension<FSingularityAtmosphereViewExtension>();
}

void USingularityAtmosphereSubsystem::Deinitialize()
{
	HideTweakUIInternal();
	ClearAtmospheres();
	ViewExtension.Reset();
	Super::Deinitialize();
}

bool USingularityAtmosphereSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld() || World->WorldType == EWorldType::Editor;
	}
	return false;
}

void USingularityAtmosphereSubsystem::SetViewSnapshot(FSingularityAtmosphereViewSnapshot Snapshot)
{
	if (ViewExtension.IsValid())
	{
		ViewExtension->SetSnapshot_GameThread(MoveTemp(Snapshot));
	}
}

void USingularityAtmosphereSubsystem::ClearAtmospheres()
{
	FSingularityAtmosphereViewSnapshot Empty;
	Empty.bActive = false;
	Empty.Fade = 0.0f;
	SetViewSnapshot(MoveTemp(Empty));
}

void USingularityAtmosphereSubsystem::ToggleTweakUI()
{
	SetTweakUIVisible(!bTweakUIVisible);
}

void USingularityAtmosphereSubsystem::SetTweakUIVisible(bool bVisible)
{
	if (bVisible == bTweakUIVisible)
	{
		return;
	}
	if (bVisible)
	{
		ShowTweakUIInternal();
	}
	else
	{
		HideTweakUIInternal();
	}
}

void USingularityAtmosphereSubsystem::ShowTweakUIInternal()
{
	UWorld* World = GetWorld();
	if (!World || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	if (!TweakPanelWidget.IsValid())
	{
		TWeakObjectPtr<USingularityAtmosphereSubsystem> WeakThis(this);
		TSharedRef<SWidget> Panel = SNew(SAtmosphereTweakPanel)
			.OnClosed_Lambda([WeakThis]()
			{
				if (USingularityAtmosphereSubsystem* Sub = WeakThis.Get())
				{
					Sub->SetTweakUIVisible(false);
				}
			});

		TSharedRef<SWidget> Anchored =
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(FMargin(16.0f, 48.0f, 16.0f, 16.0f))
			[
				Panel
			];

		TweakPanelWidget = Anchored;
	}

	GEngine->GameViewport->AddViewportWidgetContent(TweakPanelWidget.ToSharedRef(), 100);
	bTweakUIVisible = true;

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		if (TweakPanelWidget.IsValid())
		{
			Mode.SetWidgetToFocus(TweakPanelWidget);
		}
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
}

void USingularityAtmosphereSubsystem::HideTweakUIInternal()
{
	if (TweakPanelWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(TweakPanelWidget.ToSharedRef());
	}
	TweakPanelWidget.Reset();
	bTweakUIVisible = false;
	RestoreGameInput();
}

void USingularityAtmosphereSubsystem::RestoreGameInput()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}
