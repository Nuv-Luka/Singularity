#include "PlanetTerrainNextMoonActor.h"

#include "PlanetTerrainNextComponent.h"
#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextSpawn.h"
#include "PlanetTerrainNextStaticShell.h"
#include "PlanetTerrainNextUI.h"
#include "SingularityPlanetTerrainNextMode.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

APlanetTerrainNextMoon::APlanetTerrainNextMoon()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("PlanetCenter"));
	SetRootComponent(Root);

	Shell = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("StaticShell"));
	Shell->SetupAttachment(Root);
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shell->SetCastShadow(true);
	Shell->SetBoundsScale(1.0f);
	Shell->bUseAsOccluder = true;
	Shell->SetCanEverAffectNavigation(false);

	Terrain = CreateDefaultSubobject<UPlanetTerrainNextComponent>(TEXT("Terrain"));
	Terrain->SetupAttachment(Root);
}

void APlanetTerrainNextMoon::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorScale3D(FVector::OneVector);
	RebuildStaticShell();
}

void APlanetTerrainNextMoon::BeginPlay()
{
	Super::BeginPlay();
	SetActorScale3D(FVector::OneVector);
	RebuildStaticShell();

	if (!bStaticShellOnly && Terrain)
	{
		Terrain->SetSuspendedProxy(Shell);
		// R10.1: while Next owns the draw, shell must not occlude patch pixels.
		if (Shell)
		{
			Shell->SetVisibility(false);
			Shell->bUseAsOccluder = false;
		}
		Terrain->InitializeTerrain(Descriptor);
	}
}

void APlanetTerrainNextMoon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Terrain)
	{
		Terrain->ShutdownTerrain();
	}
	Super::EndPlay(EndPlayReason);
}

double APlanetTerrainNextMoon::GetShellRadiusUu() const
{
	return PlanetTerrainNext::MetersToUu(Descriptor.RadiusMeters);
}

double APlanetTerrainNextMoon::GetSurfaceAltitudeMeters(const FVector& WorldPointUu) const
{
	if (Terrain && Terrain->IsTerrainActive())
	{
		return Terrain->GetAltitudeMeters(WorldPointUu);
	}
	return FVector::Dist(WorldPointUu, GetActorLocation()) / PlanetTerrainNext::CmPerMeter - Descriptor.RadiusMeters;
}

uint32 APlanetTerrainNextMoon::MakeBuildKey() const
{
	// Bump when shell topology / winding changes so old PIE actors rebuild.
	constexpr uint32 GeometryVersion = 2;
	const uint32 RadiusBits = static_cast<uint32>(GetTypeHash(Descriptor.RadiusMeters));
	return HashCombine(HashCombine(RadiusBits, GetTypeHash(Descriptor.CellsPerEdge)), GeometryVersion);
}

void APlanetTerrainNextMoon::RebuildStaticShell()
{
	if (!Shell)
	{
		return;
	}

	const uint32 Key = MakeBuildKey();
	if (Key == BuiltKey && Shell->GetNumSections() == 6)
	{
		return;
	}

	SetActorScale3D(FVector::OneVector);
	Shell->SetRelativeScale3D(FVector::OneVector);
	Shell->SetBoundsScale(1.0f);
	Shell->ClearAllMeshSections();

	UMaterialInterface* Surface = UMaterial::GetDefaultMaterial(MD_Surface);
	const FLinearColor Regolith(0.55f, 0.53f, 0.50f, 1.0f);

	for (int32 Face = 0; Face < 6; ++Face)
	{
		FPlanetTerrainNextFaceGeometry Geometry;
		BuildPlanetTerrainNextFaceMesh(Face, Descriptor.CellsPerEdge, Descriptor.RadiusMeters, Geometry);

		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;
		Colors.Reserve(Geometry.Normals.Num());
		Tangents.Reserve(Geometry.Normals.Num());
		for (const FVector& Normal : Geometry.Normals)
		{
			Colors.Add(Regolith);
			FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal);
			if (Tangent.SizeSquared() < 1.0e-8)
			{
				Tangent = FVector::CrossProduct(FVector::ForwardVector, Normal);
			}
			Tangent.Normalize();
			Tangents.Add(FProcMeshTangent(Tangent, false));
		}

		Shell->CreateMeshSection_LinearColor(
			Face,
			Geometry.VerticesCm,
			Geometry.Triangles,
			Geometry.Normals,
			Geometry.UVs,
			Colors,
			Tangents,
			false);
		Shell->SetMeshSectionVisible(Face, true);
		if (Surface)
		{
			Shell->SetMaterial(Face, Surface);
		}
	}

	BuiltKey = Key;
	Shell->SetBoundsScale(1.0f);

	UE_LOG(LogTemp, Log,
		TEXT("PlanetTerrainNext R1 shell planet=%lld R=%.3f km (%.0f uu) faces=6 cells=%d scale=1 boundsScale=1"),
		Descriptor.PlanetId,
		Descriptor.RadiusMeters / 1000.0,
		GetShellRadiusUu(),
		FMath::Clamp(Descriptor.CellsPerEdge, 1, 64));
}

namespace
{
void SpawnPlanetTerrainNextStaticMoon(const TArray<FString>& Args, UWorld* World)
{
	FPlanetTerrainNextSpawnRequest Request;
	if (Args.Num() == 1 && Args[0].Equals(TEXT("Far"), ESearchCase::IgnoreCase))
	{
		Request.Placement = EPlanetTerrainNextSpawnPlacement::OneAuAlongX;
	}
	else if (Args.Num() >= 3)
	{
		Request.Placement = EPlanetTerrainNextSpawnPlacement::Custom;
		Request.CustomCenterUu = FVector(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
	}
	else
	{
		Request.Placement = EPlanetTerrainNextSpawnPlacement::InFrontOfCamera;
		Request.DistanceRadii = 3.0;
	}

	APlanetTerrainNextMoon* Moon = FPlanetTerrainNextSpawn::Spawn(World, Request);
	if (!Moon)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlanetTerrainNext spawn failed."));
		return;
	}

	const FVector Center = Moon->GetActorLocation();
	UE_LOG(LogTemp, Log,
		TEXT("PlanetTerrainNext static moon at (%.0f, %.0f, %.0f) uu. Renderer mode=%s. This actor does not move orbits."),
		Center.X, Center.Y, Center.Z,
		SingularityTerrainRendererIsNext() ? TEXT("Next") : TEXT("Legacy"));
}

static FAutoConsoleCommandWithWorldAndArgs GSpawnPlanetTerrainNextStaticMoon(
	TEXT("singularity.TerrainNext.SpawnStaticMoon"),
	TEXT("R1 preview. No args: moon center 3 radii in front of the camera. 'Far': center is 1 AU from the camera along +X. Three numbers: center in centimeters. Prefer F6 / singularity.TerrainNext.ToggleUI."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnPlanetTerrainNextStaticMoon));

static FAutoConsoleCommand GTogglePlanetTerrainNextUI(
	TEXT("singularity.TerrainNext.ToggleUI"),
	TEXT("Toggle the Planet Terrain Next spawn panel (also F6 in PIE)."),
	FConsoleCommandDelegate::CreateStatic(&FPlanetTerrainNextUI::Toggle));
}
