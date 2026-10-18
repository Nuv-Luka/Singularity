#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetTerrainNextTypes.h"
#include "PlanetTerrainNextMoonActor.generated.h"

class UProceduralMeshComponent;
class UPlanetTerrainNextComponent;

/**
 * One true-scale airless moon.
 *
 * Editor / Legacy mode: the R1 six-face static shell.
 * Play in Next mode: the R2 quadtree terrain (UPlanetTerrainNextComponent); the shell is hidden.
 * Does not participate in orbital integration; place it at the moon's world center.
 */
UCLASS(Blueprintable)
class SINGULARITYPLANETTERRAINNEXT_API APlanetTerrainNextMoon : public AActor
{
	GENERATED_BODY()

public:
	APlanetTerrainNextMoon();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "PlanetTerrainNext")
	void RebuildStaticShell();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlanetTerrainNext")
	FPlanetTerrainNextDescriptor Descriptor;

	/** Keep the R1 static shell in play instead of starting the quadtree terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlanetTerrainNext")
	bool bStaticShellOnly = false;

	double GetShellRadiusUu() const;

	/** Meters above the full-detail surface (or above the base sphere when terrain is not running). */
	double GetSurfaceAltitudeMeters(const FVector& WorldPointUu) const;

	UPlanetTerrainNextComponent* GetTerrain() const { return Terrain; }

private:
	UPROPERTY(VisibleAnywhere, Category = "PlanetTerrainNext")
	TObjectPtr<USceneComponent> Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "PlanetTerrainNext")
	TObjectPtr<UProceduralMeshComponent> Shell = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "PlanetTerrainNext")
	TObjectPtr<UPlanetTerrainNextComponent> Terrain = nullptr;

	uint32 BuiltKey = 0;

	uint32 MakeBuildKey() const;
};
