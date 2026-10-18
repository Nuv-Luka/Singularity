#include "UniverseActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Pug/PugCore.h"
#include "Singularity.h"
#include "SingularityAtmosphereSubsystem.h"
#include "SingularityAtmosphereTypes.h"
#include "SingularityAtmosphereSettings.h"
#include "HAL/IConsoleManager.h"
#include "SpaceFlyPawn.h"
#include "PlanetTerrain/LandableMoonActor.h"
#include "PlanetTerrain/SurfaceTestCharacter.h"
#include "PlanetTerrain/PlanetTerrainComponent.h"
#include "PlanetTerrain/PlanetTerrainDebug.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"

AUniverseActor::AUniverseActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AUniverseActor::ConfigureNoCull(UPrimitiveComponent* Primitive) const
{
	if (!Primitive)
	{
		return;
	}
	Primitive->SetCullDistance(0.0f);
	Primitive->bAllowCullDistanceVolume = false;
	Primitive->SetBoundsScale(100000.0f);
	Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Primitive->SetCastShadow(false);
	Primitive->bVisibleInReflectionCaptures = true;
	Primitive->bReceivesDecals = false;
	Primitive->SetRenderCustomDepth(false);
	Primitive->bUseAsOccluder = false;
}

void AUniverseActor::BeginPlay()
{
	Super::BeginPlay();
	VertexColorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorViewMode_ColorOnly.VertexColorViewMode_ColorOnly"));
	ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	GalaxyProfile = FGalaxyProfile{};
	StripOpenWorldClutter();
	BuildGalaxyView();
}

void AUniverseActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

namespace
{
bool UsesTiles(EPlanetClass Class)
{
	return Class == EPlanetClass::Rocky || Class == EPlanetClass::IceWorld || Class == EPlanetClass::Dwarf;
}

FLinearColor GiantColor(EPlanetClass Class)
{
	return Class == EPlanetClass::IceGiant
		? FLinearColor(0.55f, 0.78f, 0.92f)
		: FLinearColor(0.78f, 0.62f, 0.38f);
}

FLinearColor MarkerColor(EPlanetClass Class)
{
	switch (Class)
	{
	case EPlanetClass::Rocky: return FLinearColor(0.35f, 0.9f, 0.45f);
	case EPlanetClass::IceWorld: return FLinearColor(0.55f, 0.85f, 1.0f);
	case EPlanetClass::IceGiant: return FLinearColor(0.35f, 0.7f, 1.0f);
	case EPlanetClass::GasGiant: return FLinearColor(1.0f, 0.75f, 0.35f);
	case EPlanetClass::Dwarf:
	default: return FLinearColor(0.85f, 0.85f, 0.9f);
	}
}

FVector CubeFaceDirection(EPugCubeFace Face, double U, double V)
{
	FVector Direction;
	switch (Face)
	{
	case EPugCubeFace::PosX: Direction = FVector(1, V, -U); break;
	case EPugCubeFace::NegX: Direction = FVector(-1, V, U); break;
	case EPugCubeFace::PosY: Direction = FVector(U, 1, V); break;
	case EPugCubeFace::NegY: Direction = FVector(U, -1, -V); break;
	case EPugCubeFace::PosZ: Direction = FVector(U, V, 1); break;
	case EPugCubeFace::NegZ:
	default: Direction = FVector(-U, V, -1); break;
	}
	Direction.Normalize();
	return Direction;
}

/** Build all 6 cube-sphere faces as one section so a face cannot vanish from per-section culling. */
void BuildCubeSphere(
	UProceduralMeshComponent* Mesh,
	uint64 PlanetId,
	EPlanetClass Class,
	double EquilibriumTempK,
	double RadiusUu,
	UMaterialInterface* Material)
{
	constexpr int32 N = 16;
	constexpr int32 Row = N + 1;
	constexpr int32 FaceVerts = Row * Row;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Vertices.Reserve(FaceVerts * 6);
	Triangles.Reserve(N * N * 6 * 6);
	Normals.Reserve(FaceVerts * 6);
	UVs.Reserve(FaceVerts * 6);
	Colors.Reserve(FaceVerts * 6);
	Tangents.Reserve(FaceVerts * 6);

	for (uint8 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
	{
		const EPugCubeFace Face = static_cast<EPugCubeFace>(FaceIndex);
		const int32 Base = Vertices.Num();

		for (int32 J = 0; J <= N; ++J)
		{
			for (int32 I = 0; I <= N; ++I)
			{
				const double U = (static_cast<double>(I) / N) * 2.0 - 1.0;
				const double V = (static_cast<double>(J) / N) * 2.0 - 1.0;
				const FVector Direction = CubeFaceDirection(Face, U, V);
				Vertices.Add(Direction * RadiusUu);
				Normals.Add(Direction);
				UVs.Add(FVector2D(static_cast<float>(I) / N, static_cast<float>(J) / N));
				Colors.Add(PugBakeTileAlbedo(PlanetId, Class, EquilibriumTempK, Face, U, V));
				Tangents.Add(FProcMeshTangent(1, 0, 0));
			}
		}

		// UE culls CW → need CCW from outside → RH edge cross must point outward.
		const FVector& P0 = Vertices[Base];
		const FVector& P1 = Vertices[Base + 1];
		const FVector& PRow = Vertices[Base + Row];
		const bool bDefaultOutward =
			FVector::DotProduct(FVector::CrossProduct(P1 - P0, PRow - P0), P0) > 0.0;

		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const int32 A = Base + J * Row + I;
				if (bDefaultOutward)
				{
					Triangles.Append({A, A + 1, A + Row, A + 1, A + Row + 1, A + Row});
				}
				else
				{
					Triangles.Append({A, A + Row, A + 1, A + 1, A + Row, A + Row + 1});
				}
			}
		}
	}

	Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false, true);
	Mesh->SetMeshSectionVisible(0, true);
	if (Material)
	{
		Mesh->SetMaterial(0, Material);
	}
}

UStaticMeshComponent* SpawnSphereTracked(
	AActor* Owner,
	USceneComponent* Parent,
	UStaticMesh* Mesh,
	UMaterialInterface* Material,
	const FVector& Location,
	double RadiusUu,
	const FLinearColor& Color,
	TArray<TObjectPtr<UActorComponent>>& Tracked,
	TFunctionRef<void(UPrimitiveComponent*)> NoCull)
{
	UStaticMeshComponent* Sphere = NewObject<UStaticMeshComponent>(Owner);
	Sphere->SetStaticMesh(Mesh);
	Sphere->SetupAttachment(Parent);
	Sphere->SetWorldLocation(Location);
	Sphere->SetWorldScale3D(FVector(PugSphereMeshScale(FMath::Max(1.0, RadiusUu))));
	Sphere->RegisterComponent();
	NoCull(Sphere);
	Tracked.Add(Sphere);
	if (Material && Mesh)
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, Sphere);
		Instance->SetVectorParameterValue(TEXT("Color"), Color);
		Sphere->SetMaterial(0, Instance);
	}
	return Sphere;
}


