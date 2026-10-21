#include "PlanetTerrainNextComponent.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextDiag.h"
#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextLodFade.h"
#include "PlanetTerrainNextLog.h"
#include "PlanetTerrainNextMorphMaterial.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "SingularityPlanetTerrainNextMode.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
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

UMaterialInterface* LoadStablePatchMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (!Cached.IsValid())
	{
		Cached = PlanetTerrainNextMorphMaterial::GetOrCreateStable();
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
	DetailAlbedoTextures.SetNum(6);
	DetailNormalTextures.SetNum(6);
	for (int32 Face = 0; Face < 6; ++Face)
	{
		DetailAlbedoTextures[Face] = nullptr;
		DetailNormalTextures[Face] = nullptr;
	}
	DetailStore = MakeShared<FPlanetTerrainNextDetailStore>();
	DetailStore->SetEnabled(Settings.bDetailProjection);
	DetailStore->SetBudgets(Settings.DetailMaxConcurrentBakes, Settings.DetailUploadsPerFrame);
	DetailStore->Reset(
		Descriptor.PlanetId,
		HeightField->GetIdentityHash(),
		static_cast<uint16>(Settings.DetailResolution));
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
	FrameTimeMsSamples.Reset();
	FrameTimeSampleCursor = 0;
	FrameTimeSortCounter = 0;
	FrameStats = FPlanetTerrainNextFrameStats();
	bInitialized = true;

	// Coverage before anything else: all six roots may use the explicit bootstrap fallback.
	bAllowSynchronousBootstrap = true;
	const double Start = FPlatformTime::Seconds();
	for (int32 Face = 0; Face < 6; ++Face)
	{
		BuildNode(FPlanetTerrainNextNodeId(static_cast<uint8>(Face), 0, 0, 0));
	}
	bAllowSynchronousBootstrap = false;
	TArray<FPlanetTerrainNextDrawItem> DrawSet;
	Quadtree.CollectDrawSet([this](const FPlanetTerrainNextNodeId& Id) { return Resident.Contains(Id); }, DrawSet);
	FrameStats.bDrawCoverageOk = CheckDrawCoverage(DrawSet);
	FrameStats.Drawn = DrawSet.Num();
	ApplyDrawSet(DrawSet, 1.0f / 60.0f, nullptr);
	RefreshTilePins(DrawSet);
	ApplyMorph(DrawSet, 1.0f / 60.0f);

	UE_LOG(LogPlanetTerrainNext, Log,
		TEXT("TerrainNext R9.3 init planet=%lld R=%.1f km heights=[%.0f, %.0f] m roots=%d gpuSlots=%d detail=%dx%d in %.1f ms GE[0]=%.0f m GE[17]=%.2f m"),
		Descriptor.PlanetId,
		Descriptor.RadiusMeters / 1000.0,
		HeightField->GetMinHeightMeters(),
		HeightField->GetMaxHeightMeters(),
		Resident.Num(),
		GpuPool->GetCapacity(),
		Settings.bDetailProjection ? Settings.DetailResolution : 0,
		Settings.bDetailProjection ? Settings.DetailResolution : 0,
		(FPlatformTime::Seconds() - Start) * 1000.0,
		GeometricErrorByLevel[0],
		GeometricErrorByLevel[17]);
}

