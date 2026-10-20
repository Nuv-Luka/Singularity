#include "PlanetTerrainNextComponent.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextLog.h"
#include "PlanetTerrainNextMorphMaterial.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "SingularityPlanetTerrainNextMode.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr int32 MaxTreeLevel = 24;
constexpr int32 BuildTimeSamples = 256;
const FLinearColor RegolithColor(0.55f, 0.53f, 0.50f, 1.0f);

UMaterialInterface* LoadPatchMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (!Cached.IsValid())
	{
		Cached = PlanetTerrainNextMorphMaterial::GetOrCreate();
	}
	return Cached.Get();
}

FLinearColor DebugColorFor(EPlanetTerrainNextDebugColor Mode, const FPlanetTerrainNextNodeId& Id, float Morph)
{
	switch (Mode)
	{
	case EPlanetTerrainNextDebugColor::LodLevel:
		return FLinearColor::MakeFromHSV8(static_cast<uint8>((Id.Level * 41) % 256), 200, 235);
	case EPlanetTerrainNextDebugColor::Face:
	{
		static const FLinearColor FaceColors[6] = {
			FLinearColor(0.85f, 0.25f, 0.25f), FLinearColor(0.35f, 0.10f, 0.10f),
			FLinearColor(0.25f, 0.85f, 0.25f), FLinearColor(0.10f, 0.35f, 0.10f),
			FLinearColor(0.25f, 0.35f, 0.90f), FLinearColor(0.10f, 0.12f, 0.40f)};
		return FaceColors[Id.Face % 6];
	}
	case EPlanetTerrainNextDebugColor::Morph:
		// Blue = fine (0), red = coarse (1).
		return FLinearColor(Morph, 0.15f, 1.0f - Morph, 1.0f);
	case EPlanetTerrainNextDebugColor::Regolith:
	default:
		return RegolithColor;
	}
}
} // namespace

UPlanetTerrainNextComponent::UPlanetTerrainNextComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the pawn and camera have moved, so the tree sees this frame's view.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	bTickInEditor = false;
}

UPlanetTerrainNextComponent::~UPlanetTerrainNextComponent() = default;

void UPlanetTerrainNextComponent::InitializeTerrain(const FPlanetTerrainNextDescriptor& InDescriptor)
{
	ShutdownTerrain();

	Descriptor = InDescriptor;
	HeightField = MakeShared<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>(Descriptor);
	Settings = FPlanetTerrainNextRuntimeSettings::Read();
	AppliedDebugColor = Settings.DebugColor;

	GeometricErrorByLevel.SetNum(MaxTreeLevel + 1);
	for (int32 Level = 0; Level <= MaxTreeLevel; ++Level)
	{
		GeometricErrorByLevel[Level] = HeightField->GeometricErrorMeters(
			PlanetTerrainNextPatch::VertexSpacingMeters(Descriptor.RadiusMeters, Level));
	}

	Quadtree.Initialize(
		Descriptor.RadiusMeters,
		HeightField->GetMinHeightMeters(),
		HeightField->GetMaxHeightMeters(),
		GeometricErrorByLevel);

	TileCache = MakeShared<FPlanetTerrainNextTileCache>(Settings.CpuTileCacheSize);
	DiskTileStore = MakeShared<FPlanetTerrainNextDiskTileStore>();
	DiskTileStore->SetEnabled(Settings.bDiskCache);
	DiskTileStore->SetMaxBytes(Settings.DiskCacheMaxMB > 0
		? static_cast<int64>(Settings.DiskCacheMaxMB) * 1024 * 1024
		: 0);
	TileCache->SetDiskStore(DiskTileStore);
	GpuPool = MakeShared<FPlanetTerrainNextGpuSlotPool>();
	GpuPool->Initialize(GetOwner() ? static_cast<UObject*>(GetOwner()) : this, Settings.Lod.MaxNodes, PlanetTerrainNextHeightTile::DefaultResolution);
	Scheduler = MakeShared<FPlanetTerrainNextTileScheduler>();
	Scheduler->Reset(HeightField->GetIdentityHash());
	Scheduler->SetBudgets(Settings.MaxBakeWorkers, Settings.MaxBakeQueue);
	CollisionManager.Reset();
	{
		FPlanetTerrainNextCollisionSettings CollisionSettings;
		CollisionSettings.RadiusM = Settings.CollisionRadiusM;
		CollisionSettings.TargetSpacingM = Settings.CollisionSpacingM;
		CollisionSettings.MaxPatches = Settings.CollisionMaxPatches;
		CollisionSettings.CooksPerFrame = Settings.CollisionCooksPerFrame;
		CollisionSettings.MaxLevel = Settings.Lod.MaxLevel;
		CollisionManager.SetSettings(CollisionSettings);
	}
	FrameCounter = 0;

	BuildTimesMs.Reset();
	BuildTimeCursor = 0;
	bInitialized = true;

	// Coverage before anything else: all six roots resident before the first draw.
	const double Start = FPlatformTime::Seconds();
	for (int32 Face = 0; Face < 6; ++Face)
	{
		BuildNode(FPlanetTerrainNextNodeId(static_cast<uint8>(Face), 0, 0, 0));
	}
	TArray<FPlanetTerrainNextDrawItem> DrawSet;
	Quadtree.CollectDrawSet([this](const FPlanetTerrainNextNodeId& Id) { return Resident.Contains(Id); }, DrawSet);
	FrameStats.bDrawCoverageOk = CheckDrawCoverage(DrawSet);
	FrameStats.Drawn = DrawSet.Num();
	ApplyDrawSet(DrawSet);
	RefreshTilePins(DrawSet);
	ApplyMorph(DrawSet, 1.0f / 60.0f);

	UE_LOG(LogPlanetTerrainNext, Log,
		TEXT("TerrainNext R6 init planet=%lld R=%.1f km heights=[%.0f, %.0f] m roots=%d gpuSlots=%d in %.1f ms GE[0]=%.0f m GE[17]=%.2f m"),
		Descriptor.PlanetId,
		Descriptor.RadiusMeters / 1000.0,
		HeightField->GetMinHeightMeters(),
		HeightField->GetMaxHeightMeters(),
		Resident.Num(),
		GpuPool->GetCapacity(),
		(FPlatformTime::Seconds() - Start) * 1000.0,
		GeometricErrorByLevel[0],
		GeometricErrorByLevel[17]);
}