FLinearColor AverageTileAlbedo(uint64 PlanetId, EPlanetClass Class, double TeqK)
{
	FLinearColor Sum(0, 0, 0, 0);
	int32 Count = 0;
	for (uint8 Face = 0; Face < 6; ++Face)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				const double U = -0.75 + I * 0.75;
				const double V = -0.75 + J * 0.75;
				Sum += PugBakeTileAlbedo(PlanetId, Class, TeqK, static_cast<EPugCubeFace>(Face), U, V);
				++Count;
			}
		}
	}
	return Count > 0 ? Sum * (1.0f / Count) : FLinearColor(0.4f, 0.4f, 0.4f);
}
}

void AUniverseActor::EnsureFreeFlightPawn() const
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC)
	{
		return;
	}
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorEnableCollision(false);
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Pawn);
		for (UPrimitiveComponent* Prim : Primitives)
		{
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Prim->SetCollisionResponseToAllChannels(ECR_Ignore);
			Prim->SetGenerateOverlapEvents(false);
		}
		if (UFloatingPawnMovement* Move = Cast<UFloatingPawnMovement>(Pawn->GetMovementComponent()))
		{
			Move->bConstrainToPlane = false;
			Move->SetPlaneConstraintEnabled(false);
			Move->bSnapToPlaneAtStart = false;
		}
	}
}

void AUniverseActor::StripForeignLightsAndBlockers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> ToDestroy;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == this)
		{
			continue;
		}

		const FString ClassName = Actor->GetClass()->GetName();
		const FString ActorName = Actor->GetName();
		const FString Combined = ClassName + TEXT(" ") + ActorName;

		// Never strip gameplay actors used by free-flight / landable terrain.
		if (Actor->IsA(ASpaceFlyPawn::StaticClass())
			|| Actor->IsA(ALandableMoonActor::StaticClass())
			|| Actor->IsA(ASurfaceTestCharacter::StaticClass())
			|| Actor->IsA(APlayerController::StaticClass())
			|| Actor->IsA(APawn::StaticClass()))
		{
			continue;
		}

		if (Actor->IsA(ADirectionalLight::StaticClass())
			|| Actor->IsA(ASkyLight::StaticClass())
			|| Actor->IsA(AExponentialHeightFog::StaticClass())
			|| Combined.Contains(TEXT("Landscape"))
			|| Combined.Contains(TEXT("SkyAtmosphere"))
			|| Combined.Contains(TEXT("VolumetricCloud"))
			|| Combined.Contains(TEXT("SkySphere"))
			|| Combined.Contains(TEXT("SkyDome"))
			|| Combined.Contains(TEXT("BP_Sky"))
			|| Combined.Contains(TEXT("Ultra_Dynamic"))
			|| Combined.Contains(TEXT("Floor"))
			|| Combined.Contains(TEXT("Plane"))
			|| Combined.Contains(TEXT("Ground"))
			|| Combined.Contains(TEXT("Template"))
			|| Combined.Contains(TEXT("BlockingVolume"))
			|| Combined.Contains(TEXT("KillZ")))
		{
			ToDestroy.Add(Actor);
			continue;
		}

		if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
		{
			// Template_Default ships a ground plane — remove all static mesh actors from the map.
			ToDestroy.Add(MeshActor);
			continue;
		}

		TInlineComponentArray<UActorComponent*> Components(Actor);
		for (UActorComponent* Component : Components)
		{
			if (Component == StarLight)
			{
				continue;
			}
			if (UDirectionalLightComponent* Dir = Cast<UDirectionalLightComponent>(Component))
			{
				Dir->SetVisibility(false);
				Dir->SetIntensity(0.0f);
				Dir->DestroyComponent();
			}
			else if (Cast<USkyLightComponent>(Component) || Cast<UExponentialHeightFogComponent>(Component)
				|| Cast<USkyAtmosphereComponent>(Component))
			{
				Component->DestroyComponent();
			}
			else if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Component))
			{
				// Kill leftover collision so free-flight cannot catch on map geometry.
				if (Actor != this)
				{
					Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Prim->SetCollisionResponseToAllChannels(ECR_Ignore);
				}
			}
		}
	}

	for (AActor* Actor : ToDestroy)
	{
		if (IsValid(Actor) && Actor != this)
		{
			Actor->Destroy();
		}
	}

	if (AWorldSettings* Settings = World->GetWorldSettings())
	{
		Settings->LightmassSettings.EnvironmentIntensity = 0.0f;
		Settings->bEnableWorldBoundsChecks = false;
		Settings->KillZ = -HALF_WORLD_MAX;
	}
}

void AUniverseActor::StripOpenWorldClutter()
{
	StripForeignLightsAndBlockers();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (GEngine)
	{
		GEngine->Exec(World, TEXT("r.SkyAtmosphere 0"));
		GEngine->Exec(World, TEXT("r.VolumetricCloud 0"));
		GEngine->Exec(World, TEXT("r.Fog 0"));
		GEngine->Exec(World, TEXT("r.DefaultFeature.AmbientOcclusion 0"));
		GEngine->Exec(World, TEXT("ShowFlag.Atmosphere 0"));
		GEngine->Exec(World, TEXT("ShowFlag.Fog 0"));
		GEngine->Exec(World, TEXT("ShowFlag.Cloud 0"));
		GEngine->Exec(World, TEXT("ShowFlag.AmbientCubemap 0"));
	}
}

FVector AUniverseActor::GalaxyPos(const double PositionPc[3]) const
{
	return GetActorLocation() + FVector(
		PositionPc[0] * GalaxyUuPerPc,
		PositionPc[1] * GalaxyUuPerPc,
		PositionPc[2] * GalaxyUuPerPc);
}

FVector AUniverseActor::WorldToPc(const FVector& World) const
{
	const FVector Local = World - GetActorLocation();
	const double Scale = FMath::Max(1e-6, static_cast<double>(GalaxyUuPerPc));
	return FVector(Local.X / Scale, Local.Y / Scale, Local.Z / Scale);
}

FVector AUniverseActor::SystemPosAu(const FVector& Au) const
{
	return FVector(PugAuToUu(Au.X), PugAuToUu(Au.Y), PugAuToUu(Au.Z));
}

FSectorCoord AUniverseActor::CameraSector(const FVector& CameraLocation) const
{
	const FVector Pc = WorldToPc(CameraLocation);
	const double P[3] = {Pc.X, Pc.Y, Pc.Z};
	return FSectorCoord::FromPositionPc(P, GalaxyProfile.SectorSizePc);
}

void AUniverseActor::PlacePawn(const FVector& Location, const FVector& LookAt)
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		const FVector Dir = (LookAt - Location).GetSafeNormal();
		const FRotator Rot = Dir.IsNearlyZero() ? FRotator::ZeroRotator : Dir.Rotation();
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorLocation(Location);
			PC->SetControlRotation(Rot);
		}
		if (ASpaceFlyPawn* Fly = Cast<ASpaceFlyPawn>(PC->GetPawn()))
		{
			Fly->FlySpeedUuPerSec = ViewMode == ESingularityViewMode::Galaxy
				? 800.0f
				: static_cast<float>(PugEarthRadiiToUu(2.0));
			Fly->MinFlySpeedUuPerSec = ViewMode == ESingularityViewMode::Galaxy ? 5.0f : static_cast<float>(PugMToUu(1.0));
			Fly->MaxFlySpeedUuPerSec = ViewMode == ESingularityViewMode::Galaxy
				? 200000.0f
				: static_cast<float>(PugAuToUu(50.0));
		}
	}
}

