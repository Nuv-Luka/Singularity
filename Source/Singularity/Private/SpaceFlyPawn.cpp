#include "SpaceFlyPawn.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SphereComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Pug/PugCore.h"
#include "SingularityAtmosphereSubsystem.h"
#include "PlanetTerrainNextSpawn.h"
#include "PlanetTerrainNextUI.h"
#include "SingularityPlanetTerrainNextMode.h"

ASpaceFlyPawn::ASpaceFlyPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bAddDefaultMovementBindings = true;
	BaseTurnRate = 45.0f;
	BaseLookUpRate = 45.0f;
	FlySpeedUuPerSec = 400.0f;
	MinFlySpeedUuPerSec = 5.0f;
	MaxFlySpeedUuPerSec = 50000.0f;
	SetActorEnableCollision(false);

	if (USphereComponent* Sphere = Cast<USphereComponent>(GetCollisionComponent()))
	{
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		Sphere->SetGenerateOverlapEvents(false);
		Sphere->SetCanEverAffectNavigation(false);
	}
	if (UStaticMeshComponent* Mesh = GetMeshComponent())
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetVisibility(false);
	}
	if (UFloatingPawnMovement* Move = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Move->bConstrainToPlane = false;
		Move->SetPlaneConstraintEnabled(false);
		Move->bSnapToPlaneAtStart = false;
	}
}

void ASpaceFlyPawn::BeginPlay()
{
	Super::BeginPlay();
	SetActorEnableCollision(false);
	if (USphereComponent* Sphere = Cast<USphereComponent>(GetCollisionComponent()))
	{
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	}
	if (UFloatingPawnMovement* Move = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Move->bConstrainToPlane = false;
		Move->SetPlaneConstraintEnabled(false);
		Move->bSnapToPlaneAtStart = false;
	}
	ApplyMovementSpeed();
}

void ASpaceFlyPawn::SetupPlayerInputComponent(UInputComponent* InInputComponent)
{
	Super::SetupPlayerInputComponent(InInputComponent);
	// Explicit vertical bindings — Enhanced Input projects sometimes drop engine MoveUp maps.
	InInputComponent->BindAxis(TEXT("DefaultPawn_MoveUp"), this, &ADefaultPawn::MoveUp_World);
	// F8 is Unreal PIE eject/possess — use F7 for atmosphere tweaks, F6 for Terrain Next spawn.
	InInputComponent->BindKey(EKeys::F7, IE_Pressed, this, &ASpaceFlyPawn::ToggleAtmosphereTweakUI);
	InInputComponent->BindKey(EKeys::F6, IE_Pressed, this, &ASpaceFlyPawn::ToggleTerrainTweakUI);
}

void ASpaceFlyPawn::ToggleAtmosphereTweakUI()
{
	if (UWorld* World = GetWorld())
	{
		if (USingularityAtmosphereSubsystem* Atm = World->GetSubsystem<USingularityAtmosphereSubsystem>())
		{
			Atm->ToggleTweakUI();
		}
	}
}

void ASpaceFlyPawn::ToggleTerrainTweakUI()
{
	FPlanetTerrainNextUI::Toggle();
}

void ASpaceFlyPawn::AdjustSpeed(float WheelDelta)
{
	if (FMath::Abs(WheelDelta) < KINDA_SMALL_NUMBER)
	{
		return;
	}
	FlySpeedUuPerSec = FMath::Clamp(
		FlySpeedUuPerSec * FMath::Pow(SpeedScrollFactor, WheelDelta),
		MinFlySpeedUuPerSec,
		MaxFlySpeedUuPerSec);
	ApplyMovementSpeed();
}

void ASpaceFlyPawn::ApplyMovementSpeed()
{
	if (UFloatingPawnMovement* Move = Cast<UFloatingPawnMovement>(GetMovementComponent()))
	{
		Move->MaxSpeed = FlySpeedUuPerSec;
		Move->Acceleration = FlySpeedUuPerSec * 8.0f;
		Move->Deceleration = FlySpeedUuPerSec * 8.0f;
		Move->TurningBoost = 8.0f;
	}
}