void UPlanetTerrainNextComponent::ShutdownTerrain()
{
	if (PerfCsvArchive)
	{
		PerfCsvArchive->Close();
		delete PerfCsvArchive;
		PerfCsvArchive = nullptr;
		bPerfCsvHeaderWritten = false;
	}
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
	PendingPhysicalReleases.Reset();
	HandoffHoldFrames.Reset();
	LodFades.Reset();
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
	if (DetailStore.IsValid())
	{
		DetailStore->Shutdown();
	}
	DetailStore.Reset();
	DetailAlbedoTextures.Reset();
	DetailNormalTextures.Reset();
	bDetailProjectionLive = false;
	if (TileCache.IsValid())
	{
		TileCache->SetDiskStore(nullptr);
	}
	if (DiskTileStore.IsValid())
	{
		DiskTileStore->Shutdown();
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
	UMaterialInstanceDynamic* TransitionMaterial = nullptr;
	if (UMaterialInterface* Base = LoadStablePatchMaterial())
	{
		Material = UMaterialInstanceDynamic::Create(Base, Mesh);
	}
	if (UMaterialInterface* Base = LoadPatchMaterial())
	{
		TransitionMaterial = UMaterialInstanceDynamic::Create(Base, Mesh);
	}

	FSlot Slot;
	Slot.Mesh = Mesh;
	Slot.Material = Material;
	Slot.TransitionMaterial = TransitionMaterial;
	SlotMeshes.Add(Mesh);
	SlotMaterials.Add(Material);
	SlotTransitionMaterials.Add(TransitionMaterial);
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
	ReconcileShadowState(Slot, false, false);
	if (Slot.Mesh && Slot.Material && Slot.bTransitionMaterialActive)
	{
		Slot.Mesh->SetMaterial(0, Slot.Material);
		Slot.bTransitionMaterialActive = false;
	}
	Slot.bInUse = false;
	Slot.bCpuMorph = false;
	Slot.FineCm.Reset();
	Slot.CoarseCm.Reset();
	Slot.FineNormals.Reset();
	Slot.CoarseNormals.Reset();
	Slot.Tangents.Reset();
	Slot.BaseUVs.Reset();
	Slot.BaseColors.Reset();
	// Keep a compatible section in the pool. UpdateMeshSection preserves its index buffer;
	// ShouldRecreateSection checks topology version and both array sizes before reuse.
	Drawn.Remove(Id);
	FreeSlots.Add(SlotIndex);
}

void UPlanetTerrainNextComponent::ClearSlotSection(FSlot& Slot)
{
	if (Slot.Mesh && Slot.bHasSection)
	{
		Slot.Mesh->ClearMeshSection(0);
		++FrameStats.MeshSectionClears;
	}
	Slot.bHasSection = false;
	Slot.IndexBufferVersion = -1;
	Slot.SectionVertexCount = 0;
	Slot.SectionIndexCount = 0;
}

bool UPlanetTerrainNextComponent::ShouldRecreateSection(
	const FSlot& Slot, bool bAllowForceFresh, int32 VertexCount, int32 IndexCount) const
{
	if (!Slot.bHasSection)
	{
		return true;
	}
	if (Slot.IndexBufferVersion != PlanetTerrainNextPatch::GetIndexBufferVersion())
	{
		return true;
	}
	if (Slot.SectionVertexCount != VertexCount || Slot.SectionIndexCount != IndexCount)
	{
		return true;
	}
	if (bAllowForceFresh && Settings.bForceFreshMeshSection)
	{
		return true;
	}
	return false;
}

void UPlanetTerrainNextComponent::ApplySuspendedProxyVisibility(bool bShowProxy)
{
	if (!SuspendedProxy.IsValid())
	{
		return;
	}
	SuspendedProxy->SetVisibility(bShowProxy);
	// Patches are non-occluders; a visible shell must not occlude them. When Next is
	// active the shell stays hidden and non-occluding even if a race flips visibility.
	SuspendedProxy->SetCanEverAffectNavigation(false);
	if (UPrimitiveComponent* Prim = SuspendedProxy.Get())
	{
		Prim->bUseAsOccluder = bShowProxy;
	}
}

bool UPlanetTerrainNextComponent::BuildNode(const FPlanetTerrainNextNodeId& Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_BuildNode);
	if (!HeightField.IsValid() || !TileCache.IsValid() || !GpuPool.IsValid() || Resident.Contains(Id))
	{
		return false;
	}

	const FPlanetTerrainNextTileId TileId = MakeTileId(Id);
	auto IsPinned = [this](const FPlanetTerrainNextTileId& Key)
	{
		return GpuPool->IsPinned(Key);
	};

	// Runtime refinement must consume only resident CPU tiles. Synchronous generation
	// is reserved for the six bootstrap roots in InitializeTerrain.
	const FPlanetTerrainNextHeightTile* Tile = TileCache->Find(TileId);
	if (Tile)
	{
		TileCache->Touch(TileId, FrameCounter);
	}
	else if (bAllowSynchronousBootstrap)
	{
		Tile = TileCache->GetOrBake(*HeightField, TileId, FrameCounter, IsPinned);
	}
	else
	{
		++FrameStats.DeferredMissingTiles;
		return false;
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
	// Skirts off on CPU-morph LODs (L0–L2): curtains read as inverted checkerboards from orbit.
	const bool bLikelyCpuMorph = !Settings.bMorphWpoEnabled
		|| Id.Level <= Settings.MorphCpuMaxLevel;
	const double SkirtDepth = (Settings.bSkirtsEnabled && !bLikelyCpuMorph)
		? PlanetTerrainNextPatch::RecommendedSkirtDepthM(
			GeometricErrorByLevel[FMath::Clamp(Id.Level - 2, 0, MaxTreeLevel)],
			PlanetTerrainNextPatch::VertexSpacingMeters(Descriptor.RadiusMeters, Id.Level))
		: 0.0;

	FPlanetTerrainNextEdgeAdjacency EdgeAdjacency[4];
	if (Drawn.Num() > 1)
	{
		const double EdgeSamples[] = {0.1, 0.3, 0.5, 0.7, 0.9};
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			TMap<FPlanetTerrainNextNodeId, int32> CandidateHits;
			TMap<FPlanetTerrainNextNodeId, double> CandidateFirstT;
			TMap<FPlanetTerrainNextNodeId, double> CandidateLastT;
			TMap<FPlanetTerrainNextNodeId, EPlanetTerrainNextPatchEdge> CandidateEdges;
			for (const double T : EdgeSamples)
			{
				const double U = FMath::Lerp(Id.U0(), Id.U1(), T);
				const double V = FMath::Lerp(Id.V0(), Id.V1(), T);
				const double Epsilon = FMath::Max(Id.Size() * 1.0e-6, 1.0e-9);
				double SampleU = U;
				double SampleV = V;
				switch (Edge)
				{
				case 0: SampleV -= Epsilon; break;
				case 1: SampleV += Epsilon; break;
				case 2: SampleU -= Epsilon; break;
				default: SampleU += Epsilon; break;
				}
				const FVector3d Direction = PlanetTerrainNext::FacePoint(Id.Face, SampleU, SampleV);
				for (const FPlanetTerrainNextNodeId& Candidate : Drawn)
				{
					if (Candidate == Id || Candidate.Face == Id.Face && Candidate.Level < Id.Level
						&& (Id.X >> (Id.Level - Candidate.Level)) == Candidate.X
						&& (Id.Y >> (Id.Level - Candidate.Level)) == Candidate.Y)
					{
						continue;
					}
					double CandidateU = 0.0;
					double CandidateV = 0.0;
					if (!PlanetTerrainNext::TryUnprojectFace(Direction, Candidate.Face, CandidateU, CandidateV))
					{
						continue;
					}
					const double Eps = 1.0e-7;
					if (CandidateU < Candidate.U0() - Eps || CandidateU > Candidate.U1() + Eps
						|| CandidateV < Candidate.V0() - Eps || CandidateV > Candidate.V1() + Eps)
					{
						continue;
					}
					const double EdgeT = (CandidateU <= Candidate.U0() + Eps || CandidateU >= Candidate.U1() - Eps)
						? CandidateV : CandidateU;
					CandidateHits.FindOrAdd(Candidate)++;
					if (CandidateV <= Candidate.V0() + Eps) CandidateEdges.FindOrAdd(Candidate, EPlanetTerrainNextPatchEdge::Bottom);
					else if (CandidateV >= Candidate.V1() - Eps) CandidateEdges.FindOrAdd(Candidate, EPlanetTerrainNextPatchEdge::Top);
					else if (CandidateU <= Candidate.U0() + Eps) CandidateEdges.FindOrAdd(Candidate, EPlanetTerrainNextPatchEdge::Left);
					else CandidateEdges.FindOrAdd(Candidate, EPlanetTerrainNextPatchEdge::Right);
					if (!CandidateFirstT.Contains(Candidate)) CandidateFirstT.Add(Candidate, EdgeT);
					CandidateLastT.Add(Candidate, EdgeT);
				}
			}
			FPlanetTerrainNextNodeId Best;
			int32 BestHits = 0;
			for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : CandidateHits)
			{
				if (Pair.Value > BestHits || (Pair.Value == BestHits && Pair.Key.Level < Best.Level))
				{
					Best = Pair.Key;
					BestHits = Pair.Value;
				}
			}
			if (BestHits >= 3)
			{
				EdgeAdjacency[Edge].bHasNeighbor = true;
				EdgeAdjacency[Edge].NeighborId = Best;
				EdgeAdjacency[Edge].bRequiresStitching = Best.Level <= Id.Level;
				EdgeAdjacency[Edge].NeighborEdge = CandidateEdges.FindRef(Best);
				EdgeAdjacency[Edge].bReversed = CandidateLastT.FindRef(Best) < CandidateFirstT.FindRef(Best);
			}
		}
	}

	FPlanetTerrainNextPatchData Patch;
	PlanetTerrainNextPatch::BuildFromTile(*HeightField, *Tile, SkirtDepth, Patch, EdgeAdjacency);

	// F16 UV×MorphScaleCm quantization: ULP~1e-3 near 1 → Scale>1000cm ≈ >1cm vertex error.
	// Raw cm packing also overflows F16 at 65504. Either case → CPU morph (true WPO disconnect).
	const bool bCpuMorph = Settings.bEnableLegacyMorph
		&& (!Settings.bMorphWpoEnabled
			|| Id.Level <= Settings.MorphCpuMaxLevel
			|| Patch.MaxAbsCoarseDeltaCm > static_cast<double>(Settings.MorphGpuMaxScaleCm));

	if (Patch.MaxAbsCoarseDeltaCm > 65504.0)
	{
		UE_LOG(LogPlanetTerrainNext, Verbose,
			TEXT("Morph Δmax=%.0f cm exceeds F16 max (65504) on %s — CPU morph=%d"),
			Patch.MaxAbsCoarseDeltaCm, *Id.ToString(), bCpuMorph ? 1 : 0);
	}

	FSlot& Slot = Slots[SlotIndex];
	Slot.Id = Id;
	Slot.TileId = TileId;
	Slot.GpuSlot = GpuSlot;
	Slot.bInUse = true;
	Slot.Morph = 1.0f; // Parent-compatible until reveal + smooth open toward PE target.
	Slot.MorphReveal = 0.0f;
	Slot.MaxAbsCoarseDeltaCm = Patch.MaxAbsCoarseDeltaCm;
	Slot.bCpuMorph = bCpuMorph;
	UploadPatch(Slot, Patch);
	if (Slot.Material)
	{
		// CPU morph: UV deltas are 0 and Morph scalar must stay 0 so WPO cannot NaN.
		Slot.Material->SetScalarParameterValue(TEXT("Morph"), Slot.bCpuMorph ? 0.0f : Slot.Morph);
		Slot.Material->SetScalarParameterValue(
			TEXT("MorphScaleCm"),
			Slot.bCpuMorph ? 1.0f : static_cast<float>(FMath::Max(Slot.MaxAbsCoarseDeltaCm, 1.0)));
	}
	ApplyDebugColor(Slot);
	ApplyDetailParams(Slot);
	Resident.Add(Id, SlotIndex);
	Quadtree.SetNodeHeightRange(Id, Patch.MinHeightM, Patch.MaxHeightM);
	GpuPool->Pin(TileId, EPlanetTerrainNextPinReason::Coverage);

	RecordBuildTime((FPlatformTime::Seconds() - Start) * 1000.0);
	return true;
}

void UPlanetTerrainNextComponent::UploadPatch(FSlot& Slot, const FPlanetTerrainNextPatchData& Patch)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_MeshSectionUpload);
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_UploadPatch);
	if (!IsValid(Slot.Mesh))
	{
		UE_LOG(LogPlanetTerrainNext, Error, TEXT("UploadPatch skipped: invalid mesh for node %s"), *Patch.Id.ToString());
		return;
	}
	const int32 VertexCount = Patch.VerticesCm.Num();
	const bool bAttributesMatch = VertexCount > 0
		&& Patch.Normals.Num() == VertexCount
		&& Patch.Tangents.Num() == VertexCount
		&& Patch.UVs.Num() == VertexCount
		&& Patch.UV1.Num() == VertexCount
		&& Patch.UV2.Num() == VertexCount
		&& Patch.VertexColors.Num() == VertexCount
		&& Patch.CoarseVerticesCm.Num() == VertexCount
		&& Patch.CoarseNormals.Num() == VertexCount;
	if (!bAttributesMatch)
	{
		UE_LOG(LogPlanetTerrainNext, Error,
			TEXT("UploadPatch skipped: attribute mismatch node %s V=%d N=%d T=%d UV=%d UV1=%d UV2=%d C=%d Coarse=%d/%d"),
			*Patch.Id.ToString(), VertexCount, Patch.Normals.Num(), Patch.Tangents.Num(),
			Patch.UVs.Num(), Patch.UV1.Num(), Patch.UV2.Num(), Patch.VertexColors.Num(),
			Patch.CoarseVerticesCm.Num(), Patch.CoarseNormals.Num());
		return;
	}
	Slot.Mesh->SetRelativeLocation(Patch.OriginM * PlanetTerrainNext::CmPerMeter);

	Slot.Tangents.Reset();
	Slot.Tangents.Reserve(Patch.Tangents.Num());
	for (const FVector& Tangent : Patch.Tangents)
	{
		Slot.Tangents.Add(Tangent);
	}
	Slot.BaseUVs = Patch.UVs;
	Slot.BaseColors = Patch.VertexColors;

	if (Slot.bCpuMorph)
	{
		Slot.FineCm = Patch.VerticesCm;
		Slot.CoarseCm = Patch.CoarseVerticesCm;
		Slot.FineNormals = Patch.Normals;
		Slot.CoarseNormals = Patch.CoarseNormals;
		UploadCpuMorphedSection(Slot);
	}
	else
	{
		Slot.FineCm.Reset();
		Slot.CoarseCm.Reset();
		Slot.FineNormals.Reset();
		Slot.CoarseNormals.Reset();

		TArray<FProcMeshTangent> Tangents;
		Tangents.Reserve(Patch.Tangents.Num());
		for (const FVector& Tangent : Patch.Tangents)
		{
			Tangents.Add(FProcMeshTangent(Tangent, false));
		}

		const TArray<int32>& Indices = PlanetTerrainNextPatch::Indices();
		if (Indices.Num() == 0 || (Indices.Num() % 3) != 0)
		{
			UE_LOG(LogPlanetTerrainNext, Error, TEXT("UploadPatch skipped: invalid index buffer for node %s (%d indices)"), *Patch.Id.ToString(), Indices.Num());
			return;
		}
		if (ShouldRecreateSection(Slot, true, Patch.VerticesCm.Num(), Indices.Num()))
		{
			ClearSlotSection(Slot);
			Slot.Mesh->CreateMeshSection_LinearColor(
				0,
				Patch.VerticesCm,
				Indices,
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
			Slot.IndexBufferVersion = PlanetTerrainNextPatch::GetIndexBufferVersion();
			Slot.SectionVertexCount = Patch.VerticesCm.Num();
			Slot.SectionIndexCount = Indices.Num();
			++FrameStats.MeshSectionCreates;
			++FrameStats.MeshResourceRebuilds;
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
			++FrameStats.MeshSectionUpdates;
		}
	}

	// Expand bounds so WPO / CPU morph to coarse cannot cull the patch.
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
	Slot.Mesh->SetBoundsScale(static_cast<float>(MaxRadiusCm / FineRadius * 1.25));
}