void AUniverseActor::ClearSystemVisuals()
{
	FlushPersistentDebugLines(GetWorld());
	if (UWorld* World = GetWorld())
	{
		if (USingularityAtmosphereSubsystem* Atm = World->GetSubsystem<USingularityAtmosphereSubsystem>())
		{
			Atm->ClearAtmospheres();
		}
	}
	AtmosphereFade = 0.0f;
	ActiveAtmospherePlanet = INDEX_NONE;
	for (UActorComponent* Component : SystemComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	SystemComponents.Reset();
	Markers.Reset();
	StarLight = nullptr;
	if (bSurfaceMode)
	{
		TryEjectToFlight();
	}
	for (ALandableMoonActor* Moon : LandableMoons)
	{
		if (IsValid(Moon))
		{
			Moon->Destroy();
		}
	}
	LandableMoons.Reset();
	ActiveLandedMoon = nullptr;
	FPlanetTerrainDebug::ActiveTerrain = nullptr;
}

void AUniverseActor::ClearSpawnedVisuals()
{
	ClearSystemVisuals();
	for (auto& Pair : SectorComponents)
	{
		for (UActorComponent* Component : Pair.Value)
		{
			if (IsValid(Component))
			{
				Component->DestroyComponent();
			}
		}
	}
	SectorComponents.Reset();
	LoadedSectors.Reset();
	EmptySectors.Reset();
	PendingSectorLoads.Reset();
	GalaxyStars.Reset();
}

void AUniverseActor::UnloadSectorVisuals(const FSectorCoord& Coord)
{
	if (TArray<TObjectPtr<UActorComponent>>* Components = SectorComponents.Find(Coord))
	{
		for (UActorComponent* Component : *Components)
		{
			if (IsValid(Component))
			{
				Component->DestroyComponent();
			}
		}
		SectorComponents.Remove(Coord);
	}
	LoadedSectors.Remove(Coord);
}

void AUniverseActor::LoadSectorVisuals(const FSectorCoord& Coord, const FGeneratedSector& Sector)
{
	if (LoadedSectors.Contains(Coord) || EmptySectors.Contains(Coord))
	{
		return;
	}

	if (Sector.Stars.Num() == 0)
	{
		EmptySectors.Add(Coord);
		return;
	}

	LoadedSectors.Add(Coord, Sector);
	TArray<TObjectPtr<UActorComponent>>& Tracked = SectorComponents.FindOrAdd(Coord);
	auto NoCull = [this](UPrimitiveComponent* P) { ConfigureNoCull(P); };

	for (const FStarRecord& Star : Sector.Stars)
	{
		const FVector Location = GalaxyPos(Star.PositionPc);
		const double RadiusUu = FMath::Clamp(2.0 + Star.Stellar.RadiusSolar * 4.0, 1.5, 18.0);
		SpawnSphereTracked(this, Root, SphereMesh, ShapeMaterial, Location, RadiusUu, PugStarColor(Star.Stellar.TeffK), Tracked, NoCull);
	}
}

void AUniverseActor::RebuildGalaxyStarIndex()
{
	GalaxyStars.Reset();
	double Best = TNumericLimits<double>::Max();
	FocusStarIndex = INDEX_NONE;
	const FSectorCoord Home{32, 0, 0};
	const double HomeCenter[3] = {
		(Home.X + 0.5) * GalaxyProfile.SectorSizePc,
		(Home.Y + 0.5) * GalaxyProfile.SectorSizePc,
		(Home.Z + 0.5) * GalaxyProfile.SectorSizePc
	};

	for (const auto& Pair : LoadedSectors)
	{
		for (const FStarRecord& Star : Pair.Value.Stars)
		{
			FGalaxyStarMarker Marker;
			Marker.Star = Star;
			Marker.GalaxyLocation = GalaxyPos(Star.PositionPc);
			Marker.Sector = Pair.Key;
			const int32 Index = GalaxyStars.Add(Marker);

			const double Dx = Star.PositionPc[0] - HomeCenter[0];
			const double Dy = Star.PositionPc[1] - HomeCenter[1];
			const double Dz = Star.PositionPc[2] - HomeCenter[2];
			const double Dist2 = Dx * Dx + Dy * Dy + Dz * Dz;
			if (Dist2 < Best)
			{
				Best = Dist2;
				FocusStarIndex = Index;
			}
		}
	}
	InspectedGalaxyStar = FocusStarIndex;
}

void AUniverseActor::SyncGalaxyStreaming(const FVector& CameraLocation)
{
	StreamFocus = CameraSector(CameraLocation);
	const int32 Radius = FMath::Clamp(GalaxyLoadRadius, 0, 5);

	TSet<FSectorCoord> Desired;
	for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
	{
		for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
		{
			for (int32 Dz = -Radius; Dz <= Radius; ++Dz)
			{
				FSectorCoord Coord;
				Coord.X = static_cast<int16>(StreamFocus.X + Dx);
				Coord.Y = static_cast<int16>(StreamFocus.Y + Dy);
				Coord.Z = static_cast<int16>(StreamFocus.Z + Dz);
				Desired.Add(Coord);
				if (!LoadedSectors.Contains(Coord) && !EmptySectors.Contains(Coord) && !PendingSectorLoads.Contains(Coord))
				{
					PendingSectorLoads.Add(Coord);
				}
			}
		}
	}

	// Unload sectors that left the focus window.
	TArray<FSectorCoord> ToUnload;
	for (const auto& Pair : LoadedSectors)
	{
		if (!Desired.Contains(Pair.Key))
		{
			ToUnload.Add(Pair.Key);
		}
	}
	bool bChanged = ToUnload.Num() > 0;
	for (const FSectorCoord& Coord : ToUnload)
	{
		UnloadSectorVisuals(Coord);
	}

	PendingSectorLoads.RemoveAll([&Desired](const FSectorCoord& Coord)
	{
		return !Desired.Contains(Coord);
	});

	// Prefer nearer pending sectors.
	PendingSectorLoads.Sort([this](const FSectorCoord& A, const FSectorCoord& B)
	{
		const auto Dist = [this](const FSectorCoord& C)
		{
			const int64 Dx = static_cast<int64>(C.X) - StreamFocus.X;
			const int64 Dy = static_cast<int64>(C.Y) - StreamFocus.Y;
			const int64 Dz = static_cast<int64>(C.Z) - StreamFocus.Z;
			return Dx * Dx + Dy * Dy + Dz * Dz;
		};
		return Dist(A) < Dist(B);
	});

	const int32 Budget = FMath::Clamp(GalaxyLoadBudget, 1, 16);
	int32 LoadedThisFrame = 0;
	while (LoadedThisFrame < Budget && PendingSectorLoads.Num() > 0)
	{
		const FSectorCoord Coord = PendingSectorLoads[0];
		PendingSectorLoads.RemoveAt(0);
		if (LoadedSectors.Contains(Coord) || EmptySectors.Contains(Coord))
		{
			continue;
		}
		const FGeneratedSector Sector = PugGenerateSector(static_cast<uint64>(GalaxySeed), 0, Coord, GalaxyProfile);
		const int32 Before = LoadedSectors.Num();
		LoadSectorVisuals(Coord, Sector);
		++LoadedThisFrame;
		bChanged = bChanged || LoadedSectors.Num() != Before;
	}

	if (bChanged)
	{
		RebuildGalaxyStarIndex();
	}
}

void AUniverseActor::BuildGalaxyView()
{
	ClearSpawnedVisuals();
	ViewMode = ESingularityViewMode::Galaxy;

	// Seed the solar-neighborhood window immediately so Play isn't empty.
	StreamFocus = FSectorCoord{32, 0, 0};
	const int32 Radius = FMath::Clamp(GalaxyLoadRadius, 0, 5);
	for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
	{
		for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
		{
			for (int32 Dz = -Radius; Dz <= Radius; ++Dz)
			{
				FSectorCoord Coord;
				Coord.X = static_cast<int16>(32 + Dx);
				Coord.Y = static_cast<int16>(Dy);
				Coord.Z = static_cast<int16>(Dz);
				PendingSectorLoads.Add(Coord);
			}
		}
	}

	// Load a full budget burst on enter so several sectors appear at once.
	const int32 Burst = FMath::Min(PendingSectorLoads.Num(), FMath::Max(GalaxyLoadBudget * 4, 27));
	for (int32 I = 0; I < Burst; ++I)
	{
		const FSectorCoord Coord = PendingSectorLoads[0];
		PendingSectorLoads.RemoveAt(0);
		const FGeneratedSector Sector = PugGenerateSector(static_cast<uint64>(GalaxySeed), 0, Coord, GalaxyProfile);
		LoadSectorVisuals(Coord, Sector);
	}
	RebuildGalaxyStarIndex();

	const double HomeCenterPc[3] = {
		32.5 * GalaxyProfile.SectorSizePc,
		0.5 * GalaxyProfile.SectorSizePc,
		0.5 * GalaxyProfile.SectorSizePc
	};
	FVector FocusLoc = GalaxyPos(HomeCenterPc);
	if (GalaxyStars.IsValidIndex(FocusStarIndex))
	{
		FocusLoc = GalaxyStars[FocusStarIndex].GalaxyLocation;
	}
	const FVector Cam = FocusLoc + FVector(0.0, -600.0, 220.0);
	SavedGalaxyPawnLocation = Cam;
	SavedGalaxyPawnRotation = (FocusLoc - Cam).Rotation();
	PlacePawn(Cam, FocusLoc);

	UE_LOG(LogSingularity, Log, TEXT("Galaxy view: %d sectors loaded, %d stars, %d pending. Fly to stream more."),
		LoadedSectors.Num(), GalaxyStars.Num(), PendingSectorLoads.Num());
}

int32 AUniverseActor::FindNearestGalaxyStar(const FVector& CameraLocation) const
{
	int32 Best = INDEX_NONE;
	double BestDist = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < GalaxyStars.Num(); ++Index)
	{
		const double Dist = FVector::DistSquared(CameraLocation, GalaxyStars[Index].GalaxyLocation);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = Index;
		}
	}
	return Best;
}