void UPlanetTerrainNextComponent::ShutdownTerrain()
{
	for (FSlot& Slot : Slots)
	{
		if (Slot.Mesh)
		{
			Slot.Mesh->SetVisibility(false);
		}
		if (Slot.GpuSlot != INDEX_NONE && GpuPool.IsValid())
		{
			GpuPool->Release(Slot.TileId);
			Slot.GpuSlot = INDEX_NONE;
		}
		Slot.bInUse = false;
		Slot.bShown = false;
	}
	FreeSlots.Reset();
	for (int32 Index = Slots.Num() - 1; Index >= 0; --Index)
	{
		FreeSlots.Add(Index);
	}
	Resident.Reset();
	Drawn.Reset();
	HandoffHoldFrames.Reset();
	if (GpuPool.IsValid())
	{
		GpuPool->Shutdown();
	}
	if (Scheduler.IsValid())
	{
		Scheduler->Reset(0);
	}
	for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : CollisionResident)
	{
		if (CollisionMeshes.IsValidIndex(Pair.Value) && CollisionMeshes[Pair.Value])
		{
			CollisionMeshes[Pair.Value]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			CollisionMeshes[Pair.Value]->SetVisibility(false);
			FreeCollisionSlots.AddUnique(Pair.Value);
		}
	}
	CollisionResident.Reset();
	CollisionManager.Reset();
	Scheduler.Reset();
	if (TileCache.IsValid())
	{
		TileCache->SetDiskStore(nullptr);
	}
	TileCache.Reset();
	DiskTileStore.Reset();
	GpuPool.Reset();
	HeightField.Reset();
	bInitialized = false;
}

FPlanetTerrainNextTileId UPlanetTerrainNextComponent::MakeTileId(const FPlanetTerrainNextNodeId& Id) const
{
	return FPlanetTerrainNextTileId::Make(
		Descriptor.PlanetId,
		Descriptor.Seed,
		Descriptor.GeneratorVersion,
		HeightField.IsValid() ? HeightField->GetIdentityHash() : 0,
		Id,
		PlanetTerrainNextHeightTile::DefaultResolution);
}

void UPlanetTerrainNextComponent::OnUnregister()
{
	if (GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()) + 0x7E77A000ull);
	}
	Super::OnUnregister();
}

bool UPlanetTerrainNextComponent::HasFullCoverage() const
{
	if (!bInitialized)
	{
		return false;
	}
	// Prefer the last tick's draw-coverage flag (logical CollectDraw partition).
	if (FrameStats.Drawn > 0)
	{
		return FrameStats.bDrawCoverageOk;
	}
	for (int32 Face = 0; Face < 6; ++Face)
	{
		if (!Resident.Contains(FPlanetTerrainNextNodeId(static_cast<uint8>(Face), 0, 0, 0)))
		{
			return false;
		}
	}
	return true;
}

int32 UPlanetTerrainNextComponent::ApplyLodHandoff(
	const TSet<FPlanetTerrainNextNodeId>& PreviousDrawn,
	TSet<FPlanetTerrainNextNodeId>& InOutNext,
	TMap<FPlanetTerrainNextNodeId, int32>& InOutHoldFrames,
	int32 HoldFrames,
	TFunctionRef<bool(const FPlanetTerrainNextNodeId&)> IsParentResident,
	TFunctionRef<bool(const FPlanetTerrainNextNodeId&)> IsChildShown)
{
	const int32 HoldTotal = FMath::Max(HoldFrames, 1);
	int32 Kept = 0;
	TSet<FPlanetTerrainNextNodeId> ActiveParents;
	for (const FPlanetTerrainNextNodeId& Prev : PreviousDrawn)
	{
		if (InOutNext.Contains(Prev))
		{
			InOutHoldFrames.Remove(Prev);
			continue;
		}
		bool bAllFourInNext = true;
		for (int32 Child = 0; Child < 4; ++Child)
		{
			if (!InOutNext.Contains(Prev.Child(Child)))
			{
				bAllFourInNext = false;
				break;
			}
		}
		if (!bAllFourInNext)
		{
			InOutHoldFrames.Remove(Prev);
			continue;
		}

		// Already shown children finishing a prior hold: allow commit once hold hits 0.
		bool bAllFourShown = true;
		for (int32 Child = 0; Child < 4; ++Child)
		{
			if (!IsChildShown(Prev.Child(Child)))
			{
				bAllFourShown = false;
				break;
			}
		}

		int32* Remaining = InOutHoldFrames.Find(Prev);
		if (!Remaining)
		{
			if (bAllFourShown)
			{
				// Children already visible from a previous commit — no hold.
				continue;
			}
			if (!IsParentResident(Prev))
			{
				continue;
			}
			Remaining = &InOutHoldFrames.Add(Prev, HoldTotal);
		}

		if (*Remaining > 0)
		{
			for (int32 Child = 0; Child < 4; ++Child)
			{
				InOutNext.Remove(Prev.Child(Child));
			}
			InOutNext.Add(Prev);
			--(*Remaining);
			ActiveParents.Add(Prev);
			++Kept;
		}
		else
		{
			// Commit children; clear hold.
			InOutHoldFrames.Remove(Prev);
		}
	}

	TArray<FPlanetTerrainNextNodeId> Stale;
	for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : InOutHoldFrames)
	{
		if (!ActiveParents.Contains(Pair.Key) && !PreviousDrawn.Contains(Pair.Key))
		{
			Stale.Add(Pair.Key);
		}
	}
	for (const FPlanetTerrainNextNodeId& Id : Stale)
	{
		InOutHoldFrames.Remove(Id);
	}
	return Kept;
}

