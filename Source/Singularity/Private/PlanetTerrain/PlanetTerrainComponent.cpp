#include "PlanetTerrain/PlanetTerrainComponent.h"
#include "PlanetTerrain/PlanetTerrainTweak.h"
#include "PlanetTerrain/PlanetTerrainDebug.h"
#include "PlanetTerrain/PlanetTerrainCollision.h"
#include "PlanetTerrain/PlanetTerrainGpuBake.h"
#include "PlanetTerrain/PlanetTerrainScheduler.h"
#include "PlanetTerrain/PlanetTerrainNormals.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Pug/PugCore.h"
#include "Singularity.h"
#include "SingularityPlanetTerrainNextMode.h"

namespace
{
/** v0.1 regolith stand-in (plan Phase 5): height/slope tint until a true triplanar PBR lands. */
FLinearColor PlanetTerrainRegolithColor(float HeightM, float AmplitudeM, const FVector& Normal, const FVector3d& Dir)
{
	const float Amp = FMath::Max(1.0f, AmplitudeM);
	const float Bowl = FMath::Clamp(-HeightM / Amp, 0.0f, 1.0f);
	const float Rim = FMath::Clamp(HeightM / Amp, 0.0f, 1.0f);
	const float Slope = 1.0f - FMath::Clamp(FVector::DotProduct(Normal.GetSafeNormal(), FVector(Dir)), 0.0f, 1.0f);

	FLinearColor Base(0.46f, 0.43f, 0.38f);
	Base *= (1.0f - 0.38f * Bowl);
	Base += FLinearColor(0.14f, 0.12f, 0.09f) * Rim;
	Base += FLinearColor(0.06f, 0.06f, 0.07f) * Slope;
	Base.A = 1.0f;
	return Base;
}
}

UPlanetTerrainComponent::UPlanetTerrainComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After pawn / UniverseActor UpdateCamera — never LOD on a zero planet-local pose.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPlanetTerrainComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPlanetTerrainComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PatchMeshes.Reset();
	PatchMeshList.Reset();
	DistantMesh = nullptr;
	if (Scheduler.IsValid())
	{
		Scheduler->Shutdown();
		Scheduler.Reset();
	}
	Quadtree.Reset();
	HeightField.Reset();
	Super::EndPlay(EndPlayReason);
}

void UPlanetTerrainComponent::SuspendForNextRenderer()
{
	if (Scheduler.IsValid())
	{
		Scheduler->Shutdown();
		Scheduler.Reset();
	}
	if (DistantMesh)
	{
		DistantMesh->SetVisibility(false);
		DistantMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	for (UProceduralMeshComponent* Mesh : PatchMeshList)
	{
		if (Mesh)
		{
			Mesh->SetVisibility(false);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void UPlanetTerrainComponent::InitializeTerrain(const FRockyPlanetDescriptor& InDescriptor, const FVector& InPlanetWorldCenter)
{
	if (SingularityTerrainRendererIsNext())
	{
		UE_LOG(LogSingularity, Warning, TEXT("InitializeTerrain skipped: singularity.TerrainRenderer.Mode=Next"));
		SuspendForNextRenderer();
		return;
	}

	Descriptor = InDescriptor;
	PlanetWorldCenter = InPlanetWorldCenter;
	Epoch = 1;
	bSurfacePlayMode = false;
	UuPerMeter = PugCmPerM;
	HeightField = MakeUnique<FPlanetTerrainHeight>(Descriptor);

	FPlanetTerrainLodParams Lod;
	Lod.LodDistanceFactor = LodDistanceFactor;
	Lod.MaxActivePatches = MaxActivePatches;
	Lod.ActivateDistanceM = Descriptor.RadiusMeters * 8.0;
	Lod.MinNeighborhoodLevel = 2;
	Lod.MaxLevel = 12;
	Lod.MergeHysteresis = 1.35;
	Quadtree = MakeUnique<FPlanetTerrainQuadtree>();
	Quadtree->Initialize(Descriptor.PlanetID, Epoch, Descriptor.RadiusMeters, Lod);
	LastLodPolicyBand = -1;

	Scheduler = MakeShared<FPlanetTerrainScheduler, ESPMode::ThreadSafe>();
	Scheduler->SetPlanet(Descriptor);
	Scheduler->SetEpoch(Epoch);
	Scheduler->SetMaxConcurrent(8);
	Scheduler->SetForceCpu(FPlanetTerrainDebug::bForceCpu);
	Scheduler->OnTileReady.BindUObject(this, &UPlanetTerrainComponent::OnTileReady);

	BuildSharedIndices(PatchResolution);
	BuildDistantFallback();

	EnsureSurfaceMaterialInstance();
}

void UPlanetTerrainComponent::SetPlanetWorldCenter(const FVector& Center)
{
	PlanetWorldCenter = Center;
	if (DistantMesh)
	{
		DistantMesh->SetWorldLocation(Center);
	}
}

void UPlanetTerrainComponent::ClearPatchMeshes()
{
	for (auto& Pair : PatchMeshes)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->DestroyComponent();
		}
	}
	PatchMeshes.Reset();
	PatchMeshList.Reset();
}

void UPlanetTerrainComponent::EnsureSurfaceMaterialInstance()
{
	if (!SurfaceMaterial)
	{
		SurfaceMID = nullptr;
		return;
	}
	if (!SurfaceMID || SurfaceMID->Parent != SurfaceMaterial)
	{
		SurfaceMID = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
	}
	// Optional Color pin (BasicShape). VertexColorMaterial ignores it and uses mesh colors.
	SurfaceMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.46f, 0.43f, 0.38f));
}

void UPlanetTerrainComponent::ConfigureTerrainMeshShadows(UProceduralMeshComponent* Mesh, bool bCast) const
{
	if (!Mesh)
	{
		return;
	}
	Mesh->SetCastShadow(bCast);
	Mesh->bCastDynamicShadow = bCast;
	Mesh->bAffectDynamicIndirectLighting = true;
	Mesh->SetReceivesDecals(false);
	Mesh->bAllowCullDistanceVolume = false;
	Mesh->SetCullDistance(0.0f);
	// Planetary patches span huge local extents; too-tight BoundsScale + HZB
	// false-culls the entire moon (emergency repair regression).
	const double AmpM = HeightField.IsValid() ? HeightField->GetAmplitudeMeters() : 5000.0;
	const float BoundScale = FMath::Clamp(
		static_cast<float>((Descriptor.RadiusMeters * 0.35 + AmpM) * UuPerMeter / 1000.0),
		200.0f, 100000.0f);
	Mesh->SetBoundsScale(BoundScale);
	Mesh->bUseAsOccluder = false; // terrain should not occlude itself into holes
	// Do NOT force visibility here — empty PMCs must stay hidden until CreateMeshSection.
}

void UPlanetTerrainComponent::SetSurfacePlayMode(bool /*bEnabled*/)
{
	// Continuous traversal: scale/teleport mode removed.
	UuPerMeter = PugCmPerM;
	bSurfacePlayMode = false;
	ApplyAltitudeLodPolicy(AltitudeM);
}

void UPlanetTerrainComponent::RebuildForMode(bool /*bSurfacePlay*/)
{
	// Legacy entry point from Enter/ExitSurfaceFrame — keep residency, keep scale.
	UuPerMeter = PugCmPerM;
	bSurfacePlayMode = false;
	EnsureSurfaceMaterialInstance();
	if (!DistantMesh)
	{
		BuildDistantFallback();
	}
	else
	{
		DistantMesh->SetWorldLocation(PlanetWorldCenter);
		DistantMesh->SetVisibility(true);
	}
	ApplyAltitudeLodPolicy(AltitudeM);
	UE_LOG(LogTemp, Log, TEXT("RebuildForMode: continuous (no scale/teleport/wipe) R=%.0fm uu/m=%.0f"),
		Descriptor.RadiusMeters, UuPerMeter);
}

void UPlanetTerrainComponent::ApplyTweakSettings(const FPlanetTerrainTweakSettings& S)
{
	LodDistanceFactor = S.LodDistanceFactor;
	MaxActivePatches = S.MaxActivePatches;
	PatchResolution = FMath::Clamp(S.PatchResolution, 8, 64);
	UploadBudgetMs = S.UploadBudgetMs;
	MorphDurationSeconds = S.MorphDurationSeconds;
	GpuMeshGraceFrames = S.GpuMeshGraceFrames;
	bGeomorphEnabled = S.bGeomorph;
	bEnableCollision = S.bEnableCollision;
	bShowDebugHud = S.bShowHud;
	bDebugSeams = S.bShowSeams;
	bDebugFaces = S.bShowFaces;

	FPlanetTerrainDebug::bForceShellVisible = S.bForceShell;
	FPlanetTerrainDebug::bPauseGeneration = S.bPauseGeneration;
	FPlanetTerrainDebug::bForceCpu = S.bForceCpu;
	FPlanetTerrainDebug::bGeomorph = S.bGeomorph;
	FPlanetTerrainDebug::bShowRequestedTiles = S.bShowRequested;
	FPlanetTerrainDebug::bShowCulledTiles = S.bShowCulled;
	FPlanetTerrainDebug::bShowCoverage = S.bShowCoverage;
	FPlanetTerrainDebug::bShowRejectionReason = S.bShowRejection;
	FPlanetTerrainDebug::bShowMorphAlpha = S.bShowMorph;
	FPlanetTerrainDebug::bShowFoveation = S.bShowFoveation;
	FPlanetTerrainDebug::bShowSeams = S.bShowSeams;
	FPlanetTerrainDebug::bShowFaces = S.bShowFaces;

	if (Scheduler.IsValid())
	{
		Scheduler->SetMaxConcurrent(S.MaxWorkers);
		Scheduler->SetForceCpu(S.bForceCpu);
	}

	// Force LOD policy refresh with new numbers.
	LastLodPolicyBand = -1;
	ApplyAltitudeLodPolicy(AltitudeM);
	FPlanetTerrainTweak::Settings().bDirty = false;
}