void AUniverseActor::EnterSystem(int32 StarIndex)
{
	if (!GalaxyStars.IsValidIndex(StarIndex))
	{
		return;
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			SavedGalaxyPawnLocation = Pawn->GetActorLocation();
			SavedGalaxyPawnRotation = PC->GetControlRotation();
		}
	}

	// Keep galaxy sector meshes; only swap system overlays.
	ClearSystemVisuals();
	for (auto& Pair : SectorComponents)
	{
		for (UActorComponent* Component : Pair.Value)
		{
			if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Component))
			{
				Prim->SetVisibility(false);
			}
		}
	}

	ViewMode = ESingularityViewMode::System;
	FocusStarIndex = StarIndex;

	StripForeignLightsAndBlockers();
	EnsureFreeFlightPawn();

	const FStarRecord& Focus = GalaxyStars[StarIndex].Star;
	const FPlanetarySystem System = PugGenerateSystem(Focus.Id, Focus.Stellar);
	// Lift the system above any leftover map floor/collision plane.
	StarLocation = GetActorLocation() + FVector(0.0, 0.0, 100000.0);
	FocusStarTeffK = Focus.Stellar.TeffK;
	FocusStarLuminositySolar = FMath::Max(0.01, Focus.Stellar.LuminositySolar);
	auto NoCull = [this](UPrimitiveComponent* P) { ConfigureNoCull(P); };

	StarLight = NewObject<UDirectionalLightComponent>(this);
	StarLight->SetupAttachment(Root);
	StarLight->SetAtmosphereSunLight(false);
	StarLight->SetMobility(EComponentMobility::Movable);
	StarLight->SetCastShadows(false);
	StarLight->SetIntensity(static_cast<float>(8.0 * FMath::Sqrt(FocusStarLuminositySolar)));
	StarLight->SetLightColor(PugStarColor(FocusStarTeffK));
	StarLight->SetSpecularScale(0.5f);
	StarLight->SetVisibility(true);
	StarLight->RegisterComponent();
	SystemComponents.Add(StarLight);

	// Visual star marker only — does not emit light (directional light is the star).
	SpawnSphereTracked(this, Root, SphereMesh, ShapeMaterial, StarLocation,
		PugSolarRadiiToUu(FMath::Max(0.08, Focus.Stellar.RadiusSolar)),
		PugStarColor(Focus.Stellar.TeffK), SystemComponents, NoCull);

	if (AWorldSettings* Settings = GetWorld()->GetWorldSettings())
	{
		Settings->WorldToMeters = 100.0f;
		Settings->LightmassSettings.EnvironmentIntensity = 0.0f;
	}
	if (GEngine)
	{
		GEngine->Exec(GetWorld(), TEXT("r.SkyAtmosphere 0"));
		GEngine->Exec(GetWorld(), TEXT("r.VolumetricCloud 0"));
		GEngine->Exec(GetWorld(), TEXT("r.Fog 0"));
		GEngine->Exec(GetWorld(), TEXT("r.BloomQuality 0"));
		GEngine->Exec(GetWorld(), TEXT("r.EyeAdaptationQuality 0"));
		GEngine->Exec(GetWorld(), TEXT("r.DefaultFeature.AutoExposure 0"));
		GEngine->Exec(GetWorld(), TEXT("r.SupportSkyAtmosphereAffectsHeightFog 0"));
		GEngine->Exec(GetWorld(), TEXT("ShowFlag.Atmosphere 0"));
		GEngine->Exec(GetWorld(), TEXT("ShowFlag.AmbientCubemap 0"));
		GEngine->Exec(GetWorld(), TEXT("ShowFlag.Bloom 0"));
	}

	FVector FirstPlanetLocation = StarLocation + FVector(PugAuToUu(1.0), 0.0, 0.0);
	int32 Tiled = 0;

	for (int32 PlanetIndex = 0; PlanetIndex < System.Planets.Num(); ++PlanetIndex)
	{
		const FPlanetRecord& Planet = System.Planets[PlanetIndex];
		const FVector Au = PugKeplerPositionAu(Planet.Orbit, 0.0);
		const FVector Location = StarLocation + SystemPosAu(Au);
		if (PlanetIndex == 0)
		{
			FirstPlanetLocation = Location;
		}

		const double RadiusUu = PugEarthRadiiToUu(FMath::Max(0.05, Planet.RadiusEarth));
		if (UsesTiles(Planet.Class))
		{
			UProceduralMeshComponent* Body = NewObject<UProceduralMeshComponent>(this);
			Body->SetupAttachment(Root);
			Body->SetWorldLocation(Location);
			Body->RegisterComponent();
			ConfigureNoCull(Body);
			Body->SetCastShadow(true);
			SystemComponents.Add(Body);
			const FLinearColor AvgAlbedo = AverageTileAlbedo(Planet.Id, Planet.Class, Planet.EquilibriumTempK);
			UMaterialInterface* LitParent = ShapeMaterial ? ShapeMaterial.Get() : VertexColorMaterial.Get();
			UMaterialInterface* FaceMaterial = nullptr;
			if (LitParent)
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(LitParent, Body);
				Mid->SetVectorParameterValue(TEXT("Color"), AvgAlbedo);
				FaceMaterial = Mid;
			}
			// Single section for all faces — avoids square missing-face holes from per-section culling.
			BuildCubeSphere(Body, Planet.Id, Planet.Class, Planet.EquilibriumTempK, RadiusUu, FaceMaterial);
			++Tiled;
		}
		else
		{
			UStaticMeshComponent* Giant = SpawnSphereTracked(this, Root, SphereMesh, ShapeMaterial, Location, RadiusUu, GiantColor(Planet.Class), SystemComponents, NoCull);
			if (Giant)
			{
				Giant->SetCastShadow(true);
			}
		}

		for (const FMoonRecord& Moon : Planet.Moons)
		{
			// Landable PlanetTerrain streaming SCRAPPED — moons are static spheres only.
			const FVector MoonAu = PugKeplerPositionAu(Moon.Orbit, 0.0);
			const FVector MoonLoc = Location + SystemPosAu(MoonAu);
			const double MoonRUu = FMath::Max(PugEarthRadiiToUu(0.05), PugEarthRadiiToUu(Moon.RadiusEarth));
			SpawnSphereTracked(this, Root, SphereMesh, ShapeMaterial, MoonLoc, MoonRUu,
				FLinearColor(0.55f, 0.52f, 0.48f), SystemComponents, NoCull);
		}

		FPlanetMarker Marker;
		Marker.WorldLocation = Location;
		Marker.Orbit = Planet.Orbit;
		Marker.BodyRadiusUu = RadiusUu;
		Marker.Color = MarkerColor(Planet.Class);
		Marker.Caption = FString::Printf(TEXT("P%d %s  a=%.3f AU"), PlanetIndex, PugPlanetClassName(Planet.Class), Planet.Orbit.SemiMajorAu);
		Marker.bHasAtmosphere = PugAtmosphereForPlanet(Planet, Marker.Atmosphere);
		Markers.Add(Marker);
	}

	// Prefer first air world for spawn so atmosphere limb is immediately visible.
	int32 StartMarker = 0;
	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		if (Markers[Index].bHasAtmosphere)
		{
			StartMarker = Index;
			break;
		}
	}
	FVector StartPlanet = Markers.IsValidIndex(StartMarker) ? Markers[StartMarker].WorldLocation : FirstPlanetLocation;
	double StartHeight = Markers.IsValidIndex(StartMarker) ? Markers[StartMarker].BodyRadiusUu * 2.5 : PugEarthRadiiToUu(2.5);
	const FVector StartLoc = StartPlanet + FVector(0.0, 0.0, StartHeight);
	PlacePawn(StartLoc, StartPlanet);
	EnsureFreeFlightPawn();
	StripForeignLightsAndBlockers();
	UpdateStarLighting(StartLoc);
	UpdateAtmosphereSnapshot(StartLoc, 1.0f);
	UE_LOG(LogSingularity, Log, TEXT("System view: star %llu planets=%d tiled=%d landableMoons=%d"),
		Focus.Id, System.Planets.Num(), Tiled, LandableMoons.Num());
}