bool UPlanetTerrainNextComponent::CheckDrawCoverage(const TArray<FPlanetTerrainNextDrawItem>& DrawSet) const
{
	double Area[6] = {0, 0, 0, 0, 0, 0};
	TSet<FPlanetTerrainNextNodeId> DrawnIds;
	DrawnIds.Reserve(DrawSet.Num());
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		if (!Resident.Contains(Item.Id))
		{
			return false;
		}
		bool bAlready = false;
		DrawnIds.Add(Item.Id, &bAlready);
		if (bAlready)
		{
			return false;
		}
		Area[Item.Id.Face] += Item.Id.Size() * Item.Id.Size();
	}
	for (int32 Face = 0; Face < 6; ++Face)
	{
		if (!FMath::IsNearlyEqual(Area[Face], 1.0, 1.0e-9))
		{
			return false;
		}
	}
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		for (FPlanetTerrainNextNodeId Up = Item.Id; Up.Level > 0;)
		{
			Up = FPlanetTerrainNextNodeId(Up.Face, static_cast<uint8>(Up.Level - 1), Up.X >> 1, Up.Y >> 1);
			if (DrawnIds.Contains(Up))
			{
				return false;
			}
		}
	}
	return true;
}

int32 UPlanetTerrainNextComponent::AcquireSlot()
{
	if (FreeSlots.Num() > 0)
	{
		return FreeSlots.Pop(EAllowShrinking::No);
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return INDEX_NONE;
	}

	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
	Mesh->SetupAttachment(this);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false); // Enabled when drawn if CastShadow CVar is on.
	// Patches at mixed LOD must not occlude each other (false holes / checkerboard).
	Mesh->bUseAsOccluder = false;
	Mesh->bUseAsyncCooking = true;
	Mesh->SetBoundsScale(1.0f);
	Mesh->SetVisibility(false);
	Mesh->RegisterComponent();

	UMaterialInstanceDynamic* Material = nullptr;
	if (UMaterialInterface* Base = LoadPatchMaterial())
	{
		Material = UMaterialInstanceDynamic::Create(Base, Mesh);
	}

	FSlot Slot;
	Slot.Mesh = Mesh;
	Slot.Material = Material;
	SlotMeshes.Add(Mesh);
	SlotMaterials.Add(Material);
	return Slots.Add(Slot);
}

void UPlanetTerrainNextComponent::ReleaseNode(const FPlanetTerrainNextNodeId& Id)
{
	int32 SlotIndex = INDEX_NONE;
	if (!Resident.RemoveAndCopyValue(Id, SlotIndex))
	{
		return;
	}
	FSlot& Slot = Slots[SlotIndex];
	if (Slot.bShown && Slot.Mesh)
	{
		Slot.Mesh->SetVisibility(false);
	}
	if (Slot.GpuSlot != INDEX_NONE)
	{
		GpuPool->Release(Slot.TileId);
		Slot.GpuSlot = INDEX_NONE;
	}
	Slot.bShown = false;
	Slot.bInUse = false;
	Drawn.Remove(Id);
	FreeSlots.Add(SlotIndex);
}

bool UPlanetTerrainNextComponent::BuildNode(const FPlanetTerrainNextNodeId& Id)
{
	if (!HeightField.IsValid() || !TileCache.IsValid() || !GpuPool.IsValid() || Resident.Contains(Id))
	{
		return false;
	}

	const FPlanetTerrainNextTileId TileId = MakeTileId(Id);
	auto IsPinned = [this](const FPlanetTerrainNextTileId& Key)
	{
		return GpuPool->IsPinned(Key);
	};

	// Prefer async-baked cache hits. Sync GetOrBake is the coverage fallback so roots
	// and newly visible parents never wait on a full worker queue.
	const FPlanetTerrainNextHeightTile* Tile = TileCache->Find(TileId);
	if (Tile)
	{
		TileCache->Touch(TileId, FrameCounter);
	}
	else
	{
		Tile = TileCache->GetOrBake(*HeightField, TileId, FrameCounter, IsPinned);
	}
	if (!Tile)
	{
		return false;
	}

	const int32 GpuSlot = GpuPool->Allocate(TileId);
	if (GpuSlot == INDEX_NONE)
	{
		// Pool full of pinned slots — keep the parent covering (spec).
		return false;
	}
	// Atlas upload is best-effort for R4; morph mesh is CPU/UV-packed and does not need it.
	if (!GpuPool->Upload(GpuSlot, *Tile))
	{
		UE_LOG(LogPlanetTerrainNext, Warning, TEXT("BuildNode: GPU upload failed for %s — continuing with CPU mesh"), *Id.ToString());
	}

	const int32 SlotIndex = AcquireSlot();
	if (SlotIndex == INDEX_NONE)
	{
		GpuPool->Release(TileId);
		return false;
	}

	const double Start = FPlatformTime::Seconds();
	const double SkirtDepth = PlanetTerrainNextPatch::RecommendedSkirtDepthM(
		GeometricErrorByLevel[FMath::Clamp(Id.Level - 2, 0, MaxTreeLevel)],
		PlanetTerrainNextPatch::VertexSpacingMeters(Descriptor.RadiusMeters, Id.Level));

	FPlanetTerrainNextPatchData Patch;
	PlanetTerrainNextPatch::BuildFromTile(*HeightField, *Tile, SkirtDepth, Patch);

	FSlot& Slot = Slots[SlotIndex];
	Slot.Id = Id;
	Slot.TileId = TileId;
	Slot.GpuSlot = GpuSlot;
	Slot.bInUse = true;
	Slot.Morph = 1.0f; // Parent-compatible until reveal + smooth open toward PE target.
	Slot.MorphReveal = 0.0f;
	Slot.MaxAbsCoarseDeltaCm = Patch.MaxAbsCoarseDeltaCm;
	UploadPatch(Slot, Patch);
	if (Slot.Material)
	{
		Slot.Material->SetScalarParameterValue(TEXT("Morph"), Slot.Morph);
	}
	ApplyDebugColor(Slot);
	Resident.Add(Id, SlotIndex);
	Quadtree.SetNodeHeightRange(Id, Patch.MinHeightM, Patch.MaxHeightM);
	GpuPool->Pin(TileId, EPlanetTerrainNextPinReason::Coverage);

	RecordBuildTime((FPlatformTime::Seconds() - Start) * 1000.0);
	return true;
}

