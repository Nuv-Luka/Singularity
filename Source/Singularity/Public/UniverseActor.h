#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pug/PugCore.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"
#include "UniverseActor.generated.h"

class UDirectionalLightComponent;
class UStaticMeshComponent;
class ALandableMoonActor;
class ASurfaceTestCharacter;

UENUM()
enum class ESingularityViewMode : uint8
{
	Galaxy,
	System
};

USTRUCT()
struct FPlanetMarker
{
	GENERATED_BODY()

	FVector WorldLocation = FVector::ZeroVector;
	FKeplerElements Orbit;
	double BodyRadiusUu = 1.0;
	FString Caption;
	FLinearColor Color = FLinearColor::White;
	bool bHasAtmosphere = false;
	FPugAtmosphereDescriptor Atmosphere;
};

USTRUCT()
struct FGalaxyStarMarker
{
	GENERATED_BODY()

	FStarRecord Star;
	FVector GalaxyLocation = FVector::ZeroVector;
	FSectorCoord Sector;
};

UCLASS()
class SINGULARITY_API AUniverseActor : public AActor
{
	GENERATED_BODY()

public:
	AUniverseActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Singularity")
	int64 GalaxySeed = 0x5055475F44454D4Fll;

	UPROPERTY(EditAnywhere, Category = "Singularity")
	float GalaxyUuPerPc = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Singularity", meta = (ClampMin = "0", ClampMax = "5"))
	int32 GalaxyLoadRadius = 2;

	UPROPERTY(EditAnywhere, Category = "Singularity", meta = (ClampMin = "1", ClampMax = "16"))
	int32 GalaxyLoadBudget = 4;

private:
	void StripOpenWorldClutter();
	void StripForeignLightsAndBlockers();
	void BuildGalaxyView();
	void EnterSystem(int32 StarIndex);
	void ExitToGalaxy();
	void ClearSpawnedVisuals();
	void ClearSystemVisuals();
	void EnsureFreeFlightPawn() const;
	void UpdateInput();
	void SyncGalaxyStreaming(const FVector& CameraLocation);
	void LoadSectorVisuals(const FSectorCoord& Coord, const FGeneratedSector& Sector);
	void UnloadSectorVisuals(const FSectorCoord& Coord);
	void RebuildGalaxyStarIndex();
	void DrawDensityOverview(const FVector& CameraLocation);
	void DrawGalaxyOverlay(const FVector& CameraLocation);
	void DrawSystemOverlay(const FVector& CameraLocation);
	void UpdateStarLighting(const FVector& CameraLocation);
	void UpdateAtmosphereSnapshot(const FVector& CameraLocation, float DeltaSeconds);
	void ConfigureNoCull(UPrimitiveComponent* Primitive) const;
	void PlacePawn(const FVector& Location, const FVector& LookAt);
	void TryLandOnMoon();
	void TryEjectToFlight();
	void UpdateLandableMoons(const FVector& CameraLocation);
	void SpawnLandableMoon(const FRockyPlanetDescriptor& Desc, const FVector& WorldCenter);
	void TickLandingPrep(float DeltaSeconds);
	void CompleteLandingSpawn();
	void CancelLandingPrep(const TCHAR* Reason);
	FVector GalaxyPos(const double PositionPc[3]) const;
	FVector WorldToPc(const FVector& World) const;
	FVector SystemPosAu(const FVector& Au) const;
	int32 FindNearestGalaxyStar(const FVector& CameraLocation) const;
	FSectorCoord CameraSector(const FVector& CameraLocation) const;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> StarLight;

	UPROPERTY()
	TArray<TObjectPtr<UActorComponent>> SystemComponents;

	UPROPERTY()
	TArray<FPlanetMarker> Markers;

	UPROPERTY()
	TArray<FGalaxyStarMarker> GalaxyStars;

	TMap<FSectorCoord, FGeneratedSector> LoadedSectors;
	TMap<FSectorCoord, TArray<TObjectPtr<UActorComponent>>> SectorComponents;
	TSet<FSectorCoord> EmptySectors;
	TArray<FSectorCoord> PendingSectorLoads;
	FGalaxyProfile GalaxyProfile;
	FVector StarLocation = FVector::ZeroVector;
	FVector SavedGalaxyPawnLocation = FVector::ZeroVector;
	FRotator SavedGalaxyPawnRotation = FRotator::ZeroRotator;
	FSectorCoord StreamFocus{32, 0, 0};
	ESingularityViewMode ViewMode = ESingularityViewMode::Galaxy;
	int32 FocusStarIndex = INDEX_NONE;
	int32 InspectedGalaxyStar = INDEX_NONE;
	double FocusStarTeffK = 5772.0;
	double FocusStarLuminositySolar = 1.0;
	float AtmosphereFade = 0.0f;
	int32 ActiveAtmospherePlanet = INDEX_NONE;

	UPROPERTY()
	TArray<TObjectPtr<ALandableMoonActor>> LandableMoons;

	UPROPERTY()
	TObjectPtr<ASurfaceTestCharacter> SurfaceCharacter;

	/** Cached free-flight pawn so land/eject can repossess without losing the camera. */
	UPROPERTY()
	TObjectPtr<APawn> CachedFlyPawn;

	UPROPERTY()
	TObjectPtr<ALandableMoonActor> ActiveLandedMoon;

	bool bSurfaceMode = false;

	/** Async land: EnterSurfaceFrame + prep tiles, spawn walker when pad Resident. */
	bool bLandingPrepPending = false;
	float LandingPrepElapsed = 0.0f;
	float LandingPrepTimeout = 12.0f;
	FVector3d LandingPrepUpDir = FVector3d(0, 0, 1);
};
