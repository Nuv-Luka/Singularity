#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Components/SceneComponent.h"
#include "PlanetTerrainNextCollisionManager.h"
#include "PlanetTerrainNextDetailStore.h"
#include "PlanetTerrainNextDiskTileStore.h"
#include "PlanetTerrainNextGpuSlotPool.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextLodFade.h"
#include "PlanetTerrainNextQuadtree.h"
#include "PlanetTerrainNextSettings.h"
#include "PlanetTerrainNextTileCache.h"
#include "PlanetTerrainNextTilePlanner.h"
#include "PlanetTerrainNextTileScheduler.h"
#include "PlanetTerrainNextTypes.h"
#include "PlanetTerrainNextComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture2D;
class FArchive;
struct FPlanetTerrainNextPatchData;

struct FPlanetTerrainNextFrameStats
{
	int32 Nodes = 0;
	int32 Resident = 0;
	int32 Drawn = 0;
	int32 Pending = 0;
	int32 BuiltThisFrame = 0;
	int32 VisibilityChanges = 0;
	int32 ShadowStateChanges = 0;
	int32 DeferredMissingTiles = 0;
	int32 Splits = 0;
	int32 Merges = 0;
	int32 BudgetBlocked = 0;
	int32 BalanceBlocked = 0;
	int32 MaxDrawnLevel = 0;
	/** Drawn patch counts for levels 0..3 (diagnose L0/L1 unload vs holes). */
	int32 DrawnL0 = 0;
	int32 DrawnL1 = 0;
	int32 DrawnL2 = 0;
	int32 DrawnL3Plus = 0;
	/** Parents kept visible until replacing children have been shown at least once. */
	int32 HandoffParentsKept = 0;
	/** Logical draw-set face area coverage (ignores one-frame handoff overlap). */
	bool bDrawCoverageOk = true;
	int32 Slots = 0;
	int32 MorphUniformUpdates = 0;
	int32 MeshSectionCreates = 0;
	int32 MeshSectionUpdates = 0;
	int32 MeshSectionClears = 0;
	int32 MeshResourceRebuilds = 0;
	int32 ActiveCpuMorphPatches = 0;
	int32 CpuMorphUploads = 0;
	double CpuMorphUploadMs = 0.0;
	int32 TileCacheHits = 0;
	int32 TileCacheMisses = 0;
	int32 TileBakes = 0;
	int32 DiskHits = 0;
	int32 DiskMisses = 0;
	int32 DiskWrites = 0;
	int32 GpuResident = 0;
	int32 GpuUploads = 0;
	int32 GpuEvictions = 0;
	int32 GpuAllocFailed = 0;
	int32 SchedulerQueued = 0;
	int32 SchedulerBaking = 0;
	int32 SchedulerStarted = 0;
	int32 SchedulerCompleted = 0;
	int32 SchedulerCancelled = 0;
	int32 SchedulerStale = 0;
	int32 CollisionDesired = 0;
	int32 CollisionResident = 0;
	int32 CollisionCooked = 0;
	int32 CollisionLevel = 0;
	double CollisionCookP95Ms = 0.0;
	double LastBuildMs = 0.0;
	double P95BuildMs = 0.0;
	double TileBakeP95Ms = 0.0;
	double TreeMs = 0.0;
	double ScheduleMs = 0.0;
	double MeshBuildMs = 0.0;
	double CollisionMs = 0.0;
	double UpdateMs = 0.0;
	double DeltaMs = 0.0;
	double FrameP50Ms = 0.0;
	double FrameP95Ms = 0.0;
	double FrameP99Ms = 0.0;
	double AltitudeM = 0.0;
	float MeanMorph = 0.0f;
	/** R9.3 detail projection */
	int32 DetailReadyFaces = 0;
	int32 DetailPending = 0;
	int32 DetailBaking = 0;
	int32 DetailUploads = 0;
	double DetailBakeP95Ms = 0.0;
	double DetailUploadMs = 0.0;
	/** R10.1 diagnostic */
	bool bDiagActive = false;
	int32 DiagPatches = 0;
	int32 DiagInvalidTris = 0;
	int32 DiagInwardFacing = 0;
	/** R10.2 dither fades */
	int32 ActiveLodFades = 0;
	float MeanLodFade = 0.0f;
};

/**
 * Runtime terrain: six-face quadtree, CPU patches with fine+coarse attributes,
 * shader morph driven by a per-slot scalar (no per-frame mesh rebuilds).
 *
 * Each resident node owns one pooled ProceduralMeshComponent at the patch center.
 * A slot's section is created once and refilled in place (same topology).
 * Morph changes SetScalarParameterValue only.
 */