void UPlanetTerrainComponent::ApplyAltitudeLodPolicy(double AltitudeMeters)
{
	if (!Quadtree.IsValid())
	{
		return;
	}

	// Phase 7 peak defaults live in FPlanetTerrainTweakSettings; F6 edits them live.
	const FPlanetTerrainTweakSettings& Tw = FPlanetTerrainTweak::Settings();

	int32 Band = 0;
	if (AltitudeMeters > 1.0e6)
	{
		Band = 0;
	}
	else if (AltitudeMeters > 1.0e5)
	{
		Band = 1;
	}
	else if (AltitudeMeters > 1.0e4)
	{
		Band = 2;
	}
	else if (AltitudeMeters > 1.0e3)
	{
		Band = 3;
	}
	else
	{
		Band = 4;
	}

	if (!Tw.bDirty && Band == LastLodPolicyBand)
	{
		return;
	}
	if (LastLodPolicyBand >= 0 && Band < LastLodPolicyBand && (LastLodPolicyBand - Band) > 1)
	{
		Band = LastLodPolicyBand - 1;
	}
	LastLodPolicyBand = Band;

	FPlanetTerrainLodParams Lod = Quadtree->GetLodParams();
	Lod.bUseScreenSpaceLod = Tw.bUseScreenSpaceLod;
	Lod.LodDistanceFactor = Tw.LodDistanceFactor;
	Lod.MaxActivePatches = Tw.MaxActivePatches;
	Lod.MergeHysteresis = Tw.MergeHysteresis;
	Lod.ActivateDistanceM = Descriptor.RadiusMeters * Tw.ActivateDistanceRadii;
	Lod.TargetPixelError = Tw.TargetPixelError;
	Lod.PeripheralPixelError = Tw.PeripheralPixelError;
	Lod.PrefetchPixelError = Tw.PrefetchPixelError;
	Lod.FoveaInnerRadius = Tw.FoveaInnerRadius;
	Lod.FoveaOuterRadius = Tw.FoveaOuterRadius;
	Lod.GeometricErrorAmpFrac = Tw.GeometricErrorAmpFrac;
	Lod.GeometricErrorSizeFrac = Tw.GeometricErrorSizeFrac;
	Lod.CullAngularMarginRad = FMath::DegreesToRadians(Tw.CullAngularMarginDeg);
	Lod.FrustumMinDot = Tw.FrustumMinDot;
	Lod.FrustumPrefetchMinDot = Tw.FrustumPrefetchMinDot;
	Lod.HorizonReliefMarginM = Tw.HorizonReliefMarginKm * 1000.0;
	if (Tw.bUseAltitudeBands)
	{
		Lod.MaxLevel = Tw.BandMaxLevel[Band];
		Lod.MinNeighborhoodLevel = Tw.BandMinNeigh[Band];
	}
	else
	{
		Lod.MaxLevel = Tw.MaxLevelOverride;
		Lod.MinNeighborhoodLevel = Tw.MinNeighborhoodOverride;
	}
	Quadtree->SetLodParams(Lod);

	LodDistanceFactor = Tw.LodDistanceFactor;
	MaxActivePatches = Tw.MaxActivePatches;
}

FPlanetRelativePosition UPlanetTerrainComponent::GetCameraRelativePosition() const
{
	FPlanetRelativePosition P;
	P.PlanetID = Descriptor.PlanetID;
	P.LocalMeters = CameraPlanetLocal;
	return P;
}

void UPlanetTerrainComponent::RequestApproachStreaming(const FVector3d& UnitDir, int32 MinLevel, bool bAsLandingPrep)
{
	if (!Quadtree.IsValid() || !Scheduler.IsValid())
	{
		return;
	}
	PredictedImpactDir = UnitDir.GetSafeNormal();
	const int32 Cap = FMath::Clamp(MinLevel, 0, Quadtree->GetLodParams().MaxLevel);
	// Deepen at most +1 LOD per call — jumping to Cap floods workers and holes coverage.
	auto DeepenOne = [&](const FVector3d& Dir) -> FPlanetTerrainNode*
	{
		FPlanetTerrainNode* Leaf = Quadtree->FindLeafContaining(Dir);
		const int32 Have = Leaf ? static_cast<int32>(Leaf->Level) : 0;
		const int32 Target = FMath::Min(Cap, Have + 1);
		FPlanetTerrainNode* N = Quadtree->EnsurePathToLevel(Dir, Target);
		if (N && !N->Tile.IsValid())
		{
			N->bGenPending = true;
		}
		return N;
	};
	FPlanetTerrainNode* Primary = DeepenOne(PredictedImpactDir);
	const int32 Level = Primary ? Primary->Level : 0;
	const double Step = 1.0 / static_cast<double>(1 << FMath::Max(1, Level));
	FVector3d Tangent = FMath::Abs(PredictedImpactDir.Y) < 0.9
		? FVector3d::CrossProduct(PredictedImpactDir, FVector3d(0, 1, 0))
		: FVector3d::CrossProduct(PredictedImpactDir, FVector3d(1, 0, 0));
	Tangent.Normalize();
	const FVector3d Bitangent = FVector3d::CrossProduct(PredictedImpactDir, Tangent).GetSafeNormal();
	for (int32 Oy = -1; Oy <= 1; ++Oy)
	{
		for (int32 Ox = -1; Ox <= 1; ++Ox)
		{
			if (Ox == 0 && Oy == 0)
			{
				continue;
			}
			const FVector3d NDir = (PredictedImpactDir + Tangent * (Ox * Step) + Bitangent * (Oy * Step)).GetSafeNormal();
			if (FPlanetTerrainNode* Leaf = DeepenOne(NDir))
			{
				if (bAsLandingPrep)
				{
					LandingPrepKeys.Add(Leaf->Key);
				}
			}
		}
	}
	if (Primary && bAsLandingPrep)
	{
		LandingPrepKeys.Add(Primary->Key);
		bLandingPrepActive = true;
	}
}

void UPlanetTerrainComponent::SetCameraPlanetLocal(const FVector3d& PlanetLocalPos, const FVector3d& Forward, const FVector3d& Velocity)
{
	CameraPlanetLocal = PlanetLocalPos;
	DistanceFromCenterM = CameraPlanetLocal.Size();
	CameraForward = Forward.GetSafeNormal();
	if (CameraForward.IsNearlyZero())
	{
		CameraForward = DistanceFromCenterM > 1e-6
			? (-CameraPlanetLocal / DistanceFromCenterM) // look nadir if unknown
			: FVector3d(1, 0, 0);
	}
	CameraVelocity = Velocity;
}

void UPlanetTerrainComponent::SyncCameraFromPlayerView()
{
	if (AActor* Owner = GetOwner())
	{
		PlanetWorldCenter = Owner->GetActorLocation();
	}

	FVector CamWorld = PlanetWorldCenter;
	FVector FwdWorld(1, 0, 0);
	FVector VelWorld = FVector::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			FRotator Rot;
			PC->GetPlayerViewPoint(CamWorld, Rot);
			FwdWorld = Rot.Vector();
			if (APawn* Pawn = PC->GetPawn())
			{
				VelWorld = Pawn->GetVelocity();
			}
		}
	}

	RefreshAltitudeMetrics(CamWorld);
	const double UuPerM = FMath::Max(1e-9, UuPerMeter);
	SetCameraPlanetLocal(
		CameraPlanetLocal,
		FVector3d(FwdWorld.X, FwdWorld.Y, FwdWorld.Z),
		FVector3d(VelWorld.X / UuPerM, VelWorld.Y / UuPerM, VelWorld.Z / UuPerM));
}

void UPlanetTerrainComponent::RefreshAltitudeMetrics(const FVector& CameraWorld)
{
	if (AActor* Owner = GetOwner())
	{
		PlanetWorldCenter = Owner->GetActorLocation();
	}

	const double Scale = FMath::Max(1e-9, UuPerMeter);
	const FVector RelUu = CameraWorld - PlanetWorldCenter;
	// Planet-local metres (1 uu = 1 cm → /100).
	CameraPlanetLocal = FVector3d(RelUu.X / Scale, RelUu.Y / Scale, RelUu.Z / Scale);
	DistanceFromCenterM = CameraPlanetLocal.Size();

	if (DistanceFromCenterM < 1e-6 || !HeightField.IsValid())
	{
		HeightUnderCameraM = 0.0;
		AltitudeM = 0.0;
		return;
	}

	const FVector3d CamDir = CameraPlanetLocal / DistanceFromCenterM;
	double SurfaceH = 0.0;
	float DrawnMorph = 0.0f;
	if (!SampleDrawnHeight(CamDir, SurfaceH, DrawnMorph))
	{
		SurfaceH = HeightField->EvaluateHeightUnrestricted(CamDir);
	}
	HeightUnderCameraM = SurfaceH;

	// AGL: distance from center minus radius of terrain under the camera.
	const double SurfaceRadiusM = Descriptor.RadiusMeters + SurfaceH;
	AltitudeM = DistanceFromCenterM - SurfaceRadiusM;
}

void UPlanetTerrainComponent::BuildSharedIndices(int32 Resolution)
{
	if (SharedIndexResolution == Resolution && SharedIndices.Num() > 0)
	{
		return;
	}
	SharedIndexResolution = Resolution;
	const int32 N = Resolution + 1;
	SharedIndices.Reset();
	SharedIndices.Reserve(Resolution * Resolution * 6);
	for (int32 J = 0; J < Resolution; ++J)
	{
		for (int32 I = 0; I < Resolution; ++I)
		{
			const int32 I0 = J * N + I;
			const int32 I1 = I0 + 1;
			const int32 I2 = I0 + N;
			const int32 I3 = I2 + 1;
			SharedIndices.Add(I0);
			SharedIndices.Add(I2);
			SharedIndices.Add(I1);
			SharedIndices.Add(I1);
			SharedIndices.Add(I2);
			SharedIndices.Add(I3);
		}
	}

	// Skirt quads along four edges (extra verts appended after grid).
	SharedSkirtIndices.Reset();
	const int32 Base = N * N;
	// Top edge J=0, bottom J=Res, left I=0, right I=Res — each edge has Res segments.
	auto AddSkirtEdge = [&](int32 EdgeStart, bool bFlip)
	{
		for (int32 S = 0; S < Resolution; ++S)
		{
			const int32 A = EdgeStart + S;
			const int32 B = EdgeStart + S + 1;
			const int32 SA = Base + (EdgeStart == 0 ? S : (EdgeStart / N == Resolution ? Resolution + S : /*side*/ 2 * Resolution + S));
			// Simpler: store skirts inline in UploadTileMesh instead of shared skirt topology.
			(void)A;
			(void)B;
			(void)SA;
			(void)bFlip;
		}
	};
	(void)AddSkirtEdge;
}

