#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SurfaceTestCharacter.generated.h"

class UPlanetTerrainComponent;

/**
 * Walkable test character with planet-centric gravity for airless moon surfaces.
 */
UCLASS()
class SINGULARITY_API ASurfaceTestCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASurfaceTestCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void BindTerrain(UPlanetTerrainComponent* Terrain, const FVector& PlanetWorldCenter);
	void AlignToGravity();
	/** Teleport feet onto analytic surface (plus capsule clearance). */
	void SnapToSurface(float ExtraHoverUu = 0.0f);
	/** Reset near/far clip for play-scale surface (orbital values cull the moon). */
	void SyncSurfaceClipPlanes();

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	float GravityScaleOverride = 1.0f;

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	float MaxGravityCms2 = 2000.0f;

	/** Keep snapping to the height field until grounded or this expires. */
	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	float SurfaceSnapSeconds = 2.5f;

protected:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);

	TWeakObjectPtr<UPlanetTerrainComponent> BoundTerrain;
	FVector PlanetCenter = FVector::ZeroVector;
	float SurfaceSnapRemaining = 0.0f;
};