void UPlanetTerrainNextComponent::UploadCpuMorphedSection(FSlot& Slot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_CpuMorphMeshSectionUpload);
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_UploadCpuMorphedSection);
	const double UploadStart = FPlatformTime::Seconds();
	const int32 Num = Slot.FineCm.Num();
	if (Num == 0 || Slot.CoarseCm.Num() != Num || !Slot.Mesh)
	{
		return;
	}

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV1;
	TArray<FVector2D> UV2;
	Positions.SetNumUninitialized(Num);
	Normals.SetNumUninitialized(Num);
	UV1.Init(FVector2D::ZeroVector, Num);
	UV2.Init(FVector2D::ZeroVector, Num);

	const float M = Slot.Morph;
	for (int32 Index = 0; Index < Num; ++Index)
	{
		Positions[Index] = FMath::Lerp(Slot.FineCm[Index], Slot.CoarseCm[Index], M);
		FVector N = FMath::Lerp(Slot.FineNormals[Index], Slot.CoarseNormals[Index], M);
		if (!N.Normalize())
		{
			N = Slot.FineNormals[Index];
		}
		Normals[Index] = N;
	}

	TArray<FProcMeshTangent> Tangents;
	Tangents.Reserve(Slot.Tangents.Num());
	for (const FVector& Tangent : Slot.Tangents)
	{
		Tangents.Add(FProcMeshTangent(Tangent, false));
	}

	// Morph animation Updates in place; recreate only if section missing or index version stale.
	const TArray<int32>& Indices = PlanetTerrainNextPatch::Indices();
	if (ShouldRecreateSection(Slot, false, Num, Indices.Num()))
	{
		ClearSlotSection(Slot);
		Slot.Mesh->CreateMeshSection_LinearColor(
			0,
			Positions,
			Indices,
			Normals,
			Slot.BaseUVs,
			UV1,
			UV2,
			TArray<FVector2D>(),
			Slot.BaseColors,
			Tangents,
			false);
		if (Slot.Material)
		{
			Slot.Mesh->SetMaterial(0, Slot.Material);
		}
		Slot.bHasSection = true;
		Slot.IndexBufferVersion = PlanetTerrainNextPatch::GetIndexBufferVersion();
		Slot.SectionVertexCount = Num;
		Slot.SectionIndexCount = Indices.Num();
		++FrameStats.MeshSectionCreates;
		++FrameStats.MeshResourceRebuilds;
	}
	else
	{
		Slot.Mesh->UpdateMeshSection_LinearColor(
			0,
			Positions,
			Normals,
			Slot.BaseUVs,
			UV1,
			UV2,
			TArray<FVector2D>(),
			Slot.BaseColors,
			Tangents);
		++FrameStats.MeshSectionUpdates;
	}
	++FrameStats.CpuMorphUploads;
	FrameStats.CpuMorphUploadMs += (FPlatformTime::Seconds() - UploadStart) * 1000.0;
}

void UPlanetTerrainNextComponent::ApplyDrawSet(
	const TArray<FPlanetTerrainNextDrawItem>& DrawSet,
	float DeltaSeconds,
	const FPlanetTerrainNextView* View)
{
	TSet<FPlanetTerrainNextNodeId> Desired;
	Desired.Reserve(DrawSet.Num() + 8);
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		Desired.Add(Item.Id);
	}

	if (Settings.bEnableDitheredLOD)
	{
		ApplyDrawSetDithered(Desired, DeltaSeconds, View);
	}
	else
	{
		LodFades.Reset();
		ApplyDrawSetExclusive(Desired);
	}
}

void UPlanetTerrainNextComponent::ApplyDrawSetExclusive(TSet<FPlanetTerrainNextNodeId>& Next)
{
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

	SyncVisibilityToRenderSet(Next);
	ApplyLodFadeParams();
	Drawn = MoveTemp(Next);
	FrameStats.ActiveLodFades = 0;
	FrameStats.MeanLodFade = 0.0f;
}