void UPlanetTerrainComponent::BuildDistantFallback()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (!DistantMesh)
	{
		DistantMesh = NewObject<UProceduralMeshComponent>(Owner, TEXT("PlanetTerrainDistant"));
		DistantMesh->SetupAttachment(Owner->GetRootComponent());
		DistantMesh->RegisterComponent();
		DistantMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DistantMesh->SetCastShadow(true);
	}
	DistantMesh->SetWorldLocation(PlanetWorldCenter);

	// Same physical radius as patches (no 0.99 inset — holes exposed the void).
	constexpr double RadiusScale = 1.0;

	constexpr int32 N = 24;
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Norms;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Verts.Reserve(6 * N * N);
	for (uint8 Face = 0; Face < 6; ++Face)
	{
		const int32 Base = Verts.Num();
		for (int32 J = 0; J <= N; ++J)
		{
			for (int32 I = 0; I <= N; ++I)
			{
				const double U = static_cast<double>(I) / N;
				const double V = static_cast<double>(J) / N;
				const FVector3d Dir = PlanetTerrainFacePoint(Face, U, V);
				const double H = HeightField.IsValid()
					? HeightField->EvaluateHeightAtSpacing(Dir, Descriptor.RadiusMeters * 0.002)
					: 0.0;
				const FVector3d P = Dir * ((Descriptor.RadiusMeters + H) * RadiusScale);
				// Distant mesh is centered on planet — convert metres→uu relative to center.
				Verts.Add(FVector(P.X * UuPerMeter, P.Y * UuPerMeter, P.Z * UuPerMeter));
				Norms.Add(FVector(Dir.X, Dir.Y, Dir.Z));
				UVs.Add(FVector2D(U, V));
				const FVector Nrm(Dir.X, Dir.Y, Dir.Z);
				Colors.Add(PlanetTerrainRegolithColor(
					static_cast<float>(H),
					static_cast<float>(HeightField.IsValid() ? HeightField->GetAmplitudeMeters() : 1.0),
					Nrm,
					Dir));
				Tangents.Add(FProcMeshTangent(1, 0, 0));
			}
		}
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const int32 I0 = Base + J * (N + 1) + I;
				Tris.Add(I0);
				Tris.Add(I0 + (N + 1));
				Tris.Add(I0 + 1);
				Tris.Add(I0 + 1);
				Tris.Add(I0 + (N + 1));
				Tris.Add(I0 + (N + 1) + 1);
			}
		}
	}
	DistantMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Norms, UVs, Colors, Tangents, false);
	if (SurfaceMID)
	{
		DistantMesh->SetMaterial(0, SurfaceMID);
	}
	else if (SurfaceMaterial)
	{
		DistantMesh->SetMaterial(0, SurfaceMaterial);
	}
	DistantMesh->SetVisibility(true);
	DistantMesh->SetHiddenInGame(false);
	ConfigureTerrainMeshShadows(DistantMesh, AltitudeM < 80000.0);
	const float BoundR = static_cast<float>(GetRenderRadiusUu() * 1.15);
	DistantMesh->SetBoundsScale(FMath::Max(1.0f, BoundR / 1000.0f));
}

void UPlanetTerrainComponent::UpdateDistantVisibility(double CamDistM)
{
	if (!DistantMesh)
	{
		return;
	}
	// Coverage invariant: shell stays until ALL six face roots are Resident
	// AND their patch meshes exist. One refined child must never hide the shell.
	const bool bForceShell = FPlanetTerrainDebug::bForceShellVisible;
	bool bRootsMeshed = false;
	if (Quadtree.IsValid() && Quadtree->HasFullRootCoverage())
	{
		bRootsMeshed = true;
		for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
		{
			if (!Root.IsValid() || !PatchMeshes.Contains(Root->Key))
			{
				bRootsMeshed = false;
				break;
			}
		}
	}
	const bool bShowShell = bForceShell || !bRootsMeshed;
	DistantMesh->SetVisibility(bShowShell);
	DistantMesh->SetHiddenInGame(!bShowShell);
	if (bShowShell)
	{
		DistantMesh->SetCastShadow(AltitudeM < 80000.0);
		DistantMesh->bUseAsOccluder = false;
		DistantMesh->SetCullDistance(0.0f);
		DistantMesh->bAllowCullDistanceVolume = false;
		const float BoundR = static_cast<float>(GetRenderRadiusUu() * 1.15);
		DistantMesh->SetBoundsScale(FMath::Max(5000.0f, BoundR / 500.0f));
		// Keep shell slightly under patches so LOD detail is not buried (ForceShell uses 1.0).
		const float Scale = bForceShell ? 1.0f : FPlanetTerrainTweak::Settings().ShellScale;
		DistantMesh->SetWorldScale3D(FVector(Scale));
	}
	else
	{
		DistantMesh->SetWorldScale3D(FVector(1.0f));
	}
	(void)CamDistM;
}

void UPlanetTerrainComponent::EnsurePatchMesh(FPlanetTerrainNode& Node)
{
	if (PatchMeshes.Contains(Node.Key))
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const FName Name = *FString::Printf(TEXT("TerrainPatch_%u_%u_%u_%u"), Node.Face, Node.Level, Node.X, Node.Y);
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, Name);
	Mesh->SetupAttachment(Owner->GetRootComponent());
	Mesh->SetVisibility(false);
	Mesh->SetHiddenInGame(true);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EnsureSurfaceMaterialInstance();
	if (SurfaceMID)
	{
		Mesh->SetMaterial(0, SurfaceMID);
	}
	else if (SurfaceMaterial)
	{
		Mesh->SetMaterial(0, SurfaceMaterial);
	}
	Mesh->RegisterComponent();
	ConfigureTerrainMeshShadows(Mesh, true);
	Mesh->SetVisibility(false);
	Mesh->SetHiddenInGame(true);
	PatchMeshes.Add(Node.Key, Mesh);
	PatchMeshList.Add(Mesh);
}

