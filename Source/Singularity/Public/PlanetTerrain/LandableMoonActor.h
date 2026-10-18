#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"
#include "LandableMoonActor.generated.h"

class UPlanetTerrainComponent;
class USceneComponent;

UCLASS()
class SINGULARITY_API ALandableMoonActor : public AActor
{
	GENERATED_BODY()

public:
	ALandableMoonActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void SetupMoon(const FRockyPlanetDescriptor& Descriptor, const FVector& WorldCenter, UMaterialInterface* SurfaceMat);

	UPlanetTerrainComponent* GetTerrain() const { return Terrain; }
	FVector GetMoonCenter() const { return MoonCenter; }
	FVector GetOrbitalCenter() const { return bInSurfaceFrame ? OrbitalWorldCenter : MoonCenter; }
	bool IsInSurfaceFrame() const { return bInSurfaceFrame; }

	/** Legacy — no-op under continuous traversal (moon keeps orbital pose). */
	void EnterSurfaceFrame();
	/** Legacy — no-op under continuous traversal. */
	void ExitSurfaceFrame();

	/** Call each frame with camera world position. */
	void UpdateCamera(const FVector& CameraWorld, const FVector& CameraForward, const FVector& CameraVelocity);

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPlanetTerrainComponent> Terrain;

	FVector MoonCenter = FVector::ZeroVector;
	FVector OrbitalWorldCenter = FVector::ZeroVector;
	bool bInSurfaceFrame = false;
};
