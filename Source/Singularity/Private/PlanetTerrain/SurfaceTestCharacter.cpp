#include "PlanetTerrain/SurfaceTestCharacter.h"
#include "PlanetTerrain/PlanetTerrainComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Pug/PugCore.h"

namespace
{
constexpr double G_SI = 6.67430e-11;
}

ASurfaceTestCharacter::ASurfaceTestCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = true;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = false;
		Move->JumpZVelocity = 420.0f;
		Move->AirControl = 0.15f;
		Move->MaxWalkSpeed = 450.0f;
		Move->GravityScale = 1.0f;
		Move->SetWalkableFloorAngle(70.0f);
		Move->PerchRadiusThreshold = 20.0f;
	}
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
}

void ASurfaceTestCharacter::BeginPlay()
{
	Super::BeginPlay();
}

void ASurfaceTestCharacter::BindTerrain(UPlanetTerrainComponent* Terrain, const FVector& InPlanetWorldCenter)
{
	BoundTerrain = Terrain;
	PlanetCenter = InPlanetWorldCenter;
	SurfaceSnapRemaining = SurfaceSnapSeconds;
	SyncSurfaceClipPlanes();
	AlignToGravity();
	SnapToSurface(40.0f);
}

void ASurfaceTestCharacter::SnapToSurface(float ExtraHoverUu)
{
	if (!BoundTerrain.IsValid())
	{
		return;
	}
	PlanetCenter = BoundTerrain->GetPlanetWorldCenter();
	const FVector Pos = GetActorLocation();
	FVector Outward = (Pos - PlanetCenter).GetSafeNormal();
	if (Outward.IsNearlyZero())
	{
		Outward = FVector::UpVector;
	}
	const float CapsuleHalf = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 96.0f;
	const FVector3d Dir(Outward.X, Outward.Y, Outward.Z);
	const FVector Target = BoundTerrain->GetSurfaceWorldLocation(Dir, CapsuleHalf + ExtraHoverUu);
	SetActorLocation(Target, false, nullptr, ETeleportType::ResetPhysics);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetGravityDirection(-Outward);
		Move->Velocity = FVector::ZeroVector;
	}
}

void ASurfaceTestCharacter::AlignToGravity()
{
	const FVector Pos = GetActorLocation();
	const FVector GravityDir = (PlanetCenter - Pos).GetSafeNormal();
	if (GravityDir.IsNearlyZero())
	{
		return;
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetGravityDirection(GravityDir);
	}
}

void ASurfaceTestCharacter::SyncSurfaceClipPlanes()
{
	// Near clip for walking on a true-scale body (cm units).
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	constexpr float NearClipUu = 20.0f;
	PC->PlayerCameraManager->ViewTarget.POV.PerspectiveNearClipPlane = NearClipUu;
	PC->ConsoleCommand(FString::Printf(TEXT("r.SetNearClipPlane %.6f"), NearClipUu), true);
}

void ASurfaceTestCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SyncSurfaceClipPlanes();
	if (!BoundTerrain.IsValid())
	{
		return;
	}

	PlanetCenter = BoundTerrain->GetPlanetWorldCenter();
	const FVector Pos = GetActorLocation();
	const FVector GravityDir = (PlanetCenter - Pos).GetSafeNormal();
	if (GravityDir.IsNearlyZero())
	{
		return;
	}

	const double UuPerM = FMath::Max(1e-6, BoundTerrain->GetUuPerMeter());
	const double DistM = FVector::Distance(Pos, PlanetCenter) / UuPerM;
	const double RadiusM = BoundTerrain->GetRadiusMeters();
	const FVector3d OutDir(-GravityDir.X, -GravityDir.Y, -GravityDir.Z);
	const double HeightM = BoundTerrain->SampleHeight(OutDir);
	const double SurfaceR = RadiusM + HeightM;
	const double AltitudeM = DistM - SurfaceR;

	if (AltitudeM < -1.0 || DistM < RadiusM * 0.9)
	{
		SnapToSurface(80.0f);
		SurfaceSnapRemaining = FMath::Max(SurfaceSnapRemaining, 1.5f);
	}
	else if (SurfaceSnapRemaining > 0.0f)
	{
		SurfaceSnapRemaining -= DeltaSeconds;
		const bool bGrounded = GetCharacterMovement() && GetCharacterMovement()->IsMovingOnGround();
		if (!bGrounded)
		{
			SnapToSurface(40.0f);
		}
	}

	const double Mass = BoundTerrain->GetDescriptor().MassKg;
	// True Newtonian g in uu/s² (cm/s²). Soft floor so CharacterMovement stays walkable.
	double GCms2 = (G_SI * Mass / FMath::Max(1.0, DistM * DistM)) * UuPerM;
	GCms2 = FMath::Clamp(GCms2, 80.0, static_cast<double>(MaxGravityCms2));

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetGravityDirection(GravityDir);
		const float Scale = SurfaceSnapRemaining > 0.0f ? 0.25f : 1.0f;
		Move->GravityScale = static_cast<float>((GCms2 / 980.0) * GravityScaleOverride * Scale);
	}
}

void ASurfaceTestCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ASurfaceTestCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ASurfaceTestCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ASurfaceTestCharacter::LookYaw);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ASurfaceTestCharacter::LookPitch);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
}

void ASurfaceTestCharacter::MoveForward(float Value)
{
	if (FMath::Abs(Value) < KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector GravityDir = (PlanetCenter - GetActorLocation()).GetSafeNormal();
	const FVector Up = -GravityDir;
	const FVector Forward = FVector::VectorPlaneProject(GetControlRotation().Vector(), Up).GetSafeNormal();
	AddMovementInput(Forward, Value);
}

void ASurfaceTestCharacter::MoveRight(float Value)
{
	if (FMath::Abs(Value) < KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector GravityDir = (PlanetCenter - GetActorLocation()).GetSafeNormal();
	const FVector Up = -GravityDir;
	const FVector Forward = FVector::VectorPlaneProject(GetControlRotation().Vector(), Up).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Forward, Up).GetSafeNormal();
	AddMovementInput(Right, Value);
}

void ASurfaceTestCharacter::LookYaw(float Value)
{
	AddControllerYawInput(Value);
}

void ASurfaceTestCharacter::LookPitch(float Value)
{
	AddControllerPitchInput(Value);
}