void AUniverseActor::ExitToGalaxy()
{
	ClearSystemVisuals();
	ViewMode = ESingularityViewMode::Galaxy;
	for (auto& Pair : SectorComponents)
	{
		for (UActorComponent* Component : Pair.Value)
		{
			if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Component))
			{
				Prim->SetVisibility(true);
			}
		}
	}
	PlacePawn(SavedGalaxyPawnLocation, SavedGalaxyPawnLocation + SavedGalaxyPawnRotation.Vector() * 100.0);
}

void AUniverseActor::UpdateInput()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC)
	{
		return;
	}

	if (ViewMode == ESingularityViewMode::Galaxy)
	{
		if (PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::F))
		{
			EnterSystem(InspectedGalaxyStar != INDEX_NONE ? InspectedGalaxyStar : FocusStarIndex);
		}
	}
	else
	{
		if (PC->WasInputKeyJustPressed(EKeys::G) || PC->WasInputKeyJustPressed(EKeys::BackSpace))
		{
			if (bSurfaceMode)
			{
				TryEjectToFlight();
			}
			ExitToGalaxy();
		}
		// L / landable PlanetTerrain SCRAPPED — key ignored.
	}
}

void AUniverseActor::DrawDensityOverview(const FVector& CameraLocation)
{
	UWorld* World = GetWorld();
	const double Size = GalaxyProfile.SectorSizePc;
	const double RMax = GalaxyProfile.DiskTruncationRadiusPc + 2.5 * GalaxyProfile.DiskEdgeSoftnessPc;
	const int32 SectorMax = static_cast<int32>(FMath::CeilToDouble(RMax / Size)) + 1;
	const FVector CamPc = WorldToPc(CameraLocation);
	const int32 Stride = CamPc.Z > 14000.0 ? 3 : (CamPc.Z > 9000.0 ? 2 : 2);

	for (int32 Sx = -SectorMax; Sx <= SectorMax; Sx += Stride)
	{
		for (int32 Sy = -SectorMax; Sy <= SectorMax; Sy += Stride)
		{
			FSectorCoord Coord;
			Coord.X = static_cast<int16>(Sx);
			Coord.Y = static_cast<int16>(Sy);
			Coord.Z = 0;
			if (LoadedSectors.Contains(Coord) || EmptySectors.Contains(Coord))
			{
				continue;
			}

			const double CenterPc[3] = {
				(Sx + 0.5) * Size,
				(Sy + 0.5) * Size,
				0.0
			};
			const double RCyl = FMath::Sqrt(CenterPc[0] * CenterPc[0] + CenterPc[1] * CenterPc[1]);
			if (RCyl > RMax)
			{
				continue;
			}

			const double Density = PugSampleDensity(GalaxyProfile, CenterPc);
			if (Density < 0.02)
			{
				continue;
			}

			const float T = static_cast<float>(FMath::Clamp(FMath::Sqrt(Density) / 2.2, 0.0, 1.0));
			const float Rim = static_cast<float>(FMath::Clamp((RMax - RCyl) / GalaxyProfile.DiskEdgeSoftnessPc, 0.0, 1.0));
			const uint8 Alpha = static_cast<uint8>(FMath::Clamp((0.10f + 0.70f * T) * (0.25f + 0.75f * Rim) * 255.0f, 20.0f, 220.0f));
			const FColor Color(static_cast<uint8>(50 + 150 * T), static_cast<uint8>(100 + 110 * T), 255, Alpha);

			const FVector Center = GalaxyPos(CenterPc);
			const float ExtentXy = static_cast<float>(Size * Stride * 0.46 * GalaxyUuPerPc);
			const float ExtentZ = static_cast<float>(Size * 0.04 * GalaxyUuPerPc);
			DrawDebugBox(World, Center, FVector(ExtentXy, ExtentXy, ExtentZ), Color, false, -1.0f, 0, 0.0f);
		}
	}
}

