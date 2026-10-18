#include "PlanetTerrainNextSpawn.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextComponent.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextMoonActor.h"
#include "PlanetTerrainNextSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

FVector FPlanetTerrainNextSpawn::ResolveCenter(UWorld* World, const FPlanetTerrainNextSpawnRequest& Request)
{
	const double RadiusUu = PlanetTerrainNext::MetersToUu(Request.Descriptor.RadiusMeters);

	FVector View = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	if (World)
	{
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			FRotator Rotation = FRotator::ZeroRotator;
			Controller->GetPlayerViewPoint(View, Rotation);
			Forward = Rotation.Vector();
		}
	}

	switch (Request.Placement)
	{
	case EPlanetTerrainNextSpawnPlacement::OneAuAlongX:
		return View + FVector(PlanetTerrainNext::AstronomicalUnitMeters * PlanetTerrainNext::CmPerMeter, 0.0, 0.0);
	case EPlanetTerrainNextSpawnPlacement::WorldOrigin:
		return FVector::ZeroVector;
	case EPlanetTerrainNextSpawnPlacement::Custom:
		return Request.CustomCenterUu;
	case EPlanetTerrainNextSpawnPlacement::InFrontOfCamera:
	default:
		return View + Forward * (RadiusUu * FMath::Max(1.0, Request.DistanceRadii));
	}
}

APlanetTerrainNextMoon* FPlanetTerrainNextSpawn::Spawn(UWorld* World, const FPlanetTerrainNextSpawnRequest& Request)
{
	if (!World)
	{
		return nullptr;
	}

	if (Request.bSelectNextRenderer)
	{
		PlanetTerrainNextSettings::SetCVar(TEXT("singularity.TerrainRenderer.Mode"), TEXT("1"));
	}

	const FVector Center = ResolveCenter(World, Request);
	const FTransform SpawnTransform(FRotator::ZeroRotator, Center, FVector::OneVector);

	// Deferred so the descriptor is in place before BeginPlay starts the terrain.
	APlanetTerrainNextMoon* Moon = World->SpawnActorDeferred<APlanetTerrainNextMoon>(
		APlanetTerrainNextMoon::StaticClass(),
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Moon)
	{
		return nullptr;
	}

	Moon->Descriptor = Request.Descriptor;
	Moon->bStaticShellOnly = Request.bStaticShellOnly;
	Moon->FinishSpawning(SpawnTransform);
	return Moon;
}

bool FPlanetTerrainNextSpawn::GetNearestSurfaceAltitude(UWorld* World, const FVector& WorldPointUu, double& OutAltitudeM)
{
	const APlanetTerrainNextMoon* Moon = FindNearest(World, WorldPointUu);
	if (!Moon)
	{
		return false;
	}
	OutAltitudeM = Moon->GetSurfaceAltitudeMeters(WorldPointUu);
	return true;
}

bool FPlanetTerrainNextSpawn::PlaceCameraAtAltitude(UWorld* World, double AltitudeM)
{
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	FVector View = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	Controller->GetPlayerViewPoint(View, ViewRotation);

	APlanetTerrainNextMoon* Moon = FindNearest(World, View);
	if (!Moon)
	{
		return false;
	}

	const FVector Center = Moon->GetActorLocation();
	FVector Up = (View - Center).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		Up = FVector::UpVector;
	}

	double SurfaceRadiusM = Moon->Descriptor.RadiusMeters;
	if (const UPlanetTerrainNextComponent* Terrain = Moon->GetTerrain())
	{
		if (const FPlanetTerrainNextHeightField* Field = Terrain->GetHeightField())
		{
			SurfaceRadiusM += Field->HeightAt(FVector3d(Up), 0.0);
		}
	}
	const FVector Location = Center + Up * ((SurfaceRadiusM + FMath::Max(AltitudeM, 0.5)) * PlanetTerrainNext::CmPerMeter);

	FVector Forward = FVector::VectorPlaneProject(ViewRotation.Vector(), Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::CrossProduct(Up, FMath::Abs(Up.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	}
	const double Pitch = FMath::DegreesToRadians(20.0);
	const FVector Look = (Forward * FMath::Cos(Pitch) - Up * FMath::Sin(Pitch)).GetSafeNormal();
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Look, Up).Rotator();

	Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
	{
		Movement->StopMovementImmediately();
	}
	Controller->SetControlRotation(Rotation);
	RequestFlySpeed(FMath::Max(AltitudeM * 0.5, 2.0) * PlanetTerrainNext::CmPerMeter);
	return true;
}

namespace
{
double GRequestedFlySpeedUuPerSec = -1.0;
}

void FPlanetTerrainNextSpawn::RequestFlySpeed(double SpeedUuPerSec)
{
	GRequestedFlySpeedUuPerSec = SpeedUuPerSec;
}

bool FPlanetTerrainNextSpawn::ConsumeRequestedFlySpeed(double& OutSpeedUuPerSec)
{
	if (GRequestedFlySpeedUuPerSec <= 0.0)
	{
		return false;
	}
	OutSpeedUuPerSec = GRequestedFlySpeedUuPerSec;
	GRequestedFlySpeedUuPerSec = -1.0;
	return true;
}

int32 FPlanetTerrainNextSpawn::DestroyAll(UWorld* World)
{
	if (!World)
	{
		return 0;
	}

	TArray<APlanetTerrainNextMoon*> Moons;
	for (TActorIterator<APlanetTerrainNextMoon> It(World); It; ++It)
	{
		Moons.Add(*It);
	}

	for (APlanetTerrainNextMoon* Moon : Moons)
	{
		if (IsValid(Moon))
		{
			Moon->Destroy();
		}
	}
	return Moons.Num();
}

APlanetTerrainNextMoon* FPlanetTerrainNextSpawn::FindNearest(UWorld* World, const FVector& FromUu)
{
	if (!World)
	{
		return nullptr;
	}

	APlanetTerrainNextMoon* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (TActorIterator<APlanetTerrainNextMoon> It(World); It; ++It)
	{
		APlanetTerrainNextMoon* Moon = *It;
		if (!IsValid(Moon))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Moon->GetActorLocation(), FromUu);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Moon;
		}
	}
	return Best;
}