void UPlanetTerrainNextComponent::UploadPatch(FSlot& Slot, const FPlanetTerrainNextPatchData& Patch)
{
	TArray<FProcMeshTangent> Tangents;
	Tangents.Reserve(Patch.Tangents.Num());
	for (const FVector& Tangent : Patch.Tangents)
	{
		Tangents.Add(FProcMeshTangent(Tangent, false));
	}

	Slot.Mesh->SetRelativeLocation(Patch.OriginM * PlanetTerrainNext::CmPerMeter);

	if (!Slot.bHasSection)
	{
		Slot.Mesh->CreateMeshSection_LinearColor(
			0,
			Patch.VerticesCm,
			PlanetTerrainNextPatch::Indices(),
			Patch.Normals,
			Patch.UVs,
			Patch.UV1,
			Patch.UV2,
			TArray<FVector2D>(),
			Patch.VertexColors,
			Tangents,
			false);
		if (Slot.Material)
		{
			Slot.Mesh->SetMaterial(0, Slot.Material);
		}
		Slot.bHasSection = true;
	}
	else
	{
		Slot.Mesh->UpdateMeshSection_LinearColor(
			0,
			Patch.VerticesCm,
			Patch.Normals,
			Patch.UVs,
			Patch.UV1,
			Patch.UV2,
			TArray<FVector2D>(),
			Patch.VertexColors,
			Tangents);
	}

	// Expand bounds so WPO out to the coarse surface cannot cull the patch.
	double MaxRadiusCm = 1.0;
	for (const FVector& V : Patch.VerticesCm)
	{
		MaxRadiusCm = FMath::Max(MaxRadiusCm, V.Size());
	}
	for (const FVector& V : Patch.CoarseVerticesCm)
	{
		MaxRadiusCm = FMath::Max(MaxRadiusCm, V.Size());
	}
	const double FineRadius = FMath::Max(1.0, Slot.Mesh->CalcLocalBounds().SphereRadius);
	Slot.Mesh->SetBoundsScale(static_cast<float>(MaxRadiusCm / FineRadius * 1.05));
}

void UPlanetTerrainNextComponent::ApplyDrawSet(const TArray<FPlanetTerrainNextDrawItem>& DrawSet)
{
	TSet<FPlanetTerrainNextNodeId> Next;
	Next.Reserve(DrawSet.Num() + 8);
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		Next.Add(Item.Id);
	}

	// Hold parent for MorphHandoffFrames while children stay stripped (no co-draw / pop).
	FrameStats.HandoffParentsKept = ApplyLodHandoff(
		Drawn,
		Next,
		HandoffHoldFrames,
		Settings.MorphHandoffFrames,
		[this](const FPlanetTerrainNextNodeId& Id) { return Resident.Contains(Id); },
		[this](const FPlanetTerrainNextNodeId& Id)
		{
			if (const int32* SlotIndex = Resident.Find(Id))
			{
				return Slots[*SlotIndex].bShown;
			}
			return false;
		});

	int32 Changes = 0;
	// Show first, then hide, so a frame never has fewer patches than the coverage requires.
	for (const FPlanetTerrainNextNodeId& Id : Next)
	{
		if (const int32* SlotIndex = Resident.Find(Id))
		{
			FSlot& Slot = Slots[*SlotIndex];
			if (!Slot.bShown)
			{
				Slot.Mesh->SetVisibility(true);
				Slot.Mesh->SetCastShadow(Settings.bCastShadow);
				Slot.bShown = true;
				++Changes;
			}
			else
			{
				Slot.Mesh->SetCastShadow(Settings.bCastShadow);
			}
		}
	}
	for (const FPlanetTerrainNextNodeId& Id : Drawn)
	{
		if (!Next.Contains(Id))
		{
			if (const int32* SlotIndex = Resident.Find(Id))
			{
				FSlot& Slot = Slots[*SlotIndex];
				if (Slot.bShown)
				{
					Slot.Mesh->SetVisibility(false);
					Slot.Mesh->SetCastShadow(false);
					Slot.bShown = false;
					++Changes;
				}
			}
		}
	}
	Drawn = MoveTemp(Next);
	FrameStats.VisibilityChanges = Changes;
}

void UPlanetTerrainNextComponent::RefreshTilePins(const TArray<FPlanetTerrainNextDrawItem>& DrawSet)
{
	GpuPool->ClearPins();
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		const FPlanetTerrainNextTileId TileId = MakeTileId(Item.Id);
		GpuPool->Pin(TileId, EPlanetTerrainNextPinReason::Draw);
		GpuPool->TryGet(TileId);
		if (Item.Id.Level > 0)
		{
			const FPlanetTerrainNextNodeId Parent(
				Item.Id.Face,
				static_cast<uint8>(Item.Id.Level - 1),
				Item.Id.X >> 1,
				Item.Id.Y >> 1);
			GpuPool->Pin(MakeTileId(Parent), EPlanetTerrainNextPinReason::Coverage);
		}
	}
	for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : Resident)
	{
		GpuPool->Pin(MakeTileId(Pair.Key), EPlanetTerrainNextPinReason::Coverage);
	}
	for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : CollisionResident)
	{
		GpuPool->Pin(MakeTileId(Pair.Key), EPlanetTerrainNextPinReason::Collision);
	}
}