int32 UPlanetTerrainNextComponent::FindLodFadeIndex(const FPlanetTerrainNextNodeId& Parent) const
{
	for (int32 Index = 0; Index < LodFades.Num(); ++Index)
	{
		if (LodFades[Index].Parent == Parent)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UPlanetTerrainNextComponent::ApplyDrawSetDithered(
	const TSet<FPlanetTerrainNextNodeId>& Desired,
	float DeltaSeconds,
	const FPlanetTerrainNextView* View)
{
	HandoffHoldFrames.Reset();
	auto IsRes = [this](const FPlanetTerrainNextNodeId& Id) { return Resident.Contains(Id); };
	auto DistanceToPatch = [this, View](const FPlanetTerrainNextNodeId& Id)
	{
		if (!View)
		{
			return 0.0;
		}
		const FVector3d Center = PlanetTerrainNext::FacePoint(
			Id.Face, 0.5 * (Id.U0() + Id.U1()), 0.5 * (Id.V0() + Id.V1())) * Descriptor.RadiusMeters;
		const double BoundRadius = Descriptor.RadiusMeters * FMath::Sqrt(2.0) * Id.Size()
			+ FMath::Max(FMath::Abs(HeightField->GetMinHeightMeters()), FMath::Abs(HeightField->GetMaxHeightMeters()));
		return (View->CameraM - Center).Length() - BoundRadius;
	};
	auto FadeSpan = [this](const FPlanetTerrainNextNodeId& Id)
	{
		const int32 Level = FMath::Clamp(static_cast<int32>(Id.Level), 0, GeometricErrorByLevel.Num() - 1);
		const double PatchSizeM = Descriptor.RadiusMeters * FMath::Sqrt(2.0) * Id.Size();
		const double GeometricSpanM = GeometricErrorByLevel.IsValidIndex(Level)
			? GeometricErrorByLevel[Level] * 4.0
			: 1.0;
		return FMath::Max(1.0, FMath::Max(PatchSizeM * 0.25, GeometricSpanM));
	};
	TSet<FPlanetTerrainNextNodeId> EffectiveDesired = Desired;
	TSet<FPlanetTerrainNextNodeId> CapacityDeferredRender;

	// Reverse mid-fade if Desired flipped direction.
	for (FPlanetTerrainNextLodFade& Fade : LodFades)
	{
		const bool bWantChildren = PlanetTerrainNextLodFade::DesiredHasAllChildren(Desired, Fade.Parent);
		const bool bWantParent = Desired.Contains(Fade.Parent);
		if (Fade.State == EPlanetTerrainNextLodFadeState::FadingToChildren && bWantParent && !bWantChildren)
		{
			Fade.State = EPlanetTerrainNextLodFadeState::FadingToParent;
			if (View) PlanetTerrainNextLodFade::ReverseSpatialFade(Fade, DistanceToPatch(Fade.Parent), -1);
			else Fade.Direction = -1;
		}
		else if (Fade.State == EPlanetTerrainNextLodFadeState::FadingToParent && bWantChildren && !bWantParent)
		{
			Fade.State = EPlanetTerrainNextLodFadeState::FadingToChildren;
			if (View) PlanetTerrainNextLodFade::ReverseSpatialFade(Fade, DistanceToPatch(Fade.Parent), 1);
			else Fade.Direction = 1;
		}
	}

	// Start split fades: previous parent leaf → four resident children in Desired.
	for (const FPlanetTerrainNextNodeId& Prev : Drawn)
	{
		if (Desired.Contains(Prev))
		{
			continue;
		}
		if (!PlanetTerrainNextLodFade::DesiredHasAllChildren(Desired, Prev))
		{
			continue;
		}
		if (!PlanetTerrainNextLodFade::ChildrenResident(Prev, IsRes))
		{
			continue;
		}
		if (FindLodFadeIndex(Prev) != INDEX_NONE)
		{
			continue;
		}
		if (LodFades.Num() >= Settings.MaxConcurrentTransitionGroups)
		{
			// Keep the old parent visible until a transition slot becomes available.
			EffectiveDesired.Add(Prev);
			for (int32 Child = 0; Child < 4; ++Child)
			{
				EffectiveDesired.Remove(Prev.Child(Child));
			}
			continue;
		}
		FPlanetTerrainNextLodFade Fade;
		Fade.Parent = Prev;
		Fade.State = EPlanetTerrainNextLodFadeState::FadingToChildren;
		Fade.Progress = 0.0f;
		Fade.DurationSeconds = Settings.LodTransitionSeconds;
		Fade.Direction = 1;
		Fade.GenerationEpoch = HeightField.IsValid() ? HeightField->GetIdentityHash() : 0;
		Fade.bSpatial = View != nullptr;
		if (View)
		{
			Fade.AnchorDistanceM = DistanceToPatch(Prev);
			Fade.DistanceSpanM = FadeSpan(Prev);
			Fade.LastCameraM = View->CameraM;
			Fade.bHasCameraSample = true;
		}
		LodFades.Add(Fade);
	}

	// Start merge fades: four children drawn → parent in Desired and resident.
	TSet<FPlanetTerrainNextNodeId> CheckedParents;
	for (const FPlanetTerrainNextNodeId& Id : Drawn)
	{
		if (Id.Level == 0)
		{
			continue;
		}
		const FPlanetTerrainNextNodeId Parent(Id.Face, static_cast<uint8>(Id.Level - 1), Id.X >> 1, Id.Y >> 1);
		if (CheckedParents.Contains(Parent))
		{
			continue;
		}
		CheckedParents.Add(Parent);
		if (!Desired.Contains(Parent) || !Resident.Contains(Parent))
		{
			continue;
		}
		bool bAllChildrenDrawn = true;
		for (int32 C = 0; C < 4; ++C)
		{
			if (!Drawn.Contains(Parent.Child(C)))
			{
				bAllChildrenDrawn = false;
				break;
			}
		}
		if (!bAllChildrenDrawn)
		{
			continue;
		}
		if (FindLodFadeIndex(Parent) != INDEX_NONE)
		{
			continue;
		}
		if (LodFades.Num() >= Settings.MaxConcurrentTransitionGroups)
		{
			// Keep all old children visible until this merge can get a shared fade.
			EffectiveDesired.Remove(Parent);
			for (int32 Child = 0; Child < 4; ++Child)
			{
				CapacityDeferredRender.Add(Parent.Child(Child));
			}
			continue;
		}
		FPlanetTerrainNextLodFade Fade;
		Fade.Parent = Parent;
		Fade.State = EPlanetTerrainNextLodFadeState::FadingToParent;
		Fade.Progress = 1.0f;
		Fade.DurationSeconds = Settings.LodTransitionSeconds;
		Fade.Direction = -1;
		Fade.GenerationEpoch = HeightField.IsValid() ? HeightField->GetIdentityHash() : 0;
		Fade.bSpatial = View != nullptr;
		if (View)
		{
			Fade.AnchorDistanceM = DistanceToPatch(Parent);
			Fade.DistanceSpanM = FadeSpan(Parent);
			Fade.LastCameraM = View->CameraM;
			Fade.bHasCameraSample = true;
		}
		LodFades.Add(Fade);
	}

	// Advance fades; drop completed groups.
	for (int32 Index = LodFades.Num() - 1; Index >= 0; --Index)
	{
		FPlanetTerrainNextLodFade& Fade = LodFades[Index];
		if (HeightField.IsValid() && Fade.GenerationEpoch != HeightField->GetIdentityHash())
		{
			LodFades.RemoveAtSwap(Index);
			continue;
		}
		const bool bFadeComplete = View
			? PlanetTerrainNextLodFade::AdvanceSpatialFade(Fade, DistanceToPatch(Fade.Parent), View->CameraM)
			: PlanetTerrainNextLodFade::TickFade(Fade, DeltaSeconds);
		if (bFadeComplete)
		{
			auto RestoreStableMaterial = [this](const FPlanetTerrainNextNodeId& Id)
			{
				if (const int32* SlotIndex = Resident.Find(Id))
				{
					FSlot& Slot = Slots[*SlotIndex];
					if (Slot.Mesh && Slot.Material && Slot.bTransitionMaterialActive)
					{
						Slot.Mesh->SetMaterial(0, Slot.Material);
						Slot.bTransitionMaterialActive = false;
					}
					Slot.LastLodFadeParam = -1.0f;
					Slot.LastLodFadeRoleParam = -1.0f;
					if (Slot.Material)
					{
						Slot.Material->SetScalarParameterValue(TEXT("LodFade"), 0.0f);
						Slot.Material->SetScalarParameterValue(TEXT("LodFadeRole"), 0.0f);
					}
				}
			};
			RestoreStableMaterial(Fade.Parent);
			for (int32 Child = 0; Child < 4; ++Child)
			{
				RestoreStableMaterial(Fade.Parent.Child(Child));
			}
			LodFades.RemoveAtSwap(Index);
		}
	}

	TSet<FPlanetTerrainNextNodeId> Render;
	PlanetTerrainNextLodFade::BuildRenderSet(EffectiveDesired, LodFades, Render);
	for (const FPlanetTerrainNextNodeId& Id : CapacityDeferredRender)
	{
		Render.Add(Id);
	}
	// Keep fading parents/children resident-visible even if Desired dropped them early.
	SyncVisibilityToRenderSet(Render);
	ApplyLodFadeParams();
	Drawn = MoveTemp(Render);

	float FadeSum = 0.0f;
	for (const FPlanetTerrainNextLodFade& Fade : LodFades)
	{
		FadeSum += Fade.Progress;
	}
	FrameStats.ActiveLodFades = LodFades.Num();
	FrameStats.MeanLodFade = LodFades.Num() > 0 ? FadeSum / LodFades.Num() : 0.0f;
	FrameStats.HandoffParentsKept = LodFades.Num();
}

void UPlanetTerrainNextComponent::FlushPendingPhysicalReleases()
{
	if (PendingPhysicalReleases.Num() == 0)
	{
		return;
	}

	TArray<FPlanetTerrainNextNodeId> ToRelease;
	for (const FPlanetTerrainNextNodeId& Id : PendingPhysicalReleases)
	{
		if (Drawn.Contains(Id))
		{
			continue;
		}
		bool bFadeParticipant = false;
		for (const FPlanetTerrainNextLodFade& Fade : LodFades)
		{
			if (Fade.Parent == Id)
			{
				bFadeParticipant = true;
				break;
			}
			for (int32 Child = 0; Child < 4; ++Child)
			{
				if (Fade.Parent.Child(Child) == Id)
				{
					bFadeParticipant = true;
					break;
				}
			}
			if (bFadeParticipant)
			{
				break;
			}
		}
		if (!bFadeParticipant)
		{
			ToRelease.Add(Id);
		}
	}

	for (const FPlanetTerrainNextNodeId& Id : ToRelease)
	{
		PendingPhysicalReleases.Remove(Id);
		ReleaseNode(Id);
	}
}

void UPlanetTerrainNextComponent::SyncVisibilityToRenderSet(const TSet<FPlanetTerrainNextNodeId>& Render)
{
	int32 Changes = 0;
	for (const FPlanetTerrainNextNodeId& Id : Render)
	{
		if (const int32* SlotIndex = Resident.Find(Id))
		{
			FSlot& Slot = Slots[*SlotIndex];
			if (!Slot.bShown)
			{
				BeginVisibleHandoff(Slot);
				Slot.Mesh->SetVisibility(true);
				Slot.bShown = true;
				++Changes;
			}
			else
			{
				if (Slot.LodTransition == ELodTransition::Splitting && Slot.MorphReveal >= 1.0f - KINDA_SMALL_NUMBER)
				{
					Slot.LodTransition = ELodTransition::Stable;
				}
			}
		}
	}
	for (const FPlanetTerrainNextNodeId& Id : Drawn)
	{
		if (!Render.Contains(Id))
		{
			if (const int32* SlotIndex = Resident.Find(Id))
			{
				FSlot& Slot = Slots[*SlotIndex];
				if (Slot.bShown)
				{
					Slot.Mesh->SetVisibility(false);
					Slot.bShown = false;
					Slot.LodTransition = ELodTransition::Merging;
					Slot.MorphReveal = 0.0f;
					Slot.Morph = 1.0f;
					if (Slot.Material)
					{
						Slot.Material->SetScalarParameterValue(TEXT("LodFade"), 0.0f);
						Slot.Material->SetScalarParameterValue(TEXT("LodFadeRole"), 0.0f);
						Slot.LastLodFadeParam = 0.0f;
						Slot.LastLodFadeRoleParam = 0.0f;
					}
					++Changes;
				}
			}
		}
	}
	FrameStats.VisibilityChanges = Changes;
}

void UPlanetTerrainNextComponent::ReconcileShadowState(FSlot& Slot, bool bInFade, bool bDominant)
{
	if (!Slot.Mesh)
	{
		return;
	}
	const bool bDesiredCastShadow = Settings.bCastShadow && Slot.bShown && (!bInFade || bDominant);
	if (Slot.bAppliedCastShadow != bDesiredCastShadow)
	{
		Slot.Mesh->SetCastShadow(bDesiredCastShadow);
		Slot.bAppliedCastShadow = bDesiredCastShadow;
		++FrameStats.ShadowStateChanges;
	}
}

void UPlanetTerrainNextComponent::ApplyLodFadeParams()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_ApplyLodFadeParams);
	TMap<FPlanetTerrainNextNodeId, float> FadeByParent;
	TSet<FPlanetTerrainNextNodeId> Participants;
	for (const FPlanetTerrainNextLodFade& Fade : LodFades)
	{
		if (Fade.State != EPlanetTerrainNextLodFadeState::FadingToChildren
			&& Fade.State != EPlanetTerrainNextLodFadeState::FadingToParent)
		{
			continue;
		}
		FadeByParent.Add(Fade.Parent, Fade.Progress);
		Participants.Add(Fade.Parent);
		for (int32 Child = 0; Child < 4; ++Child)
		{
			Participants.Add(Fade.Parent.Child(Child));
		}
	}

	for (const FPlanetTerrainNextNodeId& Participant : Participants)
	{
		const int32* SlotIndex = Resident.Find(Participant);
		if (!SlotIndex)
		{
			continue;
		}
		FSlot& Slot = Slots[*SlotIndex];
		if (!Slot.bInUse || !Slot.bShown)
		{
			continue;
		}
		float Progress = 0.0f;
		float Role = PlanetTerrainNextLodFade::RoleParent();
		bool bInFade = false;

		if (const float* ParentProgress = FadeByParent.Find(Slot.Id))
		{
			Progress = *ParentProgress;
			Role = PlanetTerrainNextLodFade::RoleParent();
			bInFade = true;
		}
		else if (Slot.Id.Level > 0)
		{
			const FPlanetTerrainNextNodeId Parent(
				Slot.Id.Face, static_cast<uint8>(Slot.Id.Level - 1), Slot.Id.X >> 1, Slot.Id.Y >> 1);
			if (const float* ChildProgress = FadeByParent.Find(Parent))
			{
				Progress = *ChildProgress;
				Role = PlanetTerrainNextLodFade::RoleChild();
				bInFade = true;
			}
		}

		if (!bInFade)
		{
			// Stable leaf: fully opaque (parent role with fade 0 ⇒ EffectiveAlpha=1).
			Progress = 0.0f;
			Role = PlanetTerrainNextLodFade::RoleParent();
		}

		// Compute and apply the final shadow state exactly once for this patch.
		const bool bDominant = !bInFade || ((Role < 0.5f) ? (Progress < 0.5f) : (Progress >= 0.5f));
		ReconcileShadowState(Slot, bInFade, bDominant);
		if (!Slot.Material)
		{
			continue;
		}

		if (bInFade && Slot.TransitionMaterial)
		{
			if (!Slot.bTransitionMaterialActive)
			{
				Slot.Mesh->SetMaterial(0, Slot.TransitionMaterial);
				Slot.bTransitionMaterialActive = true;
			}
		}
		else if (!bInFade && Slot.bTransitionMaterialActive && Slot.Material)
		{
			Slot.Mesh->SetMaterial(0, Slot.Material);
			Slot.bTransitionMaterialActive = false;
		}
		UMaterialInstanceDynamic* FadeMaterial =
			(bInFade && Slot.TransitionMaterial) ? Slot.TransitionMaterial : Slot.Material;
		if (!FadeMaterial)
		{
			continue;
		}

		if (!FMath::IsNearlyEqual(Slot.LastLodFadeParam, Progress, 1.0e-4f))
		{
			FadeMaterial->SetScalarParameterValue(TEXT("LodFade"), Progress);
			Slot.LastLodFadeParam = Progress;
		}
		if (!FMath::IsNearlyEqual(Slot.LastLodFadeRoleParam, Role, 1.0e-4f))
		{
			FadeMaterial->SetScalarParameterValue(TEXT("LodFadeRole"), Role);
			Slot.LastLodFadeRoleParam = Role;
		}

	}

	// Stable shown patches are reconciled here as part of the same final-state
	// pass, avoiding a second contradictory update from SyncVisibilityToRenderSet.
	for (FSlot& Slot : Slots)
	{
		if (Slot.bInUse && Slot.bShown && !Participants.Contains(Slot.Id))
		{
			ReconcileShadowState(Slot, false, true);
		}
	}
}

