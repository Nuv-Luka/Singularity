#include "PlanetTerrain/LandableMoonActor.h"
#include "PlanetTerrain/PlanetTerrainComponent.h"
#include "PlanetTerrain/PlanetTerrainDebug.h"
#include "Pug/PugCore.h"

ALandableMoonActor::ALandableMoonActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Terrain = CreateDefaultSubobject<UPlanetTerrainComponent>(TEXT("PlanetTerrain"));
}

void ALandableMoonActor::BeginPlay()
{
	Super::BeginPlay();
}

void ALandableMoonActor::SetupMoon(const FRockyPlanetDescriptor& Descriptor, const FVector& WorldCenter, UMaterialInterface* SurfaceMat)
{
	MoonCenter = WorldCenter;
	OrbitalWorldCenter = WorldCenter;
	bInSurfaceFrame = false;
	SetActorLocation(WorldCenter);
	if (Terrain)
	{
		Terrain->SurfaceMaterial = SurfaceMat;
		Terrain->bShowDebugHud = true;
		Terrain->InitializeTerrain(Descriptor, WorldCenter);
	}
}

void ALandableMoonActor::EnterSurfaceFrame()
{
	OrbitalWorldCenter = MoonCenter;
	bInSurfaceFrame = false;
	if (Terrain)
	{
		Terrain->SetPlanetWorldCenter(MoonCenter);
		Terrain->RebuildForMode(false);
	}
	UE_LOG(LogTemp, Log, TEXT("EnterSurfaceFrame: NO-OP (continuous) center=%s R=%.0fuu"),
		*MoonCenter.ToCompactString(),
		Terrain ? Terrain->GetRenderRadiusUu() : 0.0);
}

void ALandableMoonActor::ExitSurfaceFrame()
{
	bInSurfaceFrame = false;
	MoonCenter = OrbitalWorldCenter;
	SetActorLocation(MoonCenter);
	if (Terrain)
	{
		Terrain->SetPlanetWorldCenter(MoonCenter);
		Terrain->RebuildForMode(false);
	}
	UE_LOG(LogTemp, Log, TEXT("ExitSurfaceFrame: NO-OP restore pose %s"), *MoonCenter.ToCompactString());
}

void ALandableMoonActor::UpdateCamera(const FVector& CameraWorld, const FVector& CameraForward, const FVector& CameraVelocity)
{
	if (!Terrain)
	{
		return;
	}
	MoonCenter = GetActorLocation();
	Terrain->SetPlanetWorldCenter(MoonCenter);

	// World-space AGL first (source of truth for HUD / LOD / near-clip).
	Terrain->RefreshAltitudeMetrics(CameraWorld);

	const double UuPerM = FMath::Max(1e-9, Terrain->GetUuPerMeter());
	const FVector RelUu = CameraWorld - MoonCenter;
	const FVector3d LocalM(RelUu.X / UuPerM, RelUu.Y / UuPerM, RelUu.Z / UuPerM);
	const FVector3d Fwd(CameraForward.X, CameraForward.Y, CameraForward.Z);
	const FVector3d Vel(
		CameraVelocity.X / UuPerM,
		CameraVelocity.Y / UuPerM,
		CameraVelocity.Z / UuPerM);
	Terrain->SetCameraPlanetLocal(LocalM, Fwd, Vel);
}

void ALandableMoonActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}