void UPlanetTerrainNextComponent::ApplyMorph(const TArray<FPlanetTerrainNextDrawItem>& DrawSet, float DeltaSeconds)
{
	const float Dt = FMath::Max(DeltaSeconds, 0.0f);
	int32 Updates = 0;
	double MorphSum = 0.0;
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		const int32* SlotIndex = Resident.Find(Item.Id);
		if (!SlotIndex)
		{
			continue;
		}
		FSlot& Slot = Slots[*SlotIndex];
		float Target = 0.0f;
		if (!Settings.bMorphEnabled)
		{
			Target = 0.0f;
			Slot.MorphReveal = 1.0f;
		}
		else if (Settings.MorphForce >= 0.0f)
		{
			Target = FMath::Clamp(Settings.MorphForce, 0.0f, 1.0f);
			Slot.MorphReveal = 1.0f;
		}
		else
		{
			const float PeTarget = PlanetTerrainNextPatch::MorphFromPixelError(
				Item.PixelError, Settings.Lod.TargetPixelError, Settings.MorphEndFactor);
			// New children: MorphReveal 0→1 blends parent-compatible (1) into PE target.
			if (Settings.MorphRevealSeconds <= KINDA_SMALL_NUMBER)
			{
				Slot.MorphReveal = 1.0f;
			}
			else
			{
				Slot.MorphReveal = FMath::Min(1.0f, Slot.MorphReveal + Dt / Settings.MorphRevealSeconds);
			}
			Target = FMath::Lerp(1.0f, PeTarget, Slot.MorphReveal);
		}

		float Morph = Target;
		if (Settings.MorphSmoothSeconds > KINDA_SMALL_NUMBER && Settings.bMorphEnabled && Settings.MorphForce < 0.0f)
		{
			const float Alpha = 1.0f - FMath::Exp(-Dt / Settings.MorphSmoothSeconds);
			Morph = FMath::Lerp(Slot.Morph, Target, Alpha);
		}

		if (Slot.Material && !FMath::IsNearlyEqual(Slot.Morph, Morph, 1.0e-4f))
		{
			Slot.Material->SetScalarParameterValue(TEXT("Morph"), Morph);
			++Updates;
		}
		Slot.Morph = Morph;
		MorphSum += Morph;

		if (Settings.DebugColor == EPlanetTerrainNextDebugColor::Morph)
		{
			ApplyDebugColor(Slot);
		}
	}
	FrameStats.MorphUniformUpdates = Updates;
	FrameStats.MeanMorph = DrawSet.Num() > 0 ? static_cast<float>(MorphSum / DrawSet.Num()) : 0.0f;
}

void UPlanetTerrainNextComponent::HideAll()
{
	for (FSlot& Slot : Slots)
	{
		if (Slot.Mesh)
		{
			Slot.Mesh->SetVisibility(false);
			Slot.Mesh->SetCastShadow(false);
		}
		Slot.bShown = false;
	}
	Drawn.Reset();
	HandoffHoldFrames.Reset();
	for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : CollisionResident)
	{
		if (CollisionMeshes.IsValidIndex(Pair.Value) && CollisionMeshes[Pair.Value])
		{
			CollisionMeshes[Pair.Value]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void UPlanetTerrainNextComponent::ApplyDebugColor(FSlot& Slot) const
{
	if (Slot.Material)
	{
		Slot.Material->SetVectorParameterValue(TEXT("Color"), DebugColorFor(Settings.DebugColor, Slot.Id, Slot.Morph));
	}
}

void UPlanetTerrainNextComponent::RefreshDebugColors()
{
	if (AppliedDebugColor == Settings.DebugColor)
	{
		return;
	}
	AppliedDebugColor = Settings.DebugColor;
	for (FSlot& Slot : Slots)
	{
		if (Slot.bInUse)
		{
			ApplyDebugColor(Slot);
		}
	}
}

bool UPlanetTerrainNextComponent::GetView(FPlanetTerrainNextView& OutView) const
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
	if (!Camera)
	{
		return false;
	}

	const FTransform& Planet = GetComponentTransform();
	const FVector CameraUu = Camera->GetCameraLocation();
	// Double subtraction before anything is narrowed (DSE ToCameraRelative).
	OutView.CameraM = FVector3d(Planet.InverseTransformPositionNoScale(CameraUu)) / PlanetTerrainNext::CmPerMeter;
	OutView.Forward = FVector3d(Planet.InverseTransformVectorNoScale(Camera->GetCameraRotation().Vector())).GetSafeNormal();
	OutView.TimeSeconds = World->GetRealTimeSeconds();

	FVector2D ViewportSize(1920.0, 1080.0);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	const double Width = FMath::Max(1.0, ViewportSize.X);
	const double Height = FMath::Max(1.0, ViewportSize.Y);

	// UE camera FOV is horizontal.
	const double HalfH = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(Camera->GetFOVAngle()), 1.0, 170.0)) * 0.5;
	const double TanH = FMath::Tan(HalfH);
	const double TanV = TanH * Height / Width;
	OutView.ProjectionScalePx = Height / (2.0 * TanV);
	OutView.HalfDiagonalFovRad = FMath::Atan(FMath::Sqrt(TanH * TanH + TanV * TanV));
	return true;
}

void UPlanetTerrainNextComponent::RecordBuildTime(double Milliseconds)
{
	FrameStats.LastBuildMs = Milliseconds;
	if (BuildTimesMs.Num() < BuildTimeSamples)
	{
		BuildTimesMs.Add(Milliseconds);
	}
	else
	{
		BuildTimesMs[BuildTimeCursor] = Milliseconds;
		BuildTimeCursor = (BuildTimeCursor + 1) % BuildTimeSamples;
	}
}