void ASpaceFlyPawn::ApplyVerticalInput()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	float Up = 0.0f;
	if (PC->IsInputKeyDown(EKeys::SpaceBar) || PC->IsInputKeyDown(EKeys::E) || PC->IsInputKeyDown(EKeys::PageUp))
	{
		Up += 1.0f;
	}
	if (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::C) || PC->IsInputKeyDown(EKeys::Q) || PC->IsInputKeyDown(EKeys::PageDown))
	{
		Up -= 1.0f;
	}

	if (FMath::Abs(Up) > KINDA_SMALL_NUMBER)
	{
		// World Z for "up/down", plus a camera-relative lift so looking down still climbs.
		AddMovementInput(FVector::UpVector, Up);
		const FVector CamUp = FRotationMatrix(PC->GetControlRotation()).GetScaledAxis(EAxis::Z);
		AddMovementInput(CamUp, Up * 0.35f);
	}
}

void ASpaceFlyPawn::SetSurfaceNearClipOverride(float NearClipUu)
{
	SurfaceNearClipOverrideUu = NearClipUu;
}

void ASpaceFlyPawn::SyncClipPlanes()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	float NearClip;
	double TerrainNextAltitudeM = 0.0;
	if (SurfaceNearClipOverrideUu > 0.0f)
	{
		// Approaching / skimming a moon — keep a walkable/flyable near plane.
		NearClip = SurfaceNearClipOverrideUu;
	}
	else if (SingularityTerrainRendererIsNext()
		&& FPlanetTerrainNextSpawn::GetNearestSurfaceAltitude(GetWorld(), PC->PlayerCameraManager->GetCameraLocation(), TerrainNextAltitudeM)
		&& TerrainNextAltitudeM < 2.0e6)
	{
		// Near plane at 1% of altitude: never clips the surface below, still keeps depth precision.
		NearClip = static_cast<float>(FMath::Clamp(TerrainNextAltitudeM, 10.0, 1.0e6));
	}
	else
	{
		// Deep space: scale with distance, but never as aggressively as Dist*1e-5 at AU
		// (that was larger than a lunar radius and culled the body on approach).
		const double Dist = FMath::Max(PugEarthRadiiToUu(2.0), GetActorLocation().Size());
		NearClip = static_cast<float>(FMath::Clamp(Dist * 1.0e-7, 100.0, PugEarthRadiiToUu(0.05)));
	}
	PC->PlayerCameraManager->ViewTarget.POV.PerspectiveNearClipPlane = NearClip;
	PC->ConsoleCommand(FString::Printf(TEXT("r.SetNearClipPlane %.6f"), NearClip), true);
	if (UCameraComponent* Cam = FindComponentByClass<UCameraComponent>())
	{
		Cam->SetFieldOfView(90.0f);
	}
}

void ASpaceFlyPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) || PC->WasInputKeyJustPressed(EKeys::Add) || PC->WasInputKeyJustPressed(EKeys::Equals))
		{
			AdjustSpeed(1.0f);
		}
		if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) || PC->WasInputKeyJustPressed(EKeys::Subtract) || PC->WasInputKeyJustPressed(EKeys::Hyphen))
		{
			AdjustSpeed(-1.0f);
		}
	}
	double RequestedSpeed = 0.0;
	if (FPlanetTerrainNextSpawn::ConsumeRequestedFlySpeed(RequestedSpeed))
	{
		FlySpeedUuPerSec = FMath::Clamp(static_cast<float>(RequestedSpeed), MinFlySpeedUuPerSec, MaxFlySpeedUuPerSec);
	}
	ApplyVerticalInput();
	ApplyMovementSpeed();
	SyncClipPlanes();

	if (GEngine)
	{
		const double SpeedAu = static_cast<double>(FlySpeedUuPerSec) / PugAuToUu(1.0);
		GEngine->AddOnScreenDebugMessage(7711, 0.0f, FColor::Cyan,
			FString::Printf(TEXT("Fly  %.3g AU/s  |  F6 terrain Next  |  F7 atmosphere  |  scroll speed"), SpeedAu));
	}
}