void UPlanetTerrainComponent::UploadTileMesh(FPlanetTerrainNode& Node, const FPlanetTerrainTileData& Data)
{
	EnsurePatchMesh(Node);
	UProceduralMeshComponent* Mesh = PatchMeshes.FindRef(Node.Key);
	if (!Mesh)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_UploadTileMesh);
	const double T0 = FPlatformTime::Seconds();
	const int32 Res = Data.Resolution;
	BuildSharedIndices(Res);
	const int32 N = Res + 1;
	const int32 GridVerts = N * N;
	// Rebuild baseline: uniform tile morph (parent coarse → fine). Edge lock comes later.
	const float Morph = bGeomorphEnabled
		? FMath::Clamp(FMath::Max(Node.DrawMorph, Node.TemporalMorph), 0.0f, 1.0f)
		: 0.0f;

	TArray<FVector> Verts;
	TArray<FVector> Norms;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Verts.SetNum(GridVerts);
	Norms.SetNum(GridVerts);
	UVs.SetNum(GridVerts);
	Colors.SetNum(GridVerts);
	Tangents.SetNum(GridVerts);

	const FVector3d Origin = Data.TileOrigin;
	const double SkirtDepthM = Data.WorldSizeM * 0.12 + 200.0;
	const double CoarseSpacing = Data.SampleSpacingM * 2.0;

	if (HeightField.IsValid())
	{
		FPlanetTerrainNormals::ComputeMorphed(
			*HeightField, Data.Face, Data.U0, Data.V0, Data.U1, Data.V1, Res,
			Data.SampleSpacingM, CoarseSpacing, Morph, Norms);
	}
	else
	{
		Norms = Data.Normals;
	}

	for (int32 Idx = 0; Idx < GridVerts; ++Idx)
	{
		const float Hf = Data.HeightFineM[Idx];
		const float Hc = Data.HeightCoarseM[Idx];
		const float H = FMath::Lerp(Hf, Hc, Morph);
		const FVector3d Dir = Data.Directions[Idx];
		const FVector3d AbsPos = Dir * (Descriptor.RadiusMeters + static_cast<double>(H));
		const FVector3d Rel = AbsPos - Origin;
		Verts[Idx] = FVector(Rel.X * UuPerMeter, Rel.Y * UuPerMeter, Rel.Z * UuPerMeter);
		UVs[Idx] = Data.UVs[Idx];
		Colors[Idx] = PlanetTerrainRegolithColor(
			H,
			static_cast<float>(HeightField.IsValid() ? HeightField->GetAmplitudeMeters() : 1.0),
			Norms[Idx],
			Dir);
		const FVector NrmSafe = Norms[Idx].GetSafeNormal();
		FVector Tangent = FVector::CrossProduct(NrmSafe, FVector::UpVector);
		if (Tangent.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			Tangent = FVector::CrossProduct(NrmSafe, FVector::RightVector);
		}
		Tangents[Idx] = FProcMeshTangent(Tangent.GetSafeNormal(), false);
	}

	// Append skirt vertices (duplicate edge verts pushed inward/down).
	TArray<int32> Indices = SharedIndices;
	const int32 SkirtBase = Verts.Num();
	// Skirt verts: copy locals first — TArray::Add(Array[i]) asserts when growth reallocates.
	const int32 SkirtVertBudget = Res * 8; // 4 edges × 2 verts per segment (with corner dupes)
	Verts.Reserve(GridVerts + SkirtVertBudget);
	Norms.Reserve(GridVerts + SkirtVertBudget);
	UVs.Reserve(GridVerts + SkirtVertBudget);
	Colors.Reserve(GridVerts + SkirtVertBudget);
	Tangents.Reserve(GridVerts + SkirtVertBudget);

	auto PushSkirtVert = [&](int32 SrcIdx)
	{
		const FVector P0 = Verts[SrcIdx];
		const FVector Nrm = Norms[SrcIdx];
		const FVector2D UV = UVs[SrcIdx];
		const FLinearColor Col = Colors[SrcIdx];
		const FProcMeshTangent Tan = Tangents[SrcIdx];
		const FVector P = P0 - Nrm * static_cast<float>(SkirtDepthM * UuPerMeter);
		Verts.Add(P);
		Norms.Add(Nrm);
		UVs.Add(UV);
		Colors.Add(Col);
		Tangents.Add(Tan);
		return Verts.Num() - 1;
	};

	for (int32 I = 0; I < Res; ++I)
	{
		// Bottom edge J=0
		const int32 A = I;
		const int32 B = I + 1;
		const int32 SA = PushSkirtVert(A);
		const int32 SB = PushSkirtVert(B);
		Indices.Append({A, SA, B, B, SA, SB});
		// Top edge
		const int32 C = Res * N + I;
		const int32 D = C + 1;
		const int32 SC = PushSkirtVert(C);
		const int32 SD = PushSkirtVert(D);
		Indices.Append({C, D, SC, D, SD, SC});
	}
	for (int32 J = 0; J < Res; ++J)
	{
		const int32 A = J * N;
		const int32 B = (J + 1) * N;
		const int32 SA = PushSkirtVert(A);
		const int32 SB = PushSkirtVert(B);
		Indices.Append({A, B, SA, B, SB, SA});
		const int32 C = J * N + Res;
		const int32 D = (J + 1) * N + Res;
		const int32 SC = PushSkirtVert(C);
		const int32 SD = PushSkirtVert(D);
		Indices.Append({C, SC, D, D, SC, SD});
	}
	(void)SkirtBase;

	// Rebuild baseline: collision only on underfoot Create (not morph Update).
	const FVector3d Nadir = CameraPlanetLocal.SizeSquared() > 0.0
		? CameraPlanetLocal.GetSafeNormal()
		: FVector3d(0, 0, 1);
	const double DotNadir = FVector3d::DotProduct(Nadir, Data.Directions[Data.Directions.Num() / 2]);
	const bool bWantCollision = bEnableCollision && (AltitudeM < 25000.0 || DotNadir > 0.92);

	Mesh->SetVisibility(false);
	Mesh->SetHiddenInGame(true);

	const bool bHasSection = Mesh->GetNumSections() > 0;
	const bool bSameTopology = bHasSection && Node.UploadedMorph >= 0.0f
		&& Mesh->GetProcMeshSection(0)
		&& Mesh->GetProcMeshSection(0)->ProcVertexBuffer.Num() == Verts.Num();

	EnsureSurfaceMaterialInstance();
	UMaterialInterface* Mat = SurfaceMID ? static_cast<UMaterialInterface*>(SurfaceMID.Get())
										 : SurfaceMaterial.Get();
	if (Mat)
	{
		Mesh->SetMaterial(0, Mat);
	}

	if (bSameTopology && !bWantCollision)
	{
		Mesh->UpdateMeshSection_LinearColor(0, Verts, Norms, UVs, Colors, Tangents);
		if (Scheduler.IsValid())
		{
			++Scheduler->GetStatsMutable().UploadsUpdate;
		}
	}
	else if (bSameTopology && bWantCollision && Node.Tile.IsValid() && Node.Tile->bCollisionResident)
	{
		Mesh->UpdateMeshSection_LinearColor(0, Verts, Norms, UVs, Colors, Tangents);
		if (Scheduler.IsValid())
		{
			++Scheduler->GetStatsMutable().UploadsUpdate;
		}
	}
	else
	{
		Mesh->CreateMeshSection_LinearColor(0, Verts, Indices, Norms, UVs, Colors, Tangents, bWantCollision);
		if (Scheduler.IsValid())
		{
			++Scheduler->GetStatsMutable().UploadsCreate;
		}
	}
	if (Mat)
	{
		Mesh->SetMaterial(0, Mat);
	}

	const FVector WorldOrigin = PlanetWorldCenter + FVector(
		Origin.X * UuPerMeter, Origin.Y * UuPerMeter, Origin.Z * UuPerMeter);
	Mesh->SetWorldLocation(WorldOrigin);
	ConfigureTerrainMeshShadows(Mesh, true);
	if (bWantCollision)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	Mesh->SetVisibility(true);
	Mesh->SetHiddenInGame(false);

	if (Node.Tile.IsValid())
	{
		Node.Tile->State = EPlanetTerrainTileState::Resident;
		Node.Tile->bCollisionResident = bWantCollision || Node.Tile->bCollisionResident;
	}
	Node.UploadedMorph = Morph;
	Node.bGenPending = false;

	LastUploadMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	UploadMsAccum += LastUploadMs;
	++UploadsThisFrame;
}

void UPlanetTerrainComponent::OnTileReady(TSharedPtr<FPlanetTerrainTileData> Tile)
{
	// Cache identity ignores GenerationEpoch — accept any epoch-compatible tile for the key.
	if (!Tile.IsValid() || !Quadtree.IsValid())
	{
		return;
	}

	TArray<FPlanetTerrainNode*> Stack;
	for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
	{
		if (Root.IsValid())
		{
			Stack.Add(Root.Get());
		}
	}
	while (Stack.Num() > 0)
	{
		FPlanetTerrainNode* N = Stack.Pop(EAllowShrinking::No);
		if (N->bDisposed)
		{
			continue;
		}
		const bool bKeyMatch = N->Key.PlanetID == Tile->Key.PlanetID
			&& N->Key.Face == Tile->Key.Face
			&& N->Key.Level == Tile->Key.Level
			&& N->Key.X == Tile->Key.X
			&& N->Key.Y == Tile->Key.Y;
		if (bKeyMatch)
		{
			// Only promote into the live tree — disposed / merged nodes keep data in CPU cache only.
			if (N->Level > 0)
			{
				if (FPlanetTerrainNode* Parent = Quadtree->FindParent(N->Key))
				{
					if (Parent->Tile.IsValid())
					{
						const float ParentMorph = FMath::Max(Parent->DrawMorph, Parent->TemporalMorph);
						PlanetTerrainResampleCoarseFromParent(*Tile, *Parent->Tile, ParentMorph);
					}
				}
			}
			N->Tile = Tile;
			N->bGenPending = false;
			PendingUploadKeys.AddUnique(N->Key);
			return;
		}
		for (int32 I = 0; I < 4; ++I)
		{
			if (N->Children[I].IsValid())
			{
				Stack.Add(N->Children[I].Get());
			}
		}
	}
	// No matching live node — tile remains in scheduler CPU cache for later reuse.
}

void UPlanetTerrainComponent::FlushUploadBudget()
{
	if (!Quadtree.IsValid() || PendingUploadKeys.Num() == 0)
	{
		return;
	}

	// Prefer coarse roots first so the body appears; landing pad keys win when active.
	PendingUploadKeys.Sort([this](const FPlanetTerrainTileKey& A, const FPlanetTerrainTileKey& B)
	{
		if (bLandingPrepActive && LandingPrepKeys.Num() > 0)
		{
			const bool PA = LandingPrepKeys.Contains(A);
			const bool PB = LandingPrepKeys.Contains(B);
			if (PA != PB)
			{
				return PA && !PB;
			}
		}
		if (A.Level != B.Level)
		{
			return A.Level < B.Level;
		}
		return A.Face < B.Face;
	});

	while (PendingUploadKeys.Num() > 0 && UploadMsAccum < UploadBudgetMs)
	{
		const FPlanetTerrainTileKey Key = PendingUploadKeys[0];
		PendingUploadKeys.RemoveAt(0);

		FPlanetTerrainNode* Node = nullptr;
		TArray<FPlanetTerrainNode*> Stack;
		for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
		{
			if (Root.IsValid())
			{
				Stack.Add(Root.Get());
			}
		}
		while (Stack.Num() > 0)
		{
			FPlanetTerrainNode* N = Stack.Pop(EAllowShrinking::No);
			if (N->Key == Key)
			{
				Node = N;
				break;
			}
			for (int32 I = 0; I < 4; ++I)
			{
				if (N->Children[I].IsValid())
				{
					Stack.Add(N->Children[I].Get());
				}
			}
		}
		if (!Node || !Node->Tile.IsValid())
		{
			continue;
		}
		if (Node->Tile->State == EPlanetTerrainTileState::Resident && PatchMeshes.Contains(Key))
		{
			continue;
		}
		UploadTileMesh(*Node, *Node->Tile);
	}
}

float UPlanetTerrainComponent::SampleLeafMorphAtDir(const FVector3d& UnitDir) const
{
	if (!Quadtree.IsValid() || !bGeomorphEnabled)
	{
		return 0.0f;
	}
	if (const FPlanetTerrainNode* Leaf = Quadtree->FindLeafContaining(UnitDir.GetSafeNormal()))
	{
		return FMath::Clamp(FMath::Max(Leaf->DrawMorph, Leaf->TemporalMorph), 0.0f, 1.0f);
	}
	return 0.0f;
}

void UPlanetTerrainComponent::FlushCollisionBudget()
{
	// Rebuild baseline: collision cooked with underfoot CreateMeshSection.
	// Separate cook pipeline deferred until freeflight is stable.
	PendingCollisionKeys.Reset();
}

void UPlanetTerrainComponent::DestroyPatchMesh(const FPlanetTerrainTileKey& Key)
{
	PendingCollisionKeys.Remove(Key);
	if (UProceduralMeshComponent* Mesh = PatchMeshes.FindRef(Key))
	{
		Mesh->ClearAllMeshSections();
		Mesh->DestroyComponent();
		PatchMeshList.Remove(Mesh);
	}
	PatchMeshes.Remove(Key);
}

void UPlanetTerrainComponent::PruneOrphanPatchMeshes()
{
	if (!Quadtree.IsValid() || PatchMeshes.Num() == 0)
	{
		return;
	}
	TSet<FPlanetTerrainTileKey> Live;
	TArray<FPlanetTerrainNode*> Stack;
	for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
	{
		if (Root.IsValid())
		{
			Stack.Add(Root.Get());
		}
	}
	while (Stack.Num() > 0)
	{
		FPlanetTerrainNode* N = Stack.Pop(EAllowShrinking::No);
		if (!N || N->bDisposed)
		{
			continue;
		}
		Live.Add(N->Key);
		for (int32 I = 0; I < 4; ++I)
		{
			if (N->Children[I].IsValid())
			{
				Stack.Add(N->Children[I].Get());
			}
		}
	}
	TArray<FPlanetTerrainTileKey> Orphans;
	for (const auto& Pair : PatchMeshes)
	{
		if (!Live.Contains(Pair.Key))
		{
			Orphans.Add(Pair.Key);
		}
	}
	for (const FPlanetTerrainTileKey& K : Orphans)
	{
		DestroyPatchMesh(K);
	}
}