bool UPlanetTerrainNextComponent::TrySampleSurfaceHeightM(const FVector& WorldPointUu, double& OutHeightM) const
{
	OutHeightM = 0.0;
	if (!HeightField.IsValid())
	{
		return false;
	}
	const FVector3d LocalM = FVector3d(GetComponentTransform().InverseTransformPositionNoScale(WorldPointUu)) / PlanetTerrainNext::CmPerMeter;
	const double Distance = LocalM.Length();
	if (Distance <= 0.0)
	{
		return false;
	}
	return CollisionManager.TrySampleHeightM(*HeightField, LocalM / Distance, OutHeightM);
}

double UPlanetTerrainNextComponent::GetAltitudeMeters(const FVector& WorldPointUu) const
{
	const FVector3d LocalM = FVector3d(GetComponentTransform().InverseTransformPositionNoScale(WorldPointUu)) / PlanetTerrainNext::CmPerMeter;
	if (!HeightField.IsValid())
	{
		return LocalM.Length() - Descriptor.RadiusMeters;
	}
	return CollisionManager.SampleAltitudeM(*HeightField, LocalM);
}

void UPlanetTerrainNextComponent::ReleaseCollisionNode(const FPlanetTerrainNextNodeId& Id)
{
	const int32* SlotIndex = CollisionResident.Find(Id);
	if (!SlotIndex)
	{
		return;
	}
	if (CollisionMeshes.IsValidIndex(*SlotIndex) && CollisionMeshes[*SlotIndex])
	{
		UProceduralMeshComponent* Mesh = CollisionMeshes[*SlotIndex];
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->ClearAllMeshSections();
		Mesh->SetVisibility(false);
		FreeCollisionSlots.AddUnique(*SlotIndex);
	}
	if (GpuPool.IsValid())
	{
		GpuPool->Unpin(MakeTileId(Id), EPlanetTerrainNextPinReason::Collision);
	}
	CollisionResident.Remove(Id);
}