void UPlanetTerrainNextComponent::BeginVisibleHandoff(FSlot& Slot)
{
	if (Settings.bEnableLegacyMorph)
	{
		Slot.Morph = 1.0f;
		Slot.MorphReveal = 0.0f;
	}
	else
	{
		Slot.Morph = 0.0f;
		Slot.MorphReveal = 1.0f;
	}
	Slot.LodTransition = ELodTransition::Splitting;
	if (Slot.bCpuMorph)
	{
		UploadCpuMorphedSection(Slot);
		if (Slot.Material)
		{
			Slot.Material->SetScalarParameterValue(TEXT("Morph"), 0.0f);
			Slot.Material->SetScalarParameterValue(TEXT("MorphScaleCm"), 1.0f);
			if (Slot.TransitionMaterial)
			{
				Slot.TransitionMaterial->SetScalarParameterValue(TEXT("Morph"), 0.0f);
				Slot.TransitionMaterial->SetScalarParameterValue(TEXT("MorphScaleCm"), 1.0f);
			}
		}
	}
	else if (Slot.Material)
	{
		Slot.Material->SetScalarParameterValue(TEXT("Morph"), Settings.bEnableLegacyMorph ? 1.0f : 0.0f);
		Slot.Material->SetScalarParameterValue(
			TEXT("MorphScaleCm"),
			static_cast<float>(FMath::Max(Slot.MaxAbsCoarseDeltaCm, 1.0)));
		if (Slot.TransitionMaterial)
		{
			Slot.TransitionMaterial->SetScalarParameterValue(TEXT("Morph"), Settings.bEnableLegacyMorph ? 1.0f : 0.0f);
			Slot.TransitionMaterial->SetScalarParameterValue(
				TEXT("MorphScaleCm"), static_cast<float>(FMath::Max(Slot.MaxAbsCoarseDeltaCm, 1.0)));
		}
	}
	if (Slot.Material)
	{
		Slot.Material->SetScalarParameterValue(TEXT("LodFade"), 0.0f);
		Slot.Material->SetScalarParameterValue(TEXT("LodFadeRole"), 0.0f);
		if (Slot.TransitionMaterial)
		{
			Slot.TransitionMaterial->SetScalarParameterValue(TEXT("LodFade"), 0.0f);
			Slot.TransitionMaterial->SetScalarParameterValue(TEXT("LodFadeRole"), 0.0f);
		}
		Slot.LastLodFadeParam = 0.0f;
		Slot.LastLodFadeRoleParam = 0.0f;
	}
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
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_ApplyMorph);
	const float Dt = FMath::Max(DeltaSeconds, 0.0f);
	TMap<FPlanetTerrainNextNodeId, double> PixelErrors;
	PixelErrors.Reserve(DrawSet.Num());
	for (const FPlanetTerrainNextDrawItem& Item : DrawSet)
	{
		PixelErrors.Add(Item.Id, Item.PixelError);
	}

	int32 Updates = 0;
	double MorphSum = 0.0;
	int32 MorphCount = 0;

	// R9.2: only the final visible set (Drawn). Hidden residents must not advance MorphReveal.
	for (const FPlanetTerrainNextNodeId& Id : Drawn)
	{
		const int32* SlotIndex = Resident.Find(Id);
		if (!SlotIndex)
		{
			continue;
		}
		FSlot& Slot = Slots[*SlotIndex];
		if (!Slot.bShown)
		{
			continue;
		}

		if (const double* Pe = PixelErrors.Find(Id))
		{
			Slot.LastPixelError = *Pe;
		}

		float Target = 0.0f;
		if (!Settings.bEnableLegacyMorph || !Settings.bMorphEnabled)
		{
			// R10.2: dither owns LOD transitions; keep fine geometry (Morph=0).
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
				Slot.LastPixelError, Settings.Lod.TargetPixelError, Settings.MorphEndFactor);
			if (Settings.MorphRevealSeconds <= KINDA_SMALL_NUMBER)
			{
				Slot.MorphReveal = 1.0f;
			}
			else
			{
				const float Linear = FMath::Min(1.0f, Slot.MorphReveal + Dt / Settings.MorphRevealSeconds);
				// SmoothStep so 1→0 morph eases in after commit (same T for all siblings if same age).
				Slot.MorphReveal = Linear;
			}
			const float RevealEase = Slot.MorphReveal * Slot.MorphReveal * (3.0f - 2.0f * Slot.MorphReveal);
			Target = FMath::Lerp(1.0f, PeTarget, RevealEase);
		}

		float Morph = Target;
		if (Settings.bEnableLegacyMorph
			&& Settings.MorphSmoothSeconds > KINDA_SMALL_NUMBER
			&& Settings.bMorphEnabled
			&& Settings.MorphForce < 0.0f)
		{
			const float Alpha = 1.0f - FMath::Exp(-Dt / Settings.MorphSmoothSeconds);
			Morph = FMath::Lerp(Slot.Morph, Target, Alpha);
		}

		const bool bMorphChanged = !FMath::IsNearlyEqual(Slot.Morph, Morph, 1.0e-4f);
		Slot.Morph = Morph;
		MorphSum += Morph;
		++MorphCount;

		if (Slot.bCpuMorph)
		{
			if (bMorphChanged)
			{
				UploadCpuMorphedSection(Slot);
				++Updates;
			}
		}
		else if (Slot.Material && bMorphChanged)
		{
			Slot.Material->SetScalarParameterValue(TEXT("Morph"), Morph);
			if (Slot.TransitionMaterial)
			{
				Slot.TransitionMaterial->SetScalarParameterValue(TEXT("Morph"), Morph);
			}
			++Updates;
		}

		if (Settings.DebugColor == EPlanetTerrainNextDebugColor::Morph)
		{
			ApplyDebugColor(Slot);
		}
	}
	FrameStats.MorphUniformUpdates = Updates;
	FrameStats.MeanMorph = MorphCount > 0 ? static_cast<float>(MorphSum / MorphCount) : 0.0f;
}