void UPlanetTerrainComponent::EvictUnusedGpuMeshes(const TSet<FPlanetTerrainTileKey>& /*KeepKeys*/)
{
	// Peak path: deload = QT DisposeChildren → PruneOrphanPatchMeshes only.
	// Aggressive Resident demotion caused holes / white flashes. Do not demote here.
	PruneOrphanPatchMeshes();
}

void UPlanetTerrainComponent::UpdateWorldOriginIfNeeded(const FVector3d& /*CamPlanetLocal*/)
{
	// Rebase when near the body so AU-scale absolute coords don't destroy edge continuity.
	// CRITICAL: must not fire every frame — log showed per-frame rebase storms during approach.
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner || AltitudeM > 250000.0 || AltitudeM < 0.0)
	{
		return;
	}
	const FVector Center = Owner->GetActorLocation();
	constexpr double MinRebaseDistUu = 5.0e5; // 5 km
	if (Center.SizeSquared() < FMath::Square(MinRebaseDistUu))
	{
		return; // already near world origin
	}
	const int32 RebaseCooldown = FMath::Max(0, FPlanetTerrainTweak::Settings().WorldOriginRebaseCooldownFrames);
	if ((FrameIndex - LastWorldOriginRebaseFrame) < RebaseCooldown)
	{
		return;
	}
	// FIntVector is int32 — reject coordinates that would overflow RoundToInt.
	constexpr double MaxOriginComponent = 2.0e9;
	if (FMath::Abs(Center.X) > MaxOriginComponent
		|| FMath::Abs(Center.Y) > MaxOriginComponent
		|| FMath::Abs(Center.Z) > MaxOriginComponent)
	{
		return;
	}
	const FIntVector NewOrigin(
		FMath::RoundToInt(Center.X),
		FMath::RoundToInt(Center.Y),
		FMath::RoundToInt(Center.Z));
	if (NewOrigin == World->OriginLocation)
	{
		return;
	}
	World->SetNewWorldOrigin(NewOrigin);
	LastWorldOriginRebaseFrame = FrameIndex;
	PlanetWorldCenter = Owner->GetActorLocation();
	if (DistantMesh)
	{
		DistantMesh->SetWorldLocation(PlanetWorldCenter);
	}
	TArray<FPlanetTerrainNode*> Leaves;
	if (Quadtree.IsValid())
	{
		Quadtree->CollectDrawLeaves(Leaves);
	}
	for (FPlanetTerrainNode* Leaf : Leaves)
	{
		if (!Leaf || !Leaf->Tile.IsValid())
		{
			continue;
		}
		if (UProceduralMeshComponent* Mesh = PatchMeshes.FindRef(Leaf->Key))
		{
			const FVector3d Origin = Leaf->Tile->TileOrigin;
			Mesh->SetWorldLocation(PlanetWorldCenter + FVector(
				Origin.X * UuPerMeter, Origin.Y * UuPerMeter, Origin.Z * UuPerMeter));
			ConfigureTerrainMeshShadows(Mesh, true);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("PlanetTerrain world origin rebased near moon (alt_surface=%.0fm) centerLen=%.0f"),
		AltitudeM, Center.Size());
}

void UPlanetTerrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (SingularityTerrainRendererIsNext())
	{
		SuspendForNextRenderer();
		return;
	}
	if (!HeightField.IsValid() || !Quadtree.IsValid() || !Scheduler.IsValid())
	{
		return;
	}

	++FrameIndex;
	UploadsThisFrame = 0;
	UploadMsAccum = 0.0;

	// Keep planet center synced to owner (after world-origin rebase).
	if (AActor* Owner = GetOwner())
	{
		PlanetWorldCenter = Owner->GetActorLocation();
		if (DistantMesh)
		{
			DistantMesh->SetWorldLocation(PlanetWorldCenter);
		}
	}

	// CRITICAL: resolve planet-local camera BEFORE any LOD / streaming.
	// UniverseActor::UpdateCamera is a backup; this is the authoritative path.
	SyncCameraFromPlayerView();

	const FVector3d CamDir = DistanceFromCenterM > 1e-9
		? (CameraPlanetLocal / DistanceFromCenterM)
		: FVector3d(0, 0, 1);

	// Valid camera: outside a tiny dead-zone around the origin (not "inside the moon core").
	if (DistanceFromCenterM > 1.0)
	{
		ApplyAltitudeLodPolicy(AltitudeM);
	}
	else
	{
		// Still demand face roots so we never sit on an empty scheduler.
		Quadtree->EnsureMandatoryRootCoverage();
	}

	// Live F6 tweaks — only when dirty (sliders set the flag; avoid per-frame LOD thrash).
	{
		FPlanetTerrainTweakSettings& Tw = FPlanetTerrainTweak::Settings();
		if (Tw.bApplyLive && Tw.bDirty)
		{
			ApplyTweakSettings(Tw);
		}
	}

	// Peak freeflight predictive streaming (nadir + velocity horizon).
	if (DistanceFromCenterM > Descriptor.RadiusMeters * 0.25 && AltitudeM < 2.0e5)
	{
		const double Speed = CameraVelocity.Size();
		const double BakeSec = FMath::Clamp(GetLastBakeMs() * 0.001, 0.005, 0.25);
		const double HorizonSec = FMath::Clamp(BakeSec * 4.0 + Speed * 0.00002, 0.5, 12.0);
		if (Speed > 1.0 && (FrameIndex % 12) == 0)
		{
			const FVector3d Future = (CameraPlanetLocal + CameraVelocity * HorizonSec).GetSafeNormal();
			const int32 PrepLevel = AltitudeM < 1.0e4 ? 6 : (AltitudeM < 5.0e4 ? 4 : 3);
			RequestApproachStreaming(Future, PrepLevel, false);
		}
		else if (AltitudeM < 2.0e4 && (FrameIndex % 20) == 0)
		{
			RequestApproachStreaming(CamDir, 5, false);
		}
	}

	if (SurfaceLodFreezeSeconds > 0.0f)
	{
		SurfaceLodFreezeSeconds = FMath::Max(0.0f, SurfaceLodFreezeSeconds - DeltaTime);
		if (SurfaceLodFreezeSeconds <= 0.0f && Quadtree.IsValid())
		{
			Quadtree->SetFrozen(false);
		}
	}

	UpdateDistantVisibility(DistanceFromCenterM);
	UpdateWorldOriginIfNeeded(CameraPlanetLocal);
	// Rebase shifts world coords — re-resolve planet-local before LOD.
	if ((FrameIndex - LastWorldOriginRebaseFrame) == 0)
	{
		SyncCameraFromPlayerView();
	}

	FPlanetTerrainCameraQuery Query;
	Query.PlanetLocalPosition = CameraPlanetLocal;
	Query.Forward = CameraForward.SizeSquared() > 1e-8
		? CameraForward.GetSafeNormal()
		: (-CamDir);
	Query.Velocity = CameraVelocity;
	Query.TerrainAmplitudeM = HeightField->GetAmplitudeMeters();
	Query.bHasFrustum = true;
	{
		FVector3d Right = FVector3d::CrossProduct(Query.Forward, FVector3d(0, 0, 1));
		if (Right.SizeSquared() < 1e-8)
		{
			Right = FVector3d::CrossProduct(Query.Forward, FVector3d(0, 1, 0));
		}
		Query.Right = Right.GetSafeNormal();
		Query.Up = FVector3d::CrossProduct(Query.Right, Query.Forward).GetSafeNormal();
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (PC->PlayerCameraManager)
		{
			Query.VerticalFovRad = FMath::DegreesToRadians(
				PC->PlayerCameraManager->GetFOVAngle());
			Query.ScreenHeightPx = 1080.0;
			Query.AspectRatio = 16.0 / 9.0;
			if (GEngine && GEngine->GameViewport)
			{
				FVector2D Size(1920, 1080);
				GEngine->GameViewport->GetViewportSize(Size);
				Query.ScreenHeightPx = FMath::Max(720.0, static_cast<double>(Size.Y));
				if (Size.Y > 1.0)
				{
					Query.AspectRatio = static_cast<double>(Size.X) / static_cast<double>(Size.Y);
				}
			}
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrain_UpdateLod);
		Quadtree->UpdateLod(Query, *HeightField, FrameIndex);
	}
	// Merges dispose QT nodes — free orphaned GPU meshes immediately.
	PruneOrphanPatchMeshes();
	if (Scheduler.IsValid())
	{
		Scheduler->GetStatsMutable().TilesFrustumCulled = Quadtree->GetFrustumCulledCount();
		Scheduler->GetStatsMutable().TilesHorizonCulled = Quadtree->GetHorizonCulledCount();
		Scheduler->GetStatsMutable().SplitsScreenError = Quadtree->GetScreenErrorSplitCount();
		Scheduler->GetStatsMutable().BudgetExhausted = Quadtree->GetBudgetExhaustedCount();
	}

	// Preemptive desired-set sync (cancels obsolete queued / optional running work).
	TArray<FPlanetTerrainNode*> Requests;
	Quadtree->CollectGenerationRequests(Requests);
	TArray<FPlanetTerrainJob> Desired;
	Desired.Reserve(Requests.Num());
	const double SpeedM = CameraVelocity.Size();
	const double SafetyCorridorM = FMath::Clamp(3000.0 + SpeedM * 2.0, 2000.0, 40000.0);
	for (FPlanetTerrainNode* Node : Requests)
	{
		if (!Node || Node->bDisposed)
		{
			continue;
		}
		FPlanetTerrainJob Job;
		Job.Key = Node->Key;
		Job.Key.GenerationEpoch = Epoch;
		Job.Epoch = Epoch;
		Job.U0 = Node->U0;
		Job.V0 = Node->V0;
		Job.U1 = Node->U1;
		Job.V1 = Node->V1;
		// Coarser grid for high LOD parents — big bake win during descent.
		int32 Res = PatchResolution;
		if (Node->Level <= 1)
		{
			Res = FMath::Min(Res, 16);
		}
		else if (Node->Level <= 3)
		{
			Res = FMath::Min(Res, 24);
		}
		Job.Resolution = Res;
		Job.ScreenErrorPx = Node->ScreenErrorPx;

		const FVector3d NodeDir = Node->CenterLocal.GetSafeNormal();
		const double GeodesicM = FMath::Acos(FMath::Clamp(FVector3d::DotProduct(CamDir, NodeDir), -1.0, 1.0))
			* Descriptor.RadiusMeters;

		if (Node->bMandatoryCoverage || Node->Level == 0)
		{
			Job.Priority = EPlanetTerrainJobPriority::MandatoryCoverage;
		}
		else if (bLandingPrepActive && LandingPrepKeys.Contains(Node->Key))
		{
			Job.Priority = EPlanetTerrainJobPriority::Underfoot;
		}
		else if (GeodesicM < SafetyCorridorM * 0.5)
		{
			Job.Priority = EPlanetTerrainJobPriority::Underfoot;
		}
		else if (GeodesicM < SafetyCorridorM)
		{
			Job.Priority = EPlanetTerrainJobPriority::Collision;
		}
		else if (Node->bBoundsInFrustum && Node->ScreenErrorPx > 4.0f)
		{
			Job.Priority = EPlanetTerrainJobPriority::PredictedCorridor;
		}
		else
		{
			Job.Priority = EPlanetTerrainJobPriority::ScreenError;
		}
		Desired.Add(Job);
	}
	Scheduler->SyncDesiredSet(Desired);
	Scheduler->Tick(DeltaTime);
	FlushUploadBudget();

	// Advance temporal morph (parent→child blend) on all resident nodes.
	if (bGeomorphEnabled && MorphDurationSeconds > 0.0f)
	{
		const float MorphStep = DeltaTime / MorphDurationSeconds;
		TArray<FPlanetTerrainNode*> MorphStack;
		for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
		{
			if (Root.IsValid())
			{
				MorphStack.Add(Root.Get());
			}
		}
		while (MorphStack.Num() > 0)
		{
			FPlanetTerrainNode* N = MorphStack.Pop(EAllowShrinking::No);
			if (N->TemporalMorph > 0.0f)
			{
				N->TemporalMorph = FMath::Max(0.0f, N->TemporalMorph - MorphStep);
			}
			for (int32 I = 0; I < 4; ++I)
			{
				if (N->Children[I].IsValid())
				{
					MorphStack.Add(N->Children[I].Get());
				}
			}
		}
	}

	// Keep draw-set visible. Morph: every frame while temporal handoff active,
	// otherwise every 2 frames (avoids starving first-time uploads).
	TArray<FPlanetTerrainNode*> Leaves;
	Quadtree->CollectDrawLeaves(Leaves);
	for (FPlanetTerrainNode* Leaf : Leaves)
	{
		if (!Leaf || !Leaf->Tile.IsValid() || Leaf->Tile->State != EPlanetTerrainTileState::Resident)
		{
			continue;
		}
		Leaf->Tile->LastUseSeconds = FPlatformTime::Seconds();
		const float TargetMorph = bGeomorphEnabled
			? FMath::Clamp(FMath::Max(Leaf->DrawMorph, Leaf->TemporalMorph), 0.0f, 1.0f)
			: 0.0f;
		// Only burn upload budget on active temporal handoff (not every DrawMorph twitch).
		const bool bMorphDirty = Leaf->TemporalMorph > 0.0f
			&& FMath::Abs(TargetMorph - Leaf->UploadedMorph) > 0.03f;
		// Prefer first-time uploads over morph refreshes when the queue is backed up.
		if (bGeomorphEnabled && bMorphDirty && PendingUploadKeys.Num() == 0
			&& UploadMsAccum < UploadBudgetMs * 0.5f)
		{
			UploadTileMesh(*Leaf, *Leaf->Tile);
		}
		else if (UProceduralMeshComponent* Mesh = PatchMeshes.FindRef(Leaf->Key))
		{
			if (Mesh->GetNumSections() > 0)
			{
				Mesh->SetVisibility(true);
				Mesh->SetHiddenInGame(false);
			}
		}
	}

	// Draw-set visibility. Keep face-root meshes always on (coverage). Never show
	// a PMC that has zero sections (white square flash).
	TSet<FPlanetTerrainTileKey> Visible;
	for (FPlanetTerrainNode* Leaf : Leaves)
	{
		if (Leaf)
		{
			Visible.Add(Leaf->Key);
			if (Leaf->Tile.IsValid())
			{
				Leaf->Tile->PinReason = EPlanetTerrainPinReason::VisibleDraw;
				Leaf->Tile->LastPinnedFrame = FrameIndex;
			}
		}
	}
	for (auto& Pair : PatchMeshes)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const bool bHasGeom = Pair.Value->GetNumSections() > 0;
		const bool bWant = bHasGeom && Visible.Contains(Pair.Key);
		Pair.Value->SetVisibility(bWant);
		Pair.Value->SetHiddenInGame(!bWant);
	}

	TSet<FPlanetTerrainTileKey> Pins;
	Quadtree->CollectCoveragePinKeys(Pins);
	for (const FPlanetTerrainTileKey& K : Visible)
	{
		Pins.Add(K);
	}
	Scheduler->EvictToBudget(256 * 1024 * 1024, Pins);
	EvictUnusedGpuMeshes(Pins);

	if (bDebugSeams || bDebugFaces || bDebugCollision || bShowDebugHud
		|| FPlanetTerrainDebug::bShowRequestedTiles || FPlanetTerrainDebug::bShowCulledTiles
		|| FPlanetTerrainDebug::bShowCoverage || FPlanetTerrainDebug::bShowRejectionReason
		|| FPlanetTerrainDebug::bShowMorphAlpha || FPlanetTerrainDebug::bShowFoveation)
	{
		DrawDebugOverlays();
	}
}