bool UPlanetTerrainNextComponent::CookCollisionNode(const FPlanetTerrainNextNodeId& Id)
{
	if (!HeightField.IsValid() || !TileCache.IsValid() || CollisionResident.Contains(Id))
	{
		return false;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	const FPlanetTerrainNextTileId TileId = MakeTileId(Id);
	auto IsPinned = [this](const FPlanetTerrainNextTileId& Key) { return GpuPool.IsValid() && GpuPool->IsPinned(Key); };
	const FPlanetTerrainNextHeightTile* Tile = TileCache->Find(TileId);
	if (!Tile)
	{
		Tile = TileCache->GetOrBake(*HeightField, TileId, FrameCounter, IsPinned);
	}
	if (!Tile)
	{
		return false;
	}

	int32 SlotIndex = INDEX_NONE;
	if (FreeCollisionSlots.Num() > 0)
	{
		SlotIndex = FreeCollisionSlots.Pop(EAllowShrinking::No);
	}
	else
	{
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
		Mesh->SetupAttachment(this);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(true);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCastShadow(false);
		Mesh->SetVisibility(false);
		Mesh->bUseAsyncCooking = true;
		Mesh->RegisterComponent();
		SlotIndex = CollisionMeshes.Add(Mesh);
	}

	UProceduralMeshComponent* Mesh = CollisionMeshes[SlotIndex];
	const double Start = FPlatformTime::Seconds();
	const double SkirtDepth = PlanetTerrainNextPatch::RecommendedSkirtDepthM(
		GeometricErrorByLevel[FMath::Clamp(Id.Level - 2, 0, MaxTreeLevel)],
		PlanetTerrainNextPatch::VertexSpacingMeters(Descriptor.RadiusMeters, Id.Level));
	FPlanetTerrainNextPatchData Patch;
	PlanetTerrainNextPatch::BuildFromTile(*HeightField, *Tile, SkirtDepth, Patch);

	TArray<FProcMeshTangent> Tangents;
	Tangents.Reserve(Patch.Tangents.Num());
	for (const FVector& Tangent : Patch.Tangents)
	{
		Tangents.Add(FProcMeshTangent(Tangent, false));
	}

	Mesh->SetRelativeLocation(Patch.OriginM * PlanetTerrainNext::CmPerMeter);
	Mesh->ClearAllMeshSections();
	Mesh->CreateMeshSection_LinearColor(
		0,
		Patch.VerticesCm,
		PlanetTerrainNextPatch::Indices(),
		Patch.Normals,
		Patch.UVs,
		Patch.UV1,
		Patch.UV2,
		TArray<FVector2D>(),
		Patch.VertexColors,
		Tangents,
		true); // complex-as-simple collision
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->SetVisibility(false);
	Mesh->SetCastShadow(false);

	CollisionResident.Add(Id, SlotIndex);
	CollisionManager.NotifyCooked(Id, *Tile, FrameCounter);
	if (GpuPool.IsValid())
	{
		GpuPool->Pin(TileId, EPlanetTerrainNextPinReason::Collision);
	}
	CollisionManager.RecordCookTime((FPlatformTime::Seconds() - Start) * 1000.0);
	return true;
}

void UPlanetTerrainNextComponent::UpdateCollision(const FPlanetTerrainNextView& View)
{
	FPlanetTerrainNextCollisionSettings CollisionSettings;
	CollisionSettings.RadiusM = Settings.CollisionRadiusM;
	CollisionSettings.TargetSpacingM = Settings.CollisionSpacingM;
	CollisionSettings.MaxPatches = Settings.CollisionMaxPatches;
	CollisionSettings.CooksPerFrame = Settings.CollisionCooksPerFrame;
	CollisionSettings.MaxLevel = Settings.Lod.MaxLevel;
	CollisionManager.SetSettings(CollisionSettings);

	// Focus on the surface under the camera; short predicted path along view forward.
	const FVector3d FocusDir = View.CameraM.GetSafeNormal();
	double SurfaceH = 0.0;
	if (HeightField.IsValid())
	{
		CollisionManager.TrySampleHeightM(*HeightField, FocusDir, SurfaceH);
	}
	const FVector3d FocusM = FocusDir * (Descriptor.RadiusMeters + SurfaceH);

	TArray<FVector3d> Path;
	Path.Reserve(CollisionSettings.PathSamples);
	for (int32 Index = 1; Index <= CollisionSettings.PathSamples; ++Index)
	{
		const double AheadM = Settings.CollisionRadiusM * (static_cast<double>(Index) / CollisionSettings.PathSamples);
		const FVector3d Step = (FocusM + View.Forward.GetSafeNormal() * AheadM).GetSafeNormal()
			* (Descriptor.RadiusMeters + SurfaceH);
		Path.Add(Step);
	}

	CollisionManager.UpdateDesired(FocusM, Path, Descriptor.RadiusMeters);

	int32 Cooked = 0;
	for (const FPlanetTerrainNextNodeId& Id : CollisionManager.GetDesired())
	{
		if (Cooked >= Settings.CollisionCooksPerFrame)
		{
			break;
		}
		if (CollisionResident.Contains(Id))
		{
			continue;
		}
		if (CookCollisionNode(Id))
		{
			++Cooked;
		}
	}

	TArray<FPlanetTerrainNextNodeId> Released;
	CollisionManager.TickResidency(FrameCounter, Released);
	for (const FPlanetTerrainNextNodeId& Id : Released)
	{
		ReleaseCollisionNode(Id);
	}
	CollisionManager.SetCookedThisFrame(Cooked);
}

void UPlanetTerrainNextComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bInitialized)
	{
		return;
	}

	if (!SingularityTerrainRendererIsNext())
	{
		if (!bSuspended)
		{
			HideAll();
			if (SuspendedProxy.IsValid())
			{
				SuspendedProxy->SetVisibility(true);
			}
			bSuspended = true;
		}
		return;
	}
	if (bSuspended || (SuspendedProxy.IsValid() && SuspendedProxy->IsVisible()))
	{
		if (SuspendedProxy.IsValid())
		{
			SuspendedProxy->SetVisibility(false);
		}
		bSuspended = false;
	}

	const double TickStart = FPlatformTime::Seconds();
	Settings = FPlanetTerrainNextRuntimeSettings::Read();
	if (!TileCache.IsValid() || !GpuPool.IsValid() || !Scheduler.IsValid() || !HeightField.IsValid())
	{
		return;
	}
	TileCache->SetMaxEntries(Settings.CpuTileCacheSize);
	if (DiskTileStore.IsValid())
	{
		DiskTileStore->SetEnabled(Settings.bDiskCache);
		DiskTileStore->SetMaxBytes(Settings.DiskCacheMaxMB > 0
			? static_cast<int64>(Settings.DiskCacheMaxMB) * 1024 * 1024
			: 0);
	}
	Scheduler->SetBudgets(Settings.MaxBakeWorkers, Settings.MaxBakeQueue);
	++FrameCounter;
	GpuPool->BeginFrame(FrameCounter);
	TileCache->BeginFrame();
	Scheduler->BeginFrame(FrameCounter);
	RefreshDebugColors();

	FPlanetTerrainNextView View;
	if (!GetView(View))
	{
		return;
	}

	auto IsReady = [this](const FPlanetTerrainNextNodeId& Id)
	{
		return Resident.Contains(Id);
	};

	const double TreeStart = FPlatformTime::Seconds();
	TArray<FPlanetTerrainNextNodeId> Released;
	Quadtree.Update(View, Settings.Lod, IsReady, Released);
	for (const FPlanetTerrainNextNodeId& Id : Released)
	{
		ReleaseNode(Id);
	}
	TArray<FPlanetTerrainNextPendingBuild> Pending;
	Quadtree.CollectPending(IsReady, Pending);
	FrameStats.TreeMs = (FPlatformTime::Seconds() - TreeStart) * 1000.0;

	const double SchedStart = FPlatformTime::Seconds();
	TArray<FPlanetTerrainNextTileRequest> Desired;
	Planner.Plan(
		Pending,
		[this](const FPlanetTerrainNextNodeId& NodeId) { return MakeTileId(NodeId); },
		Settings.Lod.TargetPixelError,
		FrameCounter,
		Settings.PrefetchPerFrame,
		Desired);
	Scheduler->SetDesired(Desired);
	Scheduler->Pump(HeightField);
	auto IsPinned = [this](const FPlanetTerrainNextTileId& Key)
	{
		return GpuPool->IsPinned(Key);
	};
	Scheduler->Drain(*TileCache, FrameCounter, IsPinned);
	FrameStats.ScheduleMs = (FPlatformTime::Seconds() - SchedStart) * 1000.0;

	int32 Built = 0;
	const double MeshStart = FPlatformTime::Seconds();
	if (!Settings.bPauseBuilds)
	{
		const double BuildStart = FPlatformTime::Seconds();
		for (const FPlanetTerrainNextPendingBuild& Request : Pending)
		{
			if (Built >= Settings.TileUploadsPerFrame)
			{
				break;
			}
			if (Built > 0 && (FPlatformTime::Seconds() - BuildStart) * 1000.0 >= Settings.BuildBudgetMs)
			{
				break;
			}
			if (Settings.DenyChildIndex >= 0 && Request.Id.Level > 0 && Request.Id.ChildIndex() == Settings.DenyChildIndex)
			{
				continue;
			}
			if (BuildNode(Request.Id))
			{
				++Built;
			}
		}
	}
	FrameStats.MeshBuildMs = (FPlatformTime::Seconds() - MeshStart) * 1000.0;

	TArray<FPlanetTerrainNextDrawItem> DrawSet;
	Quadtree.CollectDrawSet(IsReady, DrawSet);
	FrameStats.bDrawCoverageOk = CheckDrawCoverage(DrawSet);
	ApplyDrawSet(DrawSet);
	const double CollStart = FPlatformTime::Seconds();
	UpdateCollision(View);
	FrameStats.CollisionMs = (FPlatformTime::Seconds() - CollStart) * 1000.0;
	RefreshTilePins(DrawSet);
	ApplyMorph(DrawSet, DeltaTime);

	const FPlanetTerrainNextQuadtreeStats& TreeStats = Quadtree.GetStats();
	FrameStats.Nodes = TreeStats.Nodes;
	FrameStats.Splits = TreeStats.Splits;
	FrameStats.Merges = TreeStats.Merges;
	FrameStats.BudgetBlocked = TreeStats.BudgetBlocked;
	FrameStats.Resident = Resident.Num();
	FrameStats.Drawn = DrawSet.Num();
	FrameStats.Pending = FMath::Max(0, Pending.Num() - Built);
	FrameStats.BuiltThisFrame = Built;
	FrameStats.Slots = Slots.Num();
	FrameStats.MaxDrawnLevel = 0;
	FrameStats.DrawnL0 = 0;
	FrameStats.DrawnL1 = 0;
	FrameStats.DrawnL2 = 0;
	FrameStats.DrawnL3Plus = 0;
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		FrameStats.MaxDrawnLevel = FMath::Max(FrameStats.MaxDrawnLevel, static_cast<int32>(Item.Id.Level));
		if (Item.Id.Level == 0)
		{
			++FrameStats.DrawnL0;
		}
		else if (Item.Id.Level == 1)
		{
			++FrameStats.DrawnL1;
		}
		else if (Item.Id.Level == 2)
		{
			++FrameStats.DrawnL2;
		}
		else
		{
			++FrameStats.DrawnL3Plus;
		}
	}
	const FPlanetTerrainNextTileCacheStats& CacheStats = TileCache->GetStats();
	const FPlanetTerrainNextGpuPoolStats& GpuStats = GpuPool->GetStats();
	FrameStats.TileCacheHits = CacheStats.Hits;
	FrameStats.TileCacheMisses = CacheStats.Misses;
	FrameStats.TileBakes = CacheStats.BakesThisFrame;
	FrameStats.DiskHits = CacheStats.DiskHits;
	FrameStats.DiskMisses = CacheStats.DiskMisses;
	FrameStats.DiskWrites = CacheStats.DiskWrites;
	FrameStats.TileBakeP95Ms = CacheStats.P95BakeMs;
	FrameStats.GpuResident = GpuStats.Resident;
	FrameStats.GpuUploads = GpuStats.UploadsThisFrame;
	FrameStats.GpuEvictions = GpuStats.Evictions;
	FrameStats.GpuAllocFailed = GpuStats.AllocFailed;
	const FPlanetTerrainNextSchedulerStats& SchedStats = Scheduler->GetStats();
	FrameStats.SchedulerQueued = SchedStats.Queued;
	FrameStats.SchedulerBaking = SchedStats.Baking;
	FrameStats.SchedulerStarted = SchedStats.StartedThisFrame;
	FrameStats.SchedulerCompleted = SchedStats.CompletedThisFrame;
	FrameStats.SchedulerCancelled = SchedStats.CancelledThisFrame;
	FrameStats.SchedulerStale = SchedStats.StaleCompletions;
	if (SchedStats.P95BakeMs > 0.0)
	{
		FrameStats.TileBakeP95Ms = SchedStats.P95BakeMs;
	}
	const FPlanetTerrainNextCollisionStats& CollStats = CollisionManager.GetStats();
	FrameStats.CollisionDesired = CollStats.Desired;
	FrameStats.CollisionResident = CollStats.Resident;
	FrameStats.CollisionCooked = CollStats.CookedThisFrame;
	FrameStats.CollisionLevel = CollStats.Level;
	FrameStats.CollisionCookP95Ms = CollStats.P95CookMs;
	if (BuildTimesMs.Num() > 0)
	{
		TArray<double> Sorted = BuildTimesMs;
		Sorted.Sort();
		FrameStats.P95BuildMs = Sorted[FMath::Clamp(FMath::CeilToInt(0.95 * Sorted.Num()) - 1, 0, Sorted.Num() - 1)];
	}
	FrameStats.AltitudeM = CollisionManager.SampleAltitudeM(*HeightField, View.CameraM);
	FrameStats.UpdateMs = (FPlatformTime::Seconds() - TickStart) * 1000.0;

	if (Settings.bShowStats)
	{
		PrintStats();
	}
}

