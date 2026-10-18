#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SingularityAtmosphereTypes.h"
#include "SingularityAtmosphereSubsystem.generated.h"

class FSingularityAtmosphereViewExtension;
class SWidget;

/**
 * Game-thread owner for the atmosphere view extension + snapshot push + tweak UI.
 */
UCLASS()
class SINGULARITYATMOSPHERE_API USingularityAtmosphereSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Push an immutable per-frame snapshot (planets + camera). Empty/inactive disables the pass. */
	void SetViewSnapshot(FSingularityAtmosphereViewSnapshot Snapshot);

	/** Clear atmospheres (galaxy view / teardown). */
	void ClearAtmospheres();

	/** Show/hide the live atmosphere tweak panel (F8 / SingularityAtmosphere.ToggleUI). */
	UFUNCTION(BlueprintCallable, Category = "SingularityAtmosphere")
	void ToggleTweakUI();

	UFUNCTION(BlueprintCallable, Category = "SingularityAtmosphere")
	void SetTweakUIVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "SingularityAtmosphere")
	bool IsTweakUIVisible() const { return bTweakUIVisible; }

private:
	void HideTweakUIInternal();
	void ShowTweakUIInternal();
	void RestoreGameInput();

	TSharedPtr<FSingularityAtmosphereViewExtension, ESPMode::ThreadSafe> ViewExtension;
	TSharedPtr<SWidget> TweakPanelWidget;
	bool bTweakUIVisible = false;
};