void AUniverseActor::DrawGalaxyOverlay(const FVector& CameraLocation)
{
	UWorld* World = GetWorld();
	InspectedGalaxyStar = FindNearestGalaxyStar(CameraLocation);

	DrawDensityOverview(CameraLocation);

	for (const auto& Pair : LoadedSectors)
	{
		if (Pair.Value.Stars.Num() == 0)
		{
			continue;
		}
		const FVector MinCorner = GalaxyPos(Pair.Value.MinPc);
		const FVector MaxCorner = GalaxyPos(Pair.Value.MaxPc);
		const FVector Center = 0.5 * (MinCorner + MaxCorner);
		const bool bFocus = Pair.Key == StreamFocus;
		const float CountT = FMath::Clamp(Pair.Value.Stars.Num() / 64.0f, 0.05f, 1.0f);
		DrawDebugBox(World, Center, 0.5 * (MaxCorner - MinCorner) * 0.96f,
			bFocus ? FColor(255, 200, 80) : FColor(80, 160, 255), false, -1.0f, SDPG_Foreground, 0.0f);

		const double Dist = FVector::Distance(CameraLocation, Center);
		const float MarkerR = static_cast<float>(FMath::Clamp(Dist * 0.012 * (0.4 + CountT), 8.0, GalaxyProfile.SectorSizePc * GalaxyUuPerPc * 0.35));
		DrawDebugSphere(World, Center, MarkerR, 10, FColor(255, 230, static_cast<uint8>(120 + 100 * CountT)), false, -1.0f, 0, 0.0f);
	}

	if (GalaxyStars.IsValidIndex(InspectedGalaxyStar))
	{
		const FGalaxyStarMarker& Star = GalaxyStars[InspectedGalaxyStar];
		const double Dist = FVector::Distance(CameraLocation, Star.GalaxyLocation);
		const float MarkerR = static_cast<float>(FMath::Clamp(Dist * 0.02, 4.0, 80.0));
		DrawDebugSphere(World, Star.GalaxyLocation, MarkerR, 12, FColor::Yellow, false, -1.0f, 0, 2.0f);
		DrawDebugString(World, Star.GalaxyLocation + FVector(0, 0, MarkerR * 1.5f),
			FString::Printf(TEXT("STAR  Teff %.0f K  sector (%d,%d,%d)  Enter/F"),
				Star.Star.Stellar.TeffK, Star.Sector.X, Star.Sector.Y, Star.Sector.Z),
			nullptr, FColor::Yellow, 0.0f, true);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9000, 0.0f, FColor::Cyan,
			TEXT("GALAXY  |  faint grid = distant density  |  bright boxes = loaded sectors with stars  |  Enter/F system"));
		GEngine->AddOnScreenDebugMessage(9001, 0.0f, FColor::White,
			FString::Printf(TEXT("Star sectors %d   empty skipped %d   stars %d   pending %d   focus (%d,%d,%d)"),
				LoadedSectors.Num(), EmptySectors.Num(), GalaxyStars.Num(), PendingSectorLoads.Num(),
				StreamFocus.X, StreamFocus.Y, StreamFocus.Z));
	}
}

void AUniverseActor::DrawSystemOverlay(const FVector& CameraLocation)
{
	UWorld* World = GetWorld();
	auto AuToOffset = [this](const FVector& Au) { return SystemPosAu(Au); };

	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		const FPlanetMarker& Marker = Markers[Index];
		const FColor Color = Marker.Color.ToFColor(true);
		constexpr int32 Segments = 128;
		// Thickness 0 = screen-space debug line (not world-space cylinder geometry).
		FVector Prev = AuToOffset(PugKeplerPositionAu(Marker.Orbit, 0.0));
		for (int32 Step = 1; Step <= Segments; ++Step)
		{
			FKeplerElements Sample = Marker.Orbit;
			Sample.MeanAnomaly0Rad = Marker.Orbit.MeanAnomaly0Rad + (static_cast<double>(Step) / Segments) * PugTau;
			const FVector Next = AuToOffset(PugKeplerPositionAu(Sample, 0.0));
			DrawDebugLine(World, StarLocation + Prev, StarLocation + Next, Color, false, -1.0f, SDPG_Foreground, 0.0f);
			Prev = Next;
		}

		const double Dist = FVector::Distance(CameraLocation, Marker.WorldLocation);
		const float MarkerR = static_cast<float>(FMath::Clamp(Dist * 0.004, 1000.0, Marker.BodyRadiusUu * 0.5));
		DrawDebugSphere(World, Marker.WorldLocation, MarkerR, 12, Color, false, -1.0f, SDPG_Foreground, 0.0f);
		DrawDebugString(World, Marker.WorldLocation,
			FString::Printf(TEXT("%s   (%.3g AU)"), *Marker.Caption, Dist / PugAuToUu(1.0)),
			nullptr, Color, 0.0f, true, 1.25f);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9100 + Index, 0.0f, Color,
				FString::Printf(TEXT("%s   %.4g AU from cam"), *Marker.Caption, Dist / PugAuToUu(1.0)));
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9000, 0.0f, FColor::Orange,
			TEXT("SYSTEM  |  SingularityAtmosphere  |  F7 tweaks  |  G = galaxy"));
		bool bShowTweaks = false;
		if (const IConsoleVariable* ShowTweaksCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SingularityAtmosphere.ShowTweaks")))
		{
			bShowTweaks = ShowTweaksCVar->GetInt() != 0;
		}
		if (bShowTweaks)
		{
			GEngine->AddOnScreenDebugMessage(9001, 0.0f, FColor::Cyan,
				SingularityAtmosphere::FormatLiveTweaksForDisplay());
			GEngine->AddOnScreenDebugMessage(9002, 0.0f, FColor::Silver,
				TEXT("~  r.SingularityAtmosphere.DensityScale / RayleighScale / MieScale / HeightScale / IlluminanceScale …  |  SingularityAtmosphere.ResetTweaks"));
		}
	}
}