void UPlanetTerrainNextComponent::PrintStats() const
{
	if (!GEngine)
	{
		return;
	}
	const FPlanetTerrainNextFrameStats& S = FrameStats;
	const FString AltitudeText = FMath::Abs(S.AltitudeM) >= 10000.0
		? FString::Printf(TEXT("%.1f km"), S.AltitudeM / 1000.0)
		: FString::Printf(TEXT("%.1f m"), S.AltitudeM);
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(GetUniqueID()) + 0x7E77A000ull,
		0.0f,
		HasFullCoverage() ? FColor(140, 230, 140) : FColor::Red,
		FString::Printf(
			TEXT("TerrainNext R8 | alt %s | drawn %d L%d (L0:%d L1:%d L2:%d L3+:%d) handoff %d | cov %s | disk %d/%d w%d | morph %.2f | coll %d/%d | tree %.2f sch %.2f mesh %.2f coll %.2f | tick %.2f ms"),
			*AltitudeText, S.Drawn, S.MaxDrawnLevel,
			S.DrawnL0, S.DrawnL1, S.DrawnL2, S.DrawnL3Plus, S.HandoffParentsKept,
			S.bDrawCoverageOk ? TEXT("ok") : TEXT("HOLE"),
			S.DiskHits, S.DiskMisses, S.DiskWrites,
			S.MeanMorph,
			S.CollisionResident, S.CollisionDesired,
			S.TreeMs, S.ScheduleMs, S.MeshBuildMs, S.CollisionMs,
			S.UpdateMs));
}