void UPlanetTerrainComponent::DrawDebugOverlays() const
{
	UWorld* World = GetWorld();
	if (!World || !Quadtree.IsValid() || !HeightField.IsValid())
	{
		return;
	}
	TArray<FPlanetTerrainNode*> Leaves;
	Quadtree->CollectDrawLeaves(Leaves);

	auto WorldFromLocalM = [this](const FVector3d& LocalM) -> FVector
	{
		return PlanetWorldCenter + FVector(LocalM.X * UuPerMeter, LocalM.Y * UuPerMeter, LocalM.Z * UuPerMeter);
	};

	for (FPlanetTerrainNode* Leaf : Leaves)
	{
		if (!Leaf)
		{
			continue;
		}
		const FVector C = WorldFromLocalM(Leaf->CenterSurfaceLocal);
		FColor Col = FColor::Green;
		if (bDebugFaces)
		{
			static const FColor FaceCols[6] = {
				FColor::Red, FColor::Blue, FColor::Green, FColor::Yellow, FColor::Cyan, FColor::Magenta
			};
			Col = FaceCols[Leaf->Face % 6];
		}
		else
		{
			const uint8 L = Leaf->Level;
			Col = FColor(static_cast<uint8>(40 + L * 28), static_cast<uint8>(220 - L * 18), static_cast<uint8>(60 + L * 20));
		}
		const float MarkerR = static_cast<float>(FMath::Max(80.0, Leaf->WorldSizeM * UuPerMeter * 0.02));
		DrawDebugSphere(World, C, MarkerR, 6, Col, false, -1.f, 0, 2.f);

		if (bDebugSeams || bDebugFaces)
		{
			// Tile boundary wireframe (4 corners on the surface).
			const FVector3d Corners[4] = {
				PlanetTerrainFacePoint(Leaf->Face, Leaf->U0, Leaf->V0),
				PlanetTerrainFacePoint(Leaf->Face, Leaf->U1, Leaf->V0),
				PlanetTerrainFacePoint(Leaf->Face, Leaf->U1, Leaf->V1),
				PlanetTerrainFacePoint(Leaf->Face, Leaf->U0, Leaf->V1),
			};
			FVector W[4];
			for (int32 I = 0; I < 4; ++I)
			{
				const double H = HeightField->EvaluateHeightAtSpacing(Corners[I], Leaf->WorldSizeM / 32.0);
				W[I] = WorldFromLocalM(Corners[I] * (Descriptor.RadiusMeters + H));
			}
			const FColor EdgeCol = bDebugFaces ? Col : FColor(255, 180, 40);
			for (int32 I = 0; I < 4; ++I)
			{
				DrawDebugLine(World, W[I], W[(I + 1) & 3], EdgeCol, false, -1.f, 0, 3.f);
			}
		}

		if (bDebugSeams && Leaf->Tile.IsValid() && HeightField.IsValid())
		{
			// Height mismatch heatmap samples along the four edges vs continuous field.
			const int32 Samples = 8;
			const auto EdgeHeat = [&](double Ua, double Va, double Ub, double Vb)
			{
				for (int32 S = 0; S <= Samples; ++S)
				{
					const double T = static_cast<double>(S) / Samples;
					const double U = FMath::Lerp(Ua, Ub, T);
					const double V = FMath::Lerp(Va, Vb, T);
					const FVector3d Dir = PlanetTerrainFacePoint(Leaf->Face, U, V);
					double DrawnH = 0.0;
					float Morph = FMath::Max(Leaf->DrawMorph, Leaf->TemporalMorph);
					if (!PlanetTerrainSampleTileHeight(*Leaf->Tile, Dir, Morph, DrawnH))
					{
						continue;
					}
					const double TruthH = HeightField->EvaluateHeightAtSpacing(Dir, Leaf->Tile->SampleSpacingM);
					const double Err = FMath::Abs(DrawnH - TruthH);
					const float Amp = static_cast<float>(HeightField->GetAmplitudeMeters());
					const float NormErr = Amp > 1.0f ? static_cast<float>(Err / Amp) : static_cast<float>(Err);
					const FColor Heat = NormErr < 0.002f ? FColor::Green
						: (NormErr < 0.02f ? FColor::Yellow : FColor::Red);
					const FVector P = WorldFromLocalM(Dir * (Descriptor.RadiusMeters + DrawnH));
					DrawDebugPoint(World, P, 8.0f, Heat, false, -1.f, 0);
				}
			};
			EdgeHeat(Leaf->U0, Leaf->V0, Leaf->U1, Leaf->V0);
			EdgeHeat(Leaf->U0, Leaf->V1, Leaf->U1, Leaf->V1);
			EdgeHeat(Leaf->U0, Leaf->V0, Leaf->U0, Leaf->V1);
			EdgeHeat(Leaf->U1, Leaf->V0, Leaf->U1, Leaf->V1);

			// Neighbor LOD stubs (cheap): green Δ≤1, yellow/red worse.
			const double Du = FMath::Max(1e-4, (Leaf->U1 - Leaf->U0) * 0.02);
			const double Dv = FMath::Max(1e-4, (Leaf->V1 - Leaf->V0) * 0.02);
			const double MidU = 0.5 * (Leaf->U0 + Leaf->U1);
			const double MidV = 0.5 * (Leaf->V0 + Leaf->V1);
			const FVector3d OutDirs[4] = {
				PlanetTerrainFacePoint(Leaf->Face, MidU, Leaf->V0 - Dv).GetSafeNormal(),
				PlanetTerrainFacePoint(Leaf->Face, MidU, Leaf->V1 + Dv).GetSafeNormal(),
				PlanetTerrainFacePoint(Leaf->Face, Leaf->U0 - Du, MidV).GetSafeNormal(),
				PlanetTerrainFacePoint(Leaf->Face, Leaf->U1 + Du, MidV).GetSafeNormal(),
			};
			for (int32 E = 0; E < 4; ++E)
			{
				FPlanetTerrainNode* Nbr = Quadtree->FindLeafContaining(OutDirs[E]);
				const int32 DLevel = Nbr
					? FMath::Abs(static_cast<int32>(Leaf->Level) - static_cast<int32>(Nbr->Level))
					: 0;
				const FColor LodCol = DLevel <= 1 ? FColor(80, 220, 120)
					: (DLevel == 2 ? FColor::Yellow : FColor::Red);
				const double H = HeightField->EvaluateHeightUnrestricted(OutDirs[E]);
				const FVector P = WorldFromLocalM(OutDirs[E] * (Descriptor.RadiusMeters + H));
				DrawDebugPoint(World, P, 10.0f, LodCol, false, -1.f, 0);
			}
		}

		if (FPlanetTerrainDebug::bShowMorphAlpha || FPlanetTerrainDebug::bShowFoveation
			|| FPlanetTerrainDebug::bShowCoverage)
		{
			const float MorphA = FMath::Clamp(FMath::Max(Leaf->DrawMorph, Leaf->TemporalMorph), 0.0f, 1.0f);
			const float FovW = FMath::Clamp(Leaf->FoveationWeight, 0.0f, 1.0f);
			FColor OverlayCol = FColor::White;
			if (FPlanetTerrainDebug::bShowMorphAlpha)
			{
				OverlayCol = FColor(static_cast<uint8>(255 * MorphA), static_cast<uint8>(80 + 100 * (1.0f - MorphA)), 40);
			}
			else if (FPlanetTerrainDebug::bShowFoveation)
			{
				OverlayCol = FColor(static_cast<uint8>(255 * (1.0f - FovW)), static_cast<uint8>(255 * FovW), 40);
			}
			else // coverage
			{
				const bool bHasMesh = Leaf->Tile.IsValid()
					&& Leaf->Tile->State == EPlanetTerrainTileState::Resident;
				OverlayCol = bHasMesh ? FColor(40, 220, 80) : FColor(220, 40, 40);
			}
			DrawDebugSphere(World, C, MarkerR * 1.35f, 8, OverlayCol, false, -1.f, 0, 1.5f);
		}

		if (FPlanetTerrainDebug::bShowRejectionReason && Leaf->RejectReason != EPlanetTerrainRejectReason::None)
		{
			const TCHAR* Reason = TEXT("?");
			switch (Leaf->RejectReason)
			{
			case EPlanetTerrainRejectReason::Frustum: Reason = TEXT("FRUSTUM"); break;
			case EPlanetTerrainRejectReason::Horizon: Reason = TEXT("HORIZON"); break;
			case EPlanetTerrainRejectReason::Budget: Reason = TEXT("BUDGET"); break;
			case EPlanetTerrainRejectReason::Scheduler: Reason = TEXT("SCHED"); break;
			default: break;
			}
			DrawDebugString(World, C, Reason, nullptr, FColor::Orange, 0.f, true, 1.2f);
		}
	}

	if (FPlanetTerrainDebug::bShowRequestedTiles || FPlanetTerrainDebug::bShowCulledTiles)
	{
		TArray<FPlanetTerrainNode*> Stack;
		for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
		{
			if (Root.IsValid())
			{
				Stack.Add(Root.Get());
			}
		}
		while (Stack.Num() > 0)
		{
			FPlanetTerrainNode* N = Stack.Pop(EAllowShrinking::No);
			const FVector C = WorldFromLocalM(N->CenterSurfaceLocal);
			if (FPlanetTerrainDebug::bShowRequestedTiles && N->bGenPending && !N->Tile.IsValid())
			{
				DrawDebugSphere(World, C, 120.f, 4, FColor::Cyan, false, -1.f, 0, 2.f);
			}
			if (FPlanetTerrainDebug::bShowCulledTiles
				&& N->RejectReason != EPlanetTerrainRejectReason::None)
			{
				DrawDebugSphere(World, C, 90.f, 4, FColor(180, 60, 255), false, -1.f, 0, 1.f);
			}
			for (int32 I = 0; I < 4; ++I)
			{
				if (N->Children[I].IsValid())
				{
					Stack.Add(N->Children[I].Get());
				}
			}
		}
	}
}