void AUniverseActor::UpdateAtmosphereSnapshot(const FVector& CameraLocation, float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	USingularityAtmosphereSubsystem* Atm = World->GetSubsystem<USingularityAtmosphereSubsystem>();
	if (!Atm)
	{
		return;
	}

	if (ViewMode != ESingularityViewMode::System || Markers.IsEmpty())
	{
		Atm->ClearAtmospheres();
		AtmosphereFade = 0.0f;
		ActiveAtmospherePlanet = INDEX_NONE;
		return;
	}

	int32 MaxPlanets = 2;
	if (const IConsoleVariable* MaxPlanetsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SingularityAtmosphere.MaxPlanets")))
	{
		MaxPlanets = MaxPlanetsCVar->GetInt();
	}
	MaxPlanets = FMath::Clamp(MaxPlanets, 1, 4);

	FVector CamLoc = CameraLocation;
	FRotator CamRot = FRotator::ZeroRotator;
	float Aspect = 16.0f / 9.0f;
	float FOVDeg = 90.0f;
	double ViewportHeightPx = 1080.0;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		if (PC->PlayerCameraManager)
		{
			FOVDeg = PC->PlayerCameraManager->GetFOVAngle();
		}
	}
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		if (Size.Y > 1.0)
		{
			Aspect = static_cast<float>(Size.X / Size.Y);
			ViewportHeightPx = Size.Y;
		}
	}
	const FRotationMatrix CamM(CamRot);
	const float TanHalfFovX = FMath::Tan(FMath::DegreesToRadians(FOVDeg) * 0.5f);
	const double HalfFovXRad = FMath::DegreesToRadians(static_cast<double>(FOVDeg)) * 0.5;
	const double HalfFovYRad = FMath::Atan(FMath::Tan(HalfFovXRad) / FMath::Max(static_cast<double>(Aspect), 1e-3));
	// Skip sub-pixel / distant dots — full raymarch not worth it (guide distant-dot path).
	constexpr double MinProjectedRadiusPx = 4.0;

	struct FAtmCandidate
	{
		int32 Index = INDEX_NONE;
		double Dist = 0.0;
		double ScreenRadiusPx = 0.0;
	};
	TArray<FAtmCandidate, TInlineAllocator<8>> InRange;
	for (int32 Index = 0; Index < Markers.Num(); ++Index)
	{
		if (!Markers[Index].bHasAtmosphere)
		{
			continue;
		}
		const double Dist = FVector::Distance(CamLoc, Markers[Index].WorldLocation);
		const double ActivateDist = Markers[Index].BodyRadiusUu * 30.0;
		if (Dist > ActivateDist || Dist < 1.0)
		{
			continue;
		}
		const double AngularRadiusRad = FMath::Atan(Markers[Index].BodyRadiusUu / Dist);
		const double ScreenRadiusPx = (AngularRadiusRad / FMath::Max(HalfFovYRad, 1e-6)) * (ViewportHeightPx * 0.5);
		if (ScreenRadiusPx < MinProjectedRadiusPx)
		{
			continue; // distant dot — no atmosphere pass
		}
		InRange.Add({Index, Dist, ScreenRadiusPx});
	}
	// Prefer screen-relevant bodies, then nearer as tie-break.
	InRange.Sort([](const FAtmCandidate& A, const FAtmCandidate& B)
	{
		if (!FMath::IsNearlyEqual(A.ScreenRadiusPx, B.ScreenRadiusPx, 0.5))
		{
			return A.ScreenRadiusPx > B.ScreenRadiusPx;
		}
		return A.Dist < B.Dist;
	});

	const bool bWant = InRange.Num() > 0;
	AtmosphereFade = FMath::FInterpTo(AtmosphereFade, bWant ? 1.0f : 0.0f, DeltaSeconds, 2.5f);

	TArray<int32, TInlineAllocator<4>> Selected;
	if (bWant)
	{
		const int32 Take = FMath::Min(InRange.Num(), MaxPlanets);
		for (int32 I = 0; I < Take; ++I)
		{
			Selected.Add(InRange[I].Index);
		}
		// Nearest among selected for HUD / sky-view preference.
		int32 NearestSel = Selected[0];
		double NearestDist = TNumericLimits<double>::Max();
		for (const int32 Idx : Selected)
		{
			const double D = FVector::Distance(CamLoc, Markers[Idx].WorldLocation);
			if (D < NearestDist)
			{
				NearestDist = D;
				NearestSel = Idx;
			}
		}
		ActiveAtmospherePlanet = NearestSel;
		// Snapshot order: nearest first (VE re-sorts farthest-first for composite).
		Selected.Sort([&](const int32 A, const int32 B)
		{
			return FVector::Distance(CamLoc, Markers[A].WorldLocation)
				< FVector::Distance(CamLoc, Markers[B].WorldLocation);
		});
	}
	else if (AtmosphereFade >= 0.01f && Markers.IsValidIndex(ActiveAtmospherePlanet))
	{
		Selected.Add(ActiveAtmospherePlanet);
	}
	else
	{
		Atm->ClearAtmospheres();
		ActiveAtmospherePlanet = INDEX_NONE;
		return;
	}

	constexpr double CmPerKm = 100000.0;
	auto ToPerKm = [](const FLinearColor& C) -> FVector3f
	{
		return FVector3f(C.R * 1000.0f, C.G * 1000.0f, C.B * 1000.0f);
	};

	FSingularityAtmosphereViewSnapshot Snap;
	Snap.bActive = true;
	Snap.Fade = AtmosphereFade;
	Snap.Camera.CameraWorldCm = FVector3d(CamLoc);
	Snap.Camera.Forward = FVector3f(CamM.GetUnitAxis(EAxis::X));
	Snap.Camera.Right = FVector3f(CamM.GetUnitAxis(EAxis::Y));
	Snap.Camera.Up = FVector3f(CamM.GetUnitAxis(EAxis::Z));
	Snap.Camera.Aspect = Aspect;
	Snap.Camera.TanHalfFovY = TanHalfFovX / FMath::Max(Aspect, 1e-3f);

	for (const int32 MarkerIndex : Selected)
	{
		if (!Markers.IsValidIndex(MarkerIndex) || !Markers[MarkerIndex].bHasAtmosphere)
		{
			continue;
		}
		const FPlanetMarker& Marker = Markers[MarkerIndex];
		const FPugAtmosphereDescriptor& Desc = Marker.Atmosphere;

		const double BottomKm = Marker.BodyRadiusUu / CmPerKm;
		const double TopRatio = FMath::Clamp(
			Desc.OuterRadiusM / FMath::Max(Desc.InnerRadiusM, 1.0),
			1.03,
			Desc.Kind == EPugAtmosphereKind::HydrogenGiant ? 1.14 : 1.12);

		const FVector3f MieS = ToPerKm(Desc.MieScattering);
		const FVector3f MieA = ToPerKm(Desc.MieAbsorption);

		FSingularityAtmospherePlanetProxy Proxy;
		Proxy.BodyId = static_cast<uint64>(MarkerIndex);
		Proxy.PlanetCenterWorldCm = FVector3d(Marker.WorldLocation);
		Proxy.VisualBottomRadiusKm = BottomKm;
		Proxy.VisualTopRadiusKm = BottomKm * TopRatio;
		Proxy.VisualKmToWorldCm = CmPerKm;
		Proxy.RayleighScatteringPerKm = ToPerKm(Desc.RayleighScattering) * Desc.ArtisticIntensity;
		Proxy.RayleighScaleHeightKm = FMath::Max(Desc.RayleighScaleHeightM / 1000.0f, 0.5f);
		Proxy.MieScatteringPerKm = MieS * Desc.ArtisticIntensity;
		Proxy.MieExtinctionPerKm = (MieS + MieA) * Desc.ArtisticIntensity;
		Proxy.MieScaleHeightKm = FMath::Max(Desc.MieScaleHeightM / 1000.0f, 0.2f);
		Proxy.AbsorptionPerKm = ToPerKm(Desc.OzoneAbsorption) * Desc.ArtisticIntensity;
		Proxy.MieAnisotropyG = FMath::Clamp(Desc.MieAsymmetry, 0.0f, 0.95f);
		Proxy.GroundAlbedo = FVector3f(Desc.GroundAlbedo.R, Desc.GroundAlbedo.G, Desc.GroundAlbedo.B);
		Proxy.GroundBounce = Desc.Kind == EPugAtmosphereKind::ThinIcy ? 0.12f : 0.04f;
		Proxy.DensityScale = 1.0f;
		Proxy.AtmosphereProfileHash = ComputeSingularityAtmosphereProfileHash(Proxy);

		FVector ToStar = (StarLocation - Marker.WorldLocation).GetSafeNormal();
		if (ToStar.IsNearlyZero())
		{
			ToStar = FVector(1.0, 0.0, 0.0);
		}
		Proxy.DirectionToStar = FVector3f(ToStar);
		const float Illum = static_cast<float>(6.0 * FMath::Sqrt(FocusStarLuminositySolar) * Desc.ArtisticIntensity);
		Proxy.StarIlluminanceLinear = FVector3f(Illum, Illum, Illum);

		Snap.Planets.Add(Proxy);
	}

	if (Snap.Planets.Num() == 0)
	{
		Atm->ClearAtmospheres();
		return;
	}

	Atm->SetViewSnapshot(MoveTemp(Snap));
}