void UPlanetTerrainNextComponent::HideAll()
{
	for (FSlot& Slot : Slots)
	{
		if (Slot.Mesh)
		{
			Slot.Mesh->SetVisibility(false);
		}
		Slot.bShown = false;
		ReconcileShadowState(Slot, false, false);
	}
	Drawn.Reset();
	PendingPhysicalReleases.Reset();
	HandoffHoldFrames.Reset();
	LodFades.Reset();
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
		const FLinearColor Color = DebugColorFor(Settings.DebugColor, Slot.Id, Slot.Morph);
		Slot.Material->SetVectorParameterValue(TEXT("Color"), Color);
		if (Slot.TransitionMaterial)
		{
			Slot.TransitionMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

void UPlanetTerrainNextComponent::ApplyDetailParams(FSlot& Slot) const
{
	if (!Slot.Material)
	{
		return;
	}

	const FPlanetTerrainNextNodeId& Id = Slot.Id;
	const float Scale = static_cast<float>(Id.Size());
	const float U0 = static_cast<float>(Id.U0());
	const float V0 = static_cast<float>(Id.V0());
	Slot.Material->SetScalarParameterValue(TEXT("DetailUVScale"), Scale);
	Slot.Material->SetVectorParameterValue(TEXT("DetailUVOffset"), FLinearColor(U0, V0, 0.0f, 0.0f));
	if (Slot.TransitionMaterial)
	{
		Slot.TransitionMaterial->SetScalarParameterValue(TEXT("DetailUVScale"), Scale);
		Slot.TransitionMaterial->SetVectorParameterValue(TEXT("DetailUVOffset"), FLinearColor(U0, V0, 0.0f, 0.0f));
	}

	const bool bWantDetail = Settings.bDetailProjection
		&& Settings.DebugColor == EPlanetTerrainNextDebugColor::Regolith
		&& DetailStore.IsValid()
		&& DetailStore->IsFaceReady(Id.Face);

	UTexture2D* Albedo = bWantDetail ? DetailStore->GetAlbedo(Id.Face) : nullptr;
	UTexture2D* Normal = bWantDetail ? DetailStore->GetNormal(Id.Face) : nullptr;
	if (Albedo)
	{
		Slot.Material->SetTextureParameterValue(TEXT("DetailAlbedo"), Albedo);
		if (Slot.TransitionMaterial)
		{
			Slot.TransitionMaterial->SetTextureParameterValue(TEXT("DetailAlbedo"), Albedo);
		}
	}
	if (Normal)
	{
		Slot.Material->SetTextureParameterValue(TEXT("DetailNormal"), Normal);
		if (Slot.TransitionMaterial)
		{
			Slot.TransitionMaterial->SetTextureParameterValue(TEXT("DetailNormal"), Normal);
		}
	}
	const float DetailBlend = (bWantDetail && Albedo && Normal) ? 1.0f : 0.0f;
	Slot.Material->SetScalarParameterValue(TEXT("DetailBlend"), DetailBlend);
	if (Slot.TransitionMaterial)
	{
		Slot.TransitionMaterial->SetScalarParameterValue(TEXT("DetailBlend"), DetailBlend);
	}
}

void UPlanetTerrainNextComponent::RefreshDetailParams()
{
	for (FSlot& Slot : Slots)
	{
		if (Slot.bInUse)
		{
			ApplyDetailParams(Slot);
		}
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
			ApplyDetailParams(Slot);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_CookCollisionNode_GameThread);
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
		// Collision follows the async tile scheduler; never bake or load synchronously
		// from the game thread while a camera is moving.
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
	const double SkirtDepth = Settings.bSkirtsEnabled
		? PlanetTerrainNextPatch::RecommendedSkirtDepthM(
			GeometricErrorByLevel[FMath::Clamp(Id.Level - 2, 0, MaxTreeLevel)],
			PlanetTerrainNextPatch::VertexSpacingMeters(Descriptor.RadiusMeters, Id.Level))
		: 0.0;
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
	const double CameraAltitudeM = FMath::Max(0.0, View.CameraM.Length() - Descriptor.RadiusMeters);
	if (CameraAltitudeM > Settings.CollisionMaxAltitudeM)
	{
		// High-altitude travel does not need local physics meshes. Release any old
		// residency so descent starts from a bounded, intentional collision budget.
		TArray<FPlanetTerrainNextNodeId> Released;
		for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : CollisionResident)
		{
			Released.Add(Pair.Key);
		}
		for (const FPlanetTerrainNextNodeId& Id : Released)
		{
			ReleaseCollisionNode(Id);
		}
		CollisionManager.SetCookedThisFrame(0);
		return;
	}

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
	TRACE_CPUPROFILER_EVENT_SCOPE(PlanetTerrainNext_TickComponent);
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
			ApplySuspendedProxyVisibility(true);
			bSuspended = true;
		}
		return;
	}
	if (bSuspended || (SuspendedProxy.IsValid() && SuspendedProxy->IsVisible()))
	{
		ApplySuspendedProxyVisibility(false);
		bSuspended = false;
	}

	const double TickStart = FPlatformTime::Seconds();
	const double FrameMs = FMath::Max(0.0, static_cast<double>(DeltaTime) * 1000.0);
	FrameStats.DeltaMs = FrameMs;
	if (FrameTimeMsSamples.Num() < 256)
	{
		FrameTimeMsSamples.Add(FrameMs);
	}
	else
	{
		FrameTimeMsSamples[FrameTimeSampleCursor++ % FrameTimeMsSamples.Num()] = FrameMs;
	}
	if (++FrameTimeSortCounter >= 30 && FrameTimeMsSamples.Num() >= 30)
	{
		FrameTimeSortCounter = 0;
		TArray<double> SortedFrameTimes = FrameTimeMsSamples;
		SortedFrameTimes.Sort();
		auto Percentile = [&SortedFrameTimes](double P)
		{
			return SortedFrameTimes[FMath::Clamp(
				FMath::CeilToInt(P * SortedFrameTimes.Num()) - 1, 0, SortedFrameTimes.Num() - 1)];
		};
		FrameStats.FrameP50Ms = Percentile(0.50);
		FrameStats.FrameP95Ms = Percentile(0.95);
		FrameStats.FrameP99Ms = Percentile(0.99);
	}
	FrameStats.MeshSectionCreates = 0;
	FrameStats.MeshSectionUpdates = 0;
	FrameStats.MeshSectionClears = 0;
	FrameStats.MeshResourceRebuilds = 0;
	FrameStats.ShadowStateChanges = 0;
	FrameStats.DeferredMissingTiles = 0;
	FrameStats.CpuMorphUploads = 0;
	FrameStats.CpuMorphUploadMs = 0.0;
	Settings = FPlanetTerrainNextRuntimeSettings::Read();
	if (!TileCache.IsValid() || !GpuPool.IsValid() || !Scheduler.IsValid() || !HeightField.IsValid())
	{
		return;
	}

	const FPlanetTerrainNextDiagSettings Diag = FPlanetTerrainNextDiagSettings::Read();
	if (Diag.bEnable)
	{
		TickDiagnostic(DeltaTime);
		FrameStats.UpdateMs = (FPlatformTime::Seconds() - TickStart) * 1000.0;
		if (Settings.bShowStats)
		{
			PrintStats();
		}
		return;
	}
	if (bDiagWasEnabled)
	{
		// Leaving diag: drop diag-only residency so normal streaming rebuilds cleanly.
		TArray<FPlanetTerrainNextNodeId> ToRelease;
		ToRelease.Reserve(Resident.Num());
		for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : Resident)
		{
			ToRelease.Add(Pair.Key);
		}
		for (const FPlanetTerrainNextNodeId& Id : ToRelease)
		{
			ReleaseNode(Id);
		}
		bDiagWasEnabled = false;
		DiagRebuildKey = 0;
		FrameStats.bDiagActive = false;
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
	FPlanetTerrainNextView View;
	const bool bHasView = GetView(View);
	TArray<uint8> FacePriority;
	FacePriority.Reserve(6);
	if (bHasView)
	{
		const FVector3d CameraDirection = View.CameraM.GetSafeNormal();
		bool bSelected[6] = {false, false, false, false, false, false};
		for (int32 OrderIndex = 0; OrderIndex < 6; ++OrderIndex)
		{
			int32 BestFace = INDEX_NONE;
			double BestFacing = -TNumericLimits<double>::Max();
			for (int32 Face = 0; Face < 6; ++Face)
			{
				if (bSelected[Face]) continue;
				const double Facing = FVector3d::DotProduct(
					CameraDirection, PlanetTerrainNext::FacePoint(Face, 0.5, 0.5));
				if (Facing > BestFacing)
				{
					BestFacing = Facing;
					BestFace = Face;
				}
			}
			if (BestFace != INDEX_NONE)
			{
				bSelected[BestFace] = true;
				FacePriority.Add(static_cast<uint8>(BestFace));
			}
		}
	}
	else
	{
		for (uint8 Face = 0; Face < 6; ++Face) FacePriority.Add(Face);
	}
	if (DetailStore.IsValid())
	{
		DetailStore->SetEnabled(Settings.bDetailProjection);
		DetailStore->SetBudgets(Settings.DetailMaxConcurrentBakes, Settings.DetailUploadsPerFrame);
		bool bRefreshDetail = false;
		if (Settings.bDetailProjection)
		{
			DetailStore->Tick(HeightField, this, FacePriority);
			if (DetailAlbedoTextures.Num() < 6)
			{
				DetailAlbedoTextures.SetNum(6);
				DetailNormalTextures.SetNum(6);
			}
			for (int32 Face = 0; Face < 6; ++Face)
			{
				DetailAlbedoTextures[Face] = DetailStore->GetAlbedo(static_cast<uint8>(Face));
				DetailNormalTextures[Face] = DetailStore->GetNormal(static_cast<uint8>(Face));
			}
			bRefreshDetail = DetailStore->ConsumeDirty() || !bDetailProjectionLive;
			bDetailProjectionLive = true;
		}
		else if (bDetailProjectionLive)
		{
			bDetailProjectionLive = false;
			bRefreshDetail = true;
		}
		if (bRefreshDetail)
		{
			RefreshDetailParams();
		}
		const FPlanetTerrainNextDetailStoreStats& DetailStats = DetailStore->GetStats();
		FrameStats.DetailReadyFaces = Settings.bDetailProjection ? DetailStats.ReadyFaces : 0;
		FrameStats.DetailPending = Settings.bDetailProjection ? DetailStats.PendingBakes : 0;
		FrameStats.DetailBaking = Settings.bDetailProjection ? DetailStats.Baking : 0;
		FrameStats.DetailUploads = DetailStats.UploadsThisFrame;
		FrameStats.DetailBakeP95Ms = DetailStats.P95BakeMs;
		FrameStats.DetailUploadMs = DetailStats.LastUploadMs;
	}
	++FrameCounter;
	GpuPool->BeginFrame(FrameCounter);
	TileCache->BeginFrame();
	Scheduler->BeginFrame(FrameCounter);
	RefreshDebugColors();

	if (!bHasView)
	{
		return;
	}
	// Refresh collision demand before scheduling so missing collision tiles enter
	// the same bounded worker queue as visual refinement. The second call below
	// cooks any tiles that complete during this frame.
	UpdateCollision(View);

	auto IsReady = [this](const FPlanetTerrainNextNodeId& Id)
	{
		return Resident.Contains(Id);
	};

	const double TreeStart = FPlatformTime::Seconds();
	TArray<FPlanetTerrainNextNodeId> Released;
	Quadtree.Update(View, Settings.Lod, IsReady, Released);
	for (const FPlanetTerrainNextNodeId& Id : Released)
	{
		// Quadtree retirement is logical. Keep the pooled mesh resident until
		// ApplyDrawSetDithered has had a chance to start/finish a merge fade.
		PendingPhysicalReleases.Add(Id);
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
		Desired,
		Settings.Lod.QueueDistanceScale,
		Settings.Lod.MaxLevel);
	for (const FPlanetTerrainNextNodeId& CollisionId : CollisionManager.GetDesired())
	{
		if (CollisionResident.Contains(CollisionId))
		{
			continue;
		}
		const FPlanetTerrainNextTileId CollisionTileId = MakeTileId(CollisionId);
		if (TileCache->Find(CollisionTileId))
		{
			continue;
		}
		FPlanetTerrainNextTileRequest CollisionRequest;
		CollisionRequest.NodeId = CollisionId;
		CollisionRequest.TileId = CollisionTileId;
		CollisionRequest.Class = EPlanetTerrainNextRequestClass::Coverage;
		CollisionRequest.bRequiredForCoverage = true;
		CollisionRequest.Priority = 4.0e13 - static_cast<double>(CollisionId.Level);
		CollisionRequest.DeadlineFrame = FrameCounter + 1;
		Desired.Add(CollisionRequest);
	}
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
		const int32 MaxCommits = FMath::Min(Settings.TileUploadsPerFrame, Settings.MeshCommitsPerFrame);
		for (const FPlanetTerrainNextPendingBuild& Request : Pending)
		{
			if (Built >= MaxCommits)
			{
				break;
			}
			if ((FPlatformTime::Seconds() - BuildStart) * 1000.0 >= Settings.BuildBudgetMs)
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
	ApplyDrawSet(DrawSet, DeltaTime, &View);
	FlushPendingPhysicalReleases();
	const double CollStart = FPlatformTime::Seconds();
	UpdateCollision(View);
	FrameStats.CollisionMs = (FPlatformTime::Seconds() - CollStart) * 1000.0;
	RefreshTilePins(DrawSet);
	// Morph after handoff visibility so reveal cannot run on hidden children (R9.2).
	ApplyMorph(DrawSet, DeltaTime);

	const FPlanetTerrainNextQuadtreeStats& TreeStats = Quadtree.GetStats();
	FrameStats.Nodes = TreeStats.Nodes;
	FrameStats.Splits = TreeStats.Splits;
	FrameStats.Merges = TreeStats.Merges;
	FrameStats.BudgetBlocked = TreeStats.BudgetBlocked;
	FrameStats.BalanceBlocked = TreeStats.BalanceBlocked;
	FrameStats.Resident = Resident.Num();
	FrameStats.Drawn = Drawn.Num();
	FrameStats.Pending = FMath::Max(0, Pending.Num() - Built);
	FrameStats.BuiltThisFrame = Built;
	FrameStats.Slots = Slots.Num();
	FrameStats.ActiveCpuMorphPatches = 0;
	for (const FSlot& Slot : Slots)
	{
		FrameStats.ActiveCpuMorphPatches += Slot.bInUse && Slot.bCpuMorph ? 1 : 0;
	}
	FrameStats.MaxDrawnLevel = 0;
	FrameStats.DrawnL0 = 0;
	FrameStats.DrawnL1 = 0;
	FrameStats.DrawnL2 = 0;
	FrameStats.DrawnL3Plus = 0;
	for (const FPlanetTerrainNextNodeId& Id : Drawn)
	{
		FrameStats.MaxDrawnLevel = FMath::Max(FrameStats.MaxDrawnLevel, static_cast<int32>(Id.Level));
		if (Id.Level == 0)
		{
			++FrameStats.DrawnL0;
		}
		else if (Id.Level == 1)
		{
			++FrameStats.DrawnL1;
		}
		else if (Id.Level == 2)
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
	FrameStats.bDiagActive = false;
	WritePerfSample();

	if (Settings.bShowStats)
	{
		PrintStats();
	}
}

bool UPlanetTerrainNextComponent::BuildDiagNode(
	const FPlanetTerrainNextNodeId& Id,
	bool bFlat,
	bool bForceFresh,
	UMaterialInterface* DiagMaterial)
{
	if (!HeightField.IsValid() || Resident.Contains(Id))
	{
		return false;
	}

	const int32 SlotIndex = AcquireSlot();
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}

	FPlanetTerrainNextPatchData Patch;
	if (bFlat)
	{
		PlanetTerrainNextPatch::BuildFlat(Descriptor.RadiusMeters, Id, 0.0, Patch);
	}
	else
	{
		PlanetTerrainNextPatch::Build(*HeightField, Id, 0.0, Patch);
	}

	FSlot& Slot = Slots[SlotIndex];
	Slot.Id = Id;
	Slot.TileId = MakeTileId(Id);
	Slot.GpuSlot = INDEX_NONE;
	Slot.bInUse = true;
	Slot.Morph = 0.0f;
	Slot.MorphReveal = 1.0f;
	Slot.MaxAbsCoarseDeltaCm = 0.0;
	Slot.bCpuMorph = false;

	const bool bSavedForce = Settings.bForceFreshMeshSection;
	Settings.bForceFreshMeshSection = bForceFresh;
	UploadPatch(Slot, Patch);
	Settings.bForceFreshMeshSection = bSavedForce;

	if (DiagMaterial && Slot.Mesh)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(DiagMaterial, Slot.Mesh);
		if (Mid)
		{
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.78f, 0.74f, 0.70f, 1.0f));
			Slot.Material = Mid;
			if (SlotMaterials.IsValidIndex(SlotIndex))
			{
				SlotMaterials[SlotIndex] = Mid;
			}
			Slot.Mesh->SetMaterial(0, Mid);
		}
	}

	Slot.Mesh->SetVisibility(true);
	Slot.bShown = true;
	ReconcileShadowState(Slot, true, false); // diagnostic meshes never cast terrain shadows
	Resident.Add(Id, SlotIndex);
	Drawn.Add(Id);
	return true;
}