double UPlanetTerrainComponent::SampleHeight(const FVector3d& UnitDir) const
{
	return HeightField.IsValid() ? HeightField->EvaluateHeightUnrestricted(UnitDir) : 0.0;
}

FVector3d UPlanetTerrainComponent::SampleSurfacePoint(const FVector3d& UnitDir) const
{
	const FVector3d D = UnitDir.GetSafeNormal();
	return D * (Descriptor.RadiusMeters + SampleHeight(D));
}

FVector UPlanetTerrainComponent::GetSurfaceWorldLocation(const FVector3d& UnitDir, float HoverUu) const
{
	const FVector3d D = UnitDir.GetSafeNormal();
	const FVector3d SurfM = SampleSurfacePoint(D);
	const FVector World = PlanetWorldCenter
		+ FVector(SurfM.X * UuPerMeter, SurfM.Y * UuPerMeter, SurfM.Z * UuPerMeter)
		+ FVector(D.X, D.Y, D.Z) * HoverUu;
	return World;
}

void UPlanetTerrainComponent::CollectLandingPadNodes(
	const FVector3d& UnitDir, int32 MinLevel, TArray<FPlanetTerrainNode*>& OutNodes)
{
	OutNodes.Reset();
	if (!Quadtree.IsValid())
	{
		return;
	}
	TSet<FPlanetTerrainTileKey> Seen;
	auto AddUnique = [&](FPlanetTerrainNode* Node)
	{
		if (!Node || Node->bDisposed || Seen.Contains(Node->Key))
		{
			return;
		}
		Seen.Add(Node->Key);
		OutNodes.Add(Node);
	};

	for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
	{
		AddUnique(Root.Get());
	}

	const int32 TargetLevel = FMath::Clamp(MinLevel, 0, 7);
	AddUnique(Quadtree->EnsurePathToLevel(UnitDir, TargetLevel));

	uint8 Face = 0;
	double U = 0.0, V = 0.0;
	if (FPlanetTerrainQuadtree::DirectionToFaceUV(UnitDir, Face, U, V)
		&& Quadtree->GetRoots().IsValidIndex(Face))
	{
		FPlanetTerrainNode* N = Quadtree->GetRoots()[Face].Get();
		while (N)
		{
			AddUnique(N);
			if (!N->Children[0].IsValid())
			{
				break;
			}
			for (int32 I = 0; I < 4; ++I)
			{
				AddUnique(N->Children[I].Get());
			}
			const double Um = 0.5 * (N->U0 + N->U1);
			const double Vm = 0.5 * (N->V0 + N->V1);
			const int32 Ix = U < Um ? 0 : 1;
			const int32 Iy = V < Vm ? 0 : 1;
			N = N->Children[Iy * 2 + Ix].Get();
		}
	}

	const double Step = 1.0 / static_cast<double>(1 << FMath::Max(1, TargetLevel));
	const FVector3d D = UnitDir.GetSafeNormal();
	FVector3d Tangent = FMath::Abs(D.Y) < 0.9 ? FVector3d::CrossProduct(D, FVector3d(0, 1, 0))
											  : FVector3d::CrossProduct(D, FVector3d(1, 0, 0));
	Tangent.Normalize();
	const FVector3d Bitangent = FVector3d::CrossProduct(D, Tangent).GetSafeNormal();
	const double Angle = Step * 0.9;
	for (int32 Oy = -2; Oy <= 2; ++Oy)
	{
		for (int32 Ox = -2; Ox <= 2; ++Ox)
		{
			if (Ox == 0 && Oy == 0)
			{
				continue;
			}
			const FVector3d Neighbor = (D + Tangent * (Ox * Angle) + Bitangent * (Oy * Angle)).GetSafeNormal();
			if (FPlanetTerrainNode* Leaf = Quadtree->EnsurePathToLevel(Neighbor, TargetLevel))
			{
				AddUnique(Leaf);
				uint8 NFace = 0;
				double NU = 0.0, NV = 0.0;
				if (FPlanetTerrainQuadtree::DirectionToFaceUV(Neighbor, NFace, NU, NV)
					&& Quadtree->GetRoots().IsValidIndex(NFace))
				{
					FPlanetTerrainNode* N = Quadtree->GetRoots()[NFace].Get();
					while (N && N->Children[0].IsValid() && N->Level < TargetLevel)
					{
						AddUnique(N);
						for (int32 I = 0; I < 4; ++I)
						{
							AddUnique(N->Children[I].Get());
						}
						const double Um = 0.5 * (N->U0 + N->U1);
						const double Vm = 0.5 * (N->V0 + N->V1);
						const int32 Ix = NU < Um ? 0 : 1;
						const int32 Iy = NV < Vm ? 0 : 1;
						N = N->Children[Iy * 2 + Ix].Get();
					}
				}
			}
		}
	}
}