UCLASS(ClassGroup = (Singularity), meta = (BlueprintSpawnableComponent))
class SINGULARITYPLANETTERRAINNEXT_API UPlanetTerrainNextComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetTerrainNextComponent();
	virtual ~UPlanetTerrainNextComponent() override;

	/** Rebuilds the height field and tree, then builds the six roots synchronously. */
	void InitializeTerrain(const FPlanetTerrainNextDescriptor& InDescriptor);
	void ShutdownTerrain();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

	bool IsTerrainActive() const { return bInitialized; }
	/**
	 * True when the logical draw set partitions all six faces (area ≈ 1 each).
	 * Roots staying resident while refined is not enough — use this for hole detection.
	 */
	bool HasFullCoverage() const;

	/**
	 * LOD handoff without overlap (overlap z-fights as a triangle checkerboard).
	 * Holds the parent for HoldFrames while children stay stripped, then commits children.
	 * InOutHoldFrames maps parent → remaining hold frames.
	 */
	static int32 ApplyLodHandoff(
		const TSet<FPlanetTerrainNextNodeId>& PreviousDrawn,
		TSet<FPlanetTerrainNextNodeId>& InOutNext,
		TMap<FPlanetTerrainNextNodeId, int32>& InOutHoldFrames,
		int32 HoldFrames,
		TFunctionRef<bool(const FPlanetTerrainNextNodeId&)> IsParentResident,
		TFunctionRef<bool(const FPlanetTerrainNextNodeId&)> IsChildShown);

	/** Height of the collision/fine surface below WorldPointUu, in meters above it. */
	double GetAltitudeMeters(const FVector& WorldPointUu) const;
	/** Surface height (m above base radius) under a world point; uses collision tiles when resident. */
	bool TrySampleSurfaceHeightM(const FVector& WorldPointUu, double& OutHeightM) const;
	double GetRadiusMeters() const { return Descriptor.RadiusMeters; }
	const FPlanetTerrainNextCollisionManager& GetCollisionManager() const { return CollisionManager; }

	/** Shown while the renderer mode is Legacy (no patches drawn), hidden while patches are drawn. */
	void SetSuspendedProxy(UPrimitiveComponent* Proxy) { SuspendedProxy = Proxy; }

	const FPlanetTerrainNextFrameStats& GetFrameStats() const { return FrameStats; }
	const FPlanetTerrainNextHeightField* GetHeightField() const { return HeightField.Get(); }