void UPlanetTerrainNextComponent::TickDiagnostic(float DeltaSeconds)
{
	(void)DeltaSeconds;
	bDiagWasEnabled = true;
	ApplySuspendedProxyVisibility(false);

	const FPlanetTerrainNextDiagSettings Diag = FPlanetTerrainNextDiagSettings::Read();
	const uint32 Key = Diag.GetRebuildKey();
	FrameStats.bDiagActive = true;

	if (Key != DiagRebuildKey)
	{
		TArray<FPlanetTerrainNextNodeId> ToRelease;
		ToRelease.Reserve(Resident.Num());
		for (const TPair<FPlanetTerrainNextNodeId, int32>& Pair : Resident)
		{
			ToRelease.Add(Pair.Key);
		}
		for (const FPlanetTerrainNextNodeId& Id : ToRelease)
		{
			ReleaseNode(Id);
		}
		Drawn.Reset();
		HandoffHoldFrames.Reset();

		UMaterialInterface* DiagMat = PlanetTerrainNextDiag::GetOrCreateMaterial(Diag.bTwoSided);
		TArray<FPlanetTerrainNextNodeId> Nodes;
		PlanetTerrainNextDiag::CollectNodes(Diag, Nodes);

		int32 Built = 0;
		int32 Invalid = 0;
		int32 Inward = 0;
		for (const FPlanetTerrainNextNodeId& Id : Nodes)
		{
			if (!BuildDiagNode(Id, !Diag.bHeight, Diag.bForceFreshSection, DiagMat))
			{
				continue;
			}
			++Built;
			FPlanetTerrainNextPatchData Check;
			if (Diag.bHeight)
			{
				PlanetTerrainNextPatch::Build(*HeightField, Id, 0.0, Check);
			}
			else
			{
				PlanetTerrainNextPatch::BuildFlat(Descriptor.RadiusMeters, Id, 0.0, Check);
			}
			const FPlanetTerrainNextPatchValidation V = PlanetTerrainNextDiag::ValidatePatch(Check, false);
			Invalid += V.InvalidIndexCount + V.DegenerateTriangleCount + V.NonFiniteVertexCount;
			Inward += V.InwardFacingGridCount;
		}

		DiagRebuildKey = Key;
		FrameStats.DiagPatches = Built;
		FrameStats.DiagInvalidTris = Invalid;
		FrameStats.DiagInwardFacing = Inward;
		FrameStats.Drawn = Drawn.Num();
		FrameStats.Resident = Resident.Num();
		FrameStats.bDrawCoverageOk = Invalid == 0 && Inward == 0 && Built == Nodes.Num();
		UE_LOG(LogPlanetTerrainNext, Log,
			TEXT("R10.1 Diag rebuild mode=%d level=%d height=%d patches=%d/%d invalid=%d inward=%d fresh=%d"),
			static_cast<int32>(Diag.Mode), Diag.Level, Diag.bHeight ? 1 : 0,
			Built, Nodes.Num(), Invalid, Inward, Diag.bForceFreshSection ? 1 : 0);
	}

	FrameStats.Drawn = Drawn.Num();
	FrameStats.Resident = Resident.Num();
	FrameStats.DetailReadyFaces = 0;
	FrameStats.DetailPending = 0;
}