bool UPlanetTerrainComponent::BeginLandingPrep(const FVector3d& UnitDir, int32 MinLevel)
{
	if (!HeightField.IsValid() || !Quadtree.IsValid() || !Scheduler.IsValid())
	{
		return false;
	}

	BuildSharedIndices(PatchResolution);
	TArray<FPlanetTerrainNode*> Nodes;
	CollectLandingPadNodes(UnitDir, MinLevel, Nodes);
	LandingPrepKeys.Reset();
	LandingPrepDir = UnitDir.GetSafeNormal();
	LandingPrepMinLevel = MinLevel;

	for (FPlanetTerrainNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}
		LandingPrepKeys.Add(Node->Key);
		if (!Node->Tile.IsValid()
			|| Node->Tile->State < EPlanetTerrainTileState::ReadyForUpload)
		{
			Node->bGenPending = true;
		}
		// Immediate cache→upload if a prior epoch left compatible data (rare after Rebuild).
		if (TSharedPtr<FPlanetTerrainTileData> Cached = Scheduler->FindCached(Node->Key))
		{
			if (Cached->State >= EPlanetTerrainTileState::ReadyForUpload)
			{
				Node->Tile = Cached;
				Node->bGenPending = false;
				UploadTileMesh(*Node, *Cached);
			}
		}
	}

	// Continuous traversal: do NOT teleport CameraPlanetLocal. Mild freeze keeps pad stable.
	Quadtree->SetFrozen(true);
	SurfaceLodFreezeSeconds = 1.5f;
	bLandingPrepActive = true;
	UE_LOG(LogTemp, Log, TEXT("BeginLandingPrep async: %d nodes (MinLevel=%d)"), Nodes.Num(), MinLevel);
	return Nodes.Num() > 0;
}

bool UPlanetTerrainComponent::IsLandingPadReady(const FVector3d& UnitDir, int32 MinLevel) const
{
	if (!Quadtree.IsValid() || LandingPrepKeys.Num() == 0)
	{
		return false;
	}
	(void)UnitDir;
	(void)MinLevel;

	// Primary path leaf must be Resident (walkable). Roots must be Resident
	// so the body is not an empty skybox. Neighborhood: allow a few missing.
	int32 Missing = 0;
	int32 RootsReady = 0;
	bool bPrimaryReady = false;
	FPlanetTerrainNode* Primary = Quadtree->FindNodeAlongPath(LandingPrepDir, LandingPrepMinLevel);
	if (Primary && Primary->Tile.IsValid() && Primary->Tile->State == EPlanetTerrainTileState::Resident
		&& Primary->Level >= LandingPrepMinLevel)
	{
		bPrimaryReady = true;
	}

	TArray<FPlanetTerrainNode*> Stack;
	for (const TUniquePtr<FPlanetTerrainNode>& Root : Quadtree->GetRoots())
	{
		if (Root.IsValid())
		{
			Stack.Add(Root.Get());
			if (Root->Tile.IsValid() && Root->Tile->State == EPlanetTerrainTileState::Resident)
			{
				++RootsReady;
			}
		}
	}
	while (Stack.Num() > 0)
	{
		FPlanetTerrainNode* N = Stack.Pop(EAllowShrinking::No);
		if (LandingPrepKeys.Contains(N->Key))
		{
			if (!N->Tile.IsValid() || N->Tile->State != EPlanetTerrainTileState::Resident)
			{
				++Missing;
			}
		}
		for (int32 I = 0; I < 4; ++I)
		{
			if (N->Children[I].IsValid())
			{
				Stack.Add(N->Children[I].Get());
			}
		}
	}

	// Ready when primary + all 6 roots are up, and ≤4 neighborhood tiles still streaming.
	return bPrimaryReady && RootsReady >= 6 && Missing <= 4;
}

bool UPlanetTerrainComponent::EnsureLandingPad(const FVector3d& UnitDir, int32 MinLevel)
{
	if (!HeightField.IsValid() || !Quadtree.IsValid())
	{
		return false;
	}

	const double PadT0 = FPlatformTime::Seconds();
	BuildSharedIndices(PatchResolution);

	TArray<FPlanetTerrainNode*> Nodes;
	CollectLandingPadNodes(UnitDir, MinLevel, Nodes);
	for (FPlanetTerrainNode* Node : Nodes)
	{
		if (!Node || Node->bDisposed || !HeightField.IsValid())
		{
			continue;
		}
		if (Node->Tile.IsValid()
			&& (Node->Tile->State == EPlanetTerrainTileState::Resident
				|| Node->Tile->State == EPlanetTerrainTileState::ReadyForUpload))
		{
			if (Node->Tile->State == EPlanetTerrainTileState::ReadyForUpload
				|| !PatchMeshes.Contains(Node->Key))
			{
				UploadTileMesh(*Node, *Node->Tile);
			}
			continue;
		}

		TSharedPtr<FPlanetTerrainTileData> Tile = MakeShared<FPlanetTerrainTileData>();
		EPlanetTerrainBakeBackend Backend = EPlanetTerrainBakeBackend::DoubleCpu;
		if (!FPlanetTerrainDebug::bForceCpu)
		{
			Backend = FPlanetTerrainGpuBaker::IsGpuComputeAvailable()
				? EPlanetTerrainBakeBackend::GpuCompute
				: EPlanetTerrainBakeBackend::GpuFloatCpu;
		}
		FPlanetTerrainTileBakerEx::Bake(
			*HeightField, Node->Key, Node->U0, Node->V0, Node->U1, Node->V1, PatchResolution, *Tile, Backend);
		Tile->LastUseSeconds = FPlatformTime::Seconds();
		Node->Tile = Tile;
		Node->bGenPending = false;
		Node->DrawMorph = 0.0f;
		Node->TemporalMorph = 0.0f;
		Node->UploadedMorph = 0.0f;
		if (Scheduler.IsValid())
		{
			Scheduler->PutCache(Tile);
		}
		UploadTileMesh(*Node, *Tile);
	}

	const FVector3d Surf = SampleSurfacePoint(UnitDir);
	CameraPlanetLocal = Surf + UnitDir.GetSafeNormal() * 50.0;
	Quadtree->SetFrozen(true);
	SurfaceLodFreezeSeconds = 2.5f;
	LastLandingPadMs = (FPlatformTime::Seconds() - PadT0) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("EnsureLandingPad SYNC cost: %.1f ms (MinLevel=%d) — prefer BeginLandingPrep"),
		LastLandingPadMs, MinLevel);
	return true;
}

bool UPlanetTerrainComponent::SampleDrawnHeight(const FVector3d& UnitDir, double& OutHeightM, float& OutMorph) const
{
	OutMorph = 0.0f;
	if (!Quadtree.IsValid())
	{
		OutHeightM = SampleHeight(UnitDir);
		return false;
	}
	FPlanetTerrainNode* Leaf = Quadtree->FindLeafContaining(UnitDir);
	if (!Leaf || !Leaf->Tile.IsValid())
	{
		OutHeightM = SampleHeight(UnitDir);
		return false;
	}
	OutMorph = FMath::Max(Leaf->DrawMorph, Leaf->TemporalMorph);
	return PlanetTerrainSampleTileHeight(*Leaf->Tile, UnitDir, OutMorph, OutHeightM);
}

void UPlanetTerrainComponent::RegenerateWithSeed(uint64 NewTerrainSeed)
{
	Descriptor.TerrainSeed = NewTerrainSeed;
	++Epoch;
	HeightField = MakeUnique<FPlanetTerrainHeight>(Descriptor);
	for (auto& Pair : PatchMeshes)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyComponent();
		}
	}
	PatchMeshes.Reset();
	PatchMeshList.Reset();
	if (Quadtree.IsValid())
	{
		Quadtree->Reset(Epoch);
	}
	if (Scheduler.IsValid())
	{
		Scheduler->SetPlanet(Descriptor);
		Scheduler->SetEpoch(Epoch);
		Scheduler->CancelStale(Epoch);
	}
	BuildDistantFallback();
}

void UPlanetTerrainComponent::SetLodFrozen(bool bFrozen)
{
	if (Quadtree.IsValid())
	{
		Quadtree->SetFrozen(bFrozen);
	}
}

void UPlanetTerrainComponent::SetForceCpu(bool bForce)
{
	if (Scheduler.IsValid())
	{
		Scheduler->SetForceCpu(bForce);
	}
}

void UPlanetTerrainComponent::SetDebugDraw(bool bSeams, bool bFaces, bool bCollision)
{
	bDebugSeams = bSeams;
	bDebugFaces = bFaces;
	bDebugCollision = bCollision;
}

int32 UPlanetTerrainComponent::GetActiveLeaves() const
{
	return Quadtree.IsValid() ? Quadtree->GetActiveLeafCount() : 0;
}
int32 UPlanetTerrainComponent::GetQueueLength() const
{
	return Scheduler.IsValid() ? Scheduler->GetQueueLength() : 0;
}
int32 UPlanetTerrainComponent::GetWorkerCount() const
{
	return Scheduler.IsValid() ? Scheduler->GetActiveWorkers() : 0;
}
double UPlanetTerrainComponent::GetLastBakeMs() const
{
	return Scheduler.IsValid() ? Scheduler->GetLastBakeMs() : 0.0;
}
int32 UPlanetTerrainComponent::GetSplitCount() const
{
	return Quadtree.IsValid() ? Quadtree->GetSplitCount() : 0;
}
int32 UPlanetTerrainComponent::GetMergeCount() const
{
	return Quadtree.IsValid() ? Quadtree->GetMergeCount() : 0;
}
int32 UPlanetTerrainComponent::GetCacheHits() const
{
	return Scheduler.IsValid() ? Scheduler->GetCacheHits() : 0;
}
int32 UPlanetTerrainComponent::GetCacheMisses() const
{
	return Scheduler.IsValid() ? Scheduler->GetCacheMisses() : 0;
}

FString UPlanetTerrainComponent::GetStreamingStatsLine() const
{
	FString Base = Scheduler.IsValid() ? Scheduler->GetStats().HudLine() : FString();
	Base += FString::Printf(TEXT(" | gpuMeshes=%d upload=%.2fms"), PatchMeshes.Num(), UploadMsAccum);
	return Base;
}