private:
	enum class ELodTransition : uint8
	{
		Stable = 0,
		WaitingForChildren,
		Splitting,
		Merging
	};

	struct FSlot
	{
		TObjectPtr<UProceduralMeshComponent> Mesh = nullptr;
		TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;
		TObjectPtr<UMaterialInstanceDynamic> TransitionMaterial = nullptr;
		FPlanetTerrainNextNodeId Id;
		FPlanetTerrainNextTileId TileId;
		int32 GpuSlot = INDEX_NONE;
		float Morph = 0.0f;
		/** 0 = locked to parent (Morph→1), 1 = fully driven by pixel-error target. */
		float MorphReveal = 1.0f;
		double LastPixelError = 0.0;
		ELodTransition LodTransition = ELodTransition::Stable;
		double MaxAbsCoarseDeltaCm = 0.0;
		/**
		 * Optional legacy CPU morph path: bake morph into positions where WPO UVs are unsafe.
		 * Fine/Coarse arrays are kept only while that path is enabled.
		 */
		bool bCpuMorph = false;
		TArray<FVector> FineCm;
		TArray<FVector> CoarseCm;
		TArray<FVector> FineNormals;
		TArray<FVector> CoarseNormals;
		TArray<FVector> Tangents;
		TArray<FVector2D> BaseUVs;
		TArray<FLinearColor> BaseColors;
		bool bInUse = false;
		bool bHasSection = false;
		bool bShown = false;
		bool bAppliedCastShadow = false;
		bool bTransitionMaterialActive = false;
		/** Matches PlanetTerrainNextPatch::GetIndexBufferVersion() at last Create. */
		int32 IndexBufferVersion = -1;
		int32 SectionVertexCount = 0;
		int32 SectionIndexCount = 0;
		float LastLodFadeParam = -1.0f;
		float LastLodFadeRoleParam = -1.0f;
	};

	FPlanetTerrainNextTileId MakeTileId(const FPlanetTerrainNextNodeId& Id) const;
	int32 AcquireSlot();
	void ReleaseNode(const FPlanetTerrainNextNodeId& Id);
	void ClearSlotSection(FSlot& Slot);
	bool BuildNode(const FPlanetTerrainNextNodeId& Id);
	bool BuildDiagNode(const FPlanetTerrainNextNodeId& Id, bool bFlat, bool bForceFresh, UMaterialInterface* DiagMaterial);
	void UploadPatch(FSlot& Slot, const FPlanetTerrainNextPatchData& Patch);
	void UploadCpuMorphedSection(FSlot& Slot);
	/** bAllowForceFresh: true on BuildNode upload; false on per-frame morph Update. */
	bool ShouldRecreateSection(const FSlot& Slot, bool bAllowForceFresh, int32 VertexCount, int32 IndexCount) const;
	void TickDiagnostic(float DeltaSeconds);
	void ApplySuspendedProxyVisibility(bool bShowProxy);
	void ApplyDrawSet(const TArray<FPlanetTerrainNextDrawItem>& DrawSet, float DeltaSeconds, const FPlanetTerrainNextView* View = nullptr);
	void ApplyDrawSetExclusive(TSet<FPlanetTerrainNextNodeId>& Next);
	void ApplyDrawSetDithered(const TSet<FPlanetTerrainNextNodeId>& Desired, float DeltaSeconds, const FPlanetTerrainNextView* View);
	void FlushPendingPhysicalReleases();
	void SyncVisibilityToRenderSet(const TSet<FPlanetTerrainNextNodeId>& Render);
	void ReconcileShadowState(FSlot& Slot, bool bInFade, bool bDominant);
	void ApplyLodFadeParams();
	bool CheckDrawCoverage(const TArray<FPlanetTerrainNextDrawItem>& DrawSet) const;
	/** Morph only patches that are actually shown after handoff (R9.2). */
	void ApplyMorph(const TArray<FPlanetTerrainNextDrawItem>& DrawSet, float DeltaSeconds);
	void BeginVisibleHandoff(FSlot& Slot);
	int32 FindLodFadeIndex(const FPlanetTerrainNextNodeId& Parent) const;
	void RefreshTilePins(const TArray<FPlanetTerrainNextDrawItem>& DrawSet);
	void UpdateCollision(const FPlanetTerrainNextView& View);
	bool CookCollisionNode(const FPlanetTerrainNextNodeId& Id);
	void ReleaseCollisionNode(const FPlanetTerrainNextNodeId& Id);
	void HideAll();
	void ApplyDebugColor(FSlot& Slot) const;
	void ApplyDetailParams(FSlot& Slot) const;
	void RefreshDetailParams();
	void RefreshDebugColors();
	bool GetView(FPlanetTerrainNextView& OutView) const;
	void RecordBuildTime(double Milliseconds);
	void WritePerfSample();
	void PrintStats() const;

	FPlanetTerrainNextDescriptor Descriptor;
	/** Thread-safe: bake workers sample the pure height field concurrently. */
	TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe> HeightField;
	FPlanetTerrainNextQuadtree Quadtree;
	/** Shared: UCLASS members must be copyable; the cache holds non-copyable maps internally. */
	TSharedPtr<FPlanetTerrainNextTileCache> TileCache;
	TSharedPtr<FPlanetTerrainNextDiskTileStore> DiskTileStore;
	TSharedPtr<FPlanetTerrainNextGpuSlotPool> GpuPool;
	TSharedPtr<FPlanetTerrainNextTileScheduler> Scheduler;
	TSharedPtr<FPlanetTerrainNextDetailStore> DetailStore;
	FPlanetTerrainNextTilePlanner Planner;
	FPlanetTerrainNextCollisionManager CollisionManager;
	TArray<double> GeometricErrorByLevel;
	FPlanetTerrainNextRuntimeSettings Settings;
	int64 FrameCounter = 0;

	/** Pooled patch components; owned through the actor, kept alive by this array. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> SlotMeshes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SlotMaterials;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SlotTransitionMaterials;
	/** Collision-only meshes (invisible, QueryAndPhysics). Independent of visual LOD. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> CollisionMeshes;
	/** R9.3: keep root detail textures alive for GC (owned by DetailStore creation). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> DetailAlbedoTextures;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> DetailNormalTextures;

	TWeakObjectPtr<UPrimitiveComponent> SuspendedProxy;

	TArray<FSlot> Slots;
	TArray<int32> FreeSlots;
	TMap<FPlanetTerrainNextNodeId, int32> Resident;
	TSet<FPlanetTerrainNextNodeId> Drawn;
	/** Quadtree-retired nodes kept alive until a merge fade no longer needs them. */
	TSet<FPlanetTerrainNextNodeId> PendingPhysicalReleases;
	/** Parents held N frames so children replace them without co-drawing / hard pops. */
	TMap<FPlanetTerrainNextNodeId, int32> HandoffHoldFrames;
	/** R10.2: active dither fade groups (sibling Progress shared). */
	TArray<FPlanetTerrainNextLodFade> LodFades;
	TMap<FPlanetTerrainNextNodeId, int32> CollisionResident;
	TArray<int32> FreeCollisionSlots;

	TArray<double> BuildTimesMs;
	int32 BuildTimeCursor = 0;
	TArray<double> FrameTimeMsSamples;
	int32 FrameTimeSampleCursor = 0;
	int32 FrameTimeSortCounter = 0;
	FPlanetTerrainNextFrameStats FrameStats;
	EPlanetTerrainNextDebugColor AppliedDebugColor = EPlanetTerrainNextDebugColor::Regolith;
	bool bInitialized = false;
	bool bSuspended = false;
	/** Only root coverage may synchronously bake during InitializeTerrain. */
	bool bAllowSynchronousBootstrap = false;
	/** Last frame's DetailProjection CVar — detect toggles for MID refresh. */
	bool bDetailProjectionLive = false;
	/** R10.1: last diag rebuild key; INDEX_NONE when diag off. */
	uint32 DiagRebuildKey = 0;
	bool bDiagWasEnabled = false;
	/** R10 opt-in frame telemetry writer; null unless PerfCapture is enabled. */
	FArchive* PerfCsvArchive = nullptr;
	bool bPerfCsvHeaderWritten = false;
};
