#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "SpaceFlyPawn.generated.h"

UCLASS()
class SINGULARITY_API ASpaceFlyPawn : public ADefaultPawn
{
	GENERATED_BODY()

public:
	ASpaceFlyPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Singularity|Camera")
	float FlySpeedUuPerSec = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Singularity|Camera")
	float MinFlySpeedUuPerSec = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Singularity|Camera")
	float MaxFlySpeedUuPerSec = 50000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Singularity|Camera")
	float SpeedScrollFactor = 1.25f;

	/**
	 * When > 0, forces r.SetNearClipPlane (used near moons so AU-scale auto clip
	 * does not cull the body). Cleared when far from surfaces.
	 */
	void SetSurfaceNearClipOverride(float NearClipUu);

private:
	void AdjustSpeed(float WheelDelta);
	void SyncClipPlanes();
	void ApplyMovementSpeed();
	void ApplyVerticalInput();
	void ToggleAtmosphereTweakUI();
	void ToggleTerrainTweakUI();

	float SurfaceNearClipOverrideUu = 0.0f;
};