void AUniverseActor::UpdateStarLighting(const FVector& CameraLocation)
{
	if (!StarLight || ViewMode != ESingularityViewMode::System)
	{
		return;
	}

	// Continuous sun: always star → nearest body (no arena lighting swap).
	FVector FocusPlanet = CameraLocation;
	double Best = TNumericLimits<double>::Max();
	for (const FPlanetMarker& Marker : Markers)
	{
		const double Dist = FVector::DistSquared(CameraLocation, Marker.WorldLocation);
		if (Dist < Best)
		{
			Best = Dist;
			FocusPlanet = Marker.WorldLocation;
		}
	}
	FVector LightDir = (FocusPlanet - StarLocation).GetSafeNormal();
	if (LightDir.IsNearlyZero())
	{
		LightDir = FVector(1.0, 0.0, 0.0);
	}
	StarLight->SetWorldRotation(FRotationMatrix::MakeFromX(LightDir).Rotator());
	StarLight->SetIntensity(static_cast<float>(8.0 * FMath::Sqrt(FocusStarLuminositySolar)));
	StarLight->SetLightColor(PugStarColor(FocusStarTeffK));
	StarLight->SetAtmosphereSunLight(false);
	StarLight->SetCastShadows(false);
}

void AUniverseActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateInput();

	FVector CameraLocation = GetActorLocation();
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		FVector Loc;
		FRotator Rot;
		PC->GetPlayerViewPoint(Loc, Rot);
		CameraLocation = Loc;
	}

	if (ViewMode == ESingularityViewMode::Galaxy)
	{
		if (USingularityAtmosphereSubsystem* Atm = GetWorld()->GetSubsystem<USingularityAtmosphereSubsystem>())
		{
			Atm->ClearAtmospheres();
		}
		AtmosphereFade = 0.0f;
		SyncGalaxyStreaming(CameraLocation);
		DrawGalaxyOverlay(CameraLocation);
	}
	else
	{
		if (!bSurfaceMode)
		{
			EnsureFreeFlightPawn();
		}
		// Landable PlanetTerrain: SCRAPPED — no streaming / land path.
		UpdateAtmosphereSnapshot(CameraLocation, DeltaSeconds);
		UpdateStarLighting(CameraLocation);
		DrawSystemOverlay(CameraLocation);
	}
}

void AUniverseActor::SpawnLandableMoon(const FRockyPlanetDescriptor& Desc, const FVector& WorldCenter)
{
	// PlanetTerrain landable moons SCRAPPED — use static sphere moons in EnterSystem.
	UE_LOG(LogSingularity, Warning, TEXT("SpawnLandableMoon ignored (PlanetTerrain scrapped) id=%llu"),
		Desc.PlanetID);
	(void)WorldCenter;
}

void AUniverseActor::UpdateLandableMoons(const FVector& CameraLocation)
{
	(void)CameraLocation;
}

void AUniverseActor::CancelLandingPrep(const TCHAR* Reason)
{
	(void)Reason;
	bLandingPrepPending = false;
	LandingPrepElapsed = 0.0f;
	ActiveLandedMoon = nullptr;
}

void AUniverseActor::CompleteLandingSpawn()
{
	bLandingPrepPending = false;
}

void AUniverseActor::TickLandingPrep(float DeltaSeconds)
{
	(void)DeltaSeconds;
	bLandingPrepPending = false;
}

void AUniverseActor::TryLandOnMoon()
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(0x4C414E44, 3.f, FColor::Orange,
			TEXT("Landable PlanetTerrain SCRAPPED — L disabled"));
	}
}

void AUniverseActor::TryEjectToFlight()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	FVector EjectLoc = GetActorLocation() + FVector(0, 0, 100000);
	FVector LookAt = EjectLoc + FVector(0, 0, -1000);

	FVector Radial = FVector::UpVector;
	if (IsValid(SurfaceCharacter) && IsValid(ActiveLandedMoon))
	{
		Radial = (SurfaceCharacter->GetActorLocation() - ActiveLandedMoon->GetMoonCenter()).GetSafeNormal();
		if (Radial.IsNearlyZero())
		{
			Radial = FVector::UpVector;
		}
		// Resume flight from the walker's current world pose (no moon teleport).
		EjectLoc = SurfaceCharacter->GetActorLocation() + Radial * static_cast<float>(PugMToUu(200.0));
		LookAt = ActiveLandedMoon->GetMoonCenter();
	}

	if (PC)
	{
		PC->UnPossess();
	}

	if (IsValid(SurfaceCharacter))
	{
		SurfaceCharacter->Destroy();
		SurfaceCharacter = nullptr;
	}

	ActiveLandedMoon = nullptr;
	bSurfaceMode = false;
	bLandingPrepPending = false;

	APawn* Fly = IsValid(CachedFlyPawn) ? CachedFlyPawn.Get() : nullptr;
	if (!Fly && PC)
	{
		for (TActorIterator<ASpaceFlyPawn> It(GetWorld()); It; ++It)
		{
			Fly = *It;
			break;
		}
	}
	if (!Fly && PC && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Fly = GetWorld()->SpawnActor<ASpaceFlyPawn>(EjectLoc, FRotator::ZeroRotator, Params);
		CachedFlyPawn = Fly;
	}

	if (Fly && PC)
	{
		Fly->SetActorHiddenInGame(false);
		Fly->SetActorEnableCollision(false);
		Fly->SetActorTickEnabled(true);
		if (UPawnMovementComponent* Move = Fly->GetMovementComponent())
		{
			Move->SetComponentTickEnabled(true);
			Move->StopMovementImmediately();
		}
		Fly->SetActorLocation(EjectLoc, false, nullptr, ETeleportType::ResetPhysics);
		PC->Possess(Fly);
		PC->SetControlRotation((LookAt - EjectLoc).GetSafeNormal().Rotation());
		CachedFlyPawn = Fly;
	}

	EnsureFreeFlightPawn();
	UE_LOG(LogSingularity, Log, TEXT("Returned to flight (continuous, no moon rebuild)"));
}