void UPlanetTerrainNextComponent::WritePerfSample()
{
	if (!Settings.bPerfCapture)
	{
		if (PerfCsvArchive)
		{
			PerfCsvArchive->Close();
			delete PerfCsvArchive;
			PerfCsvArchive = nullptr;
			bPerfCsvHeaderWritten = false;
		}
		return;
	}

	if (!PerfCsvArchive)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Logs/PlanetTerrainNextPerf.csv");
		const int64 ExistingBytes = IFileManager::Get().FileSize(*Path);
		PerfCsvArchive = IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_Append);
		if (!PerfCsvArchive)
		{
			return;
		}
		bPerfCsvHeaderWritten = ExistingBytes > 0;
	}

	if (!bPerfCsvHeaderWritten)
	{
		const ANSICHAR Header[] =
			"frame,scenario,delta_ms,update_ms,tree_ms,schedule_ms,mesh_build_ms,collision_ms,frame_p50_ms,frame_p95_ms,frame_p99_ms,"
			"nodes,resident,drawn,pending,fades,visibility_changes,mesh_creates,mesh_updates,mesh_clears,mesh_rebuilds,balance_blocked,"
			"cpu_morph_active,cpu_morph_uploads,cpu_morph_upload_ms,detail_ready,detail_pending,detail_baking,detail_uploads,"
			"detail_bake_p95_ms,detail_upload_ms,tile_cache_hits,tile_cache_misses,tile_bakes,disk_hits,disk_misses,disk_writes,"
			"gpu_resident,gpu_uploads,collision_resident,collision_desired,collision_cooked,coverage_ok\n";
		PerfCsvArchive->Serialize(const_cast<ANSICHAR*>(Header), UE_ARRAY_COUNT(Header) - 1);
		bPerfCsvHeaderWritten = true;
	}

	const FString Line = FString::Printf(
		TEXT("%lld,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.4f,%d,%d,%d,%d,%.4f,%.4f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"),
		FrameCounter,
		*Settings.PerfScenario.Replace(TEXT(","), TEXT("_")),
		FrameStats.DeltaMs,
		FrameStats.UpdateMs,
		FrameStats.TreeMs,
		FrameStats.ScheduleMs,
		FrameStats.MeshBuildMs,
		FrameStats.CollisionMs,
		FrameStats.FrameP50Ms,
		FrameStats.FrameP95Ms,
		FrameStats.FrameP99Ms,
		FrameStats.Nodes,
		FrameStats.Resident,
		FrameStats.Drawn,
		FrameStats.Pending,
		FrameStats.ActiveLodFades,
		FrameStats.VisibilityChanges,
		FrameStats.MeshSectionCreates,
		FrameStats.MeshSectionUpdates,
		FrameStats.MeshSectionClears,
		FrameStats.MeshResourceRebuilds,
		FrameStats.BalanceBlocked,
		FrameStats.ActiveCpuMorphPatches,
		FrameStats.CpuMorphUploads,
		FrameStats.CpuMorphUploadMs,
		FrameStats.DetailReadyFaces,
		FrameStats.DetailPending,
		FrameStats.DetailBaking,
		FrameStats.DetailUploads,
		FrameStats.DetailBakeP95Ms,
		FrameStats.DetailUploadMs,
		FrameStats.TileCacheHits,
		FrameStats.TileCacheMisses,
		FrameStats.TileBakes,
		FrameStats.DiskHits,
		FrameStats.DiskMisses,
		FrameStats.DiskWrites,
		FrameStats.GpuResident,
		FrameStats.GpuUploads,
		FrameStats.CollisionResident,
		FrameStats.CollisionDesired,
		FrameStats.CollisionCooked,
		FrameStats.bDrawCoverageOk ? 1 : 0);
	FTCHARToUTF8 LineUtf8(*Line);
	PerfCsvArchive->Serialize(const_cast<ANSICHAR*>(LineUtf8.Get()), LineUtf8.Length());
	if ((FrameCounter % 30) == 0)
	{
		PerfCsvArchive->Flush();
	}
}

void UPlanetTerrainNextComponent::PrintStats() const
{
	if (!GEngine)
	{
		return;
	}
	const FPlanetTerrainNextFrameStats& S = FrameStats;
	if (S.bDiagActive)
	{
		GEngine->AddOnScreenDebugMessage(
			static_cast<uint64>(GetUniqueID()) + 0x7E77A000ull,
			0.0f,
			S.bDrawCoverageOk ? FColor(140, 230, 140) : FColor::Red,
			FString::Printf(
				TEXT("TerrainNext R10 DIAG | patches %d | invalid %d inward %d | cov %s | tick diag (streaming off)"),
				S.DiagPatches, S.DiagInvalidTris, S.DiagInwardFacing,
				S.bDrawCoverageOk ? TEXT("ok") : TEXT("FAIL")));
		return;
	}
	const FString AltitudeText = FMath::Abs(S.AltitudeM) >= 10000.0
		? FString::Printf(TEXT("%.1f km"), S.AltitudeM / 1000.0)
		: FString::Printf(TEXT("%.1f m"), S.AltitudeM);
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(GetUniqueID()) + 0x7E77A000ull,
		0.0f,
		HasFullCoverage() ? FColor(140, 230, 140) : FColor::Red,
		FString::Printf(
			TEXT("TerrainNext R10.3 | alt %s | drawn %d L%d (L0:%d L1:%d L2:%d L3+:%d) fade %d %.2f | cov %s | detail %d/6 p%d | coll %d/%d | tree %.2f sch %.2f mesh %.2f coll %.2f tick %.2fms"),
			*AltitudeText, S.Drawn, S.MaxDrawnLevel,
			S.DrawnL0, S.DrawnL1, S.DrawnL2, S.DrawnL3Plus, S.ActiveLodFades, S.MeanLodFade,
			S.bDrawCoverageOk ? TEXT("ok") : TEXT("HOLE"),
			S.DetailReadyFaces, S.DetailPending,
			S.CollisionResident, S.CollisionDesired,
			S.TreeMs, S.ScheduleMs, S.MeshBuildMs, S.CollisionMs,
			S.UpdateMs));
	GEngine->AddOnScreenDebugMessage(
		static_cast<uint64>(GetUniqueID()) + 0x7E77A001ull,
		0.0f,
		FColor(210, 210, 230),
		FString::Printf(
			TEXT("TerrainNext perf | frame P50/P95/P99 %.2f/%.2f/%.2fms | visible patches %d | sections create/update/clear %d/%d/%d rebuilds %d | balance blocked %d | CPU morph active/uploads/ms %d/%d/%.2f | detail bakeP95/upload ms %.1f/%.1f"),
			S.FrameP50Ms, S.FrameP95Ms, S.FrameP99Ms, S.Drawn,
			S.MeshSectionCreates, S.MeshSectionUpdates, S.MeshSectionClears,
			S.MeshResourceRebuilds,
			S.BalanceBlocked,
			S.ActiveCpuMorphPatches, S.CpuMorphUploads, S.CpuMorphUploadMs,
			S.DetailBakeP95Ms, S.DetailUploadMs));
}
