#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetTerrainNextCollisionManager.h"
#include "PlanetTerrainNextGpuSlotPool.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextQuadtree.h"
#include "PlanetTerrainNextSettings.h"
#include "PlanetTerrainNextTileCache.h"
#include "PlanetTerrainNextTilePlanner.h"
#include "PlanetTerrainNextTileScheduler.h"
#include "PlanetTerrainNextTypes.h"
#include "PlanetTerrainNextComponent.generated.h"

class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
struct FPlanetTerrainNextPatchData;

struct FPlanetTerrainNextFrameStats
{
	int32 Nodes = 0;
	int32 Resident = 0;
	int32 Drawn = 0;
	int32 Pending = 0;
	int32 BuiltThisFrame = 0;
	int32 VisibilityChanges = 0;
	int32 Splits = 0;
	int32 Merges = 0;
	int32 BudgetBlocked = 0;
	int32 MaxDrawnLevel = 0;
	int32 Slots = 0;
	int32 MorphUniformUpdates = 0;
	int32 TileCacheHits = 0;
	int32 TileCacheMisses = 0;
	int32 TileBakes = 0;
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
	double AltitudeM = 0.0;
	float MeanMorph = 0.0f;
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
	/** True when every face root is resident, i.e. the planet is fully covered. */
	bool HasFullCoverage() const;

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
	struct FSlot
	{
		TObjectPtr<UProceduralMeshComponent> Mesh = nullptr;
		TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;
		FPlanetTerrainNextNodeId Id;
		FPlanetTerrainNextTileId TileId;
		int32 GpuSlot = INDEX_NONE;
		float Morph = 0.0f;
		double MaxAbsCoarseDeltaCm = 0.0;
		bool bInUse = false;
		bool bHasSection = false;
		bool bShown = false;
	};

	FPlanetTerrainNextTileId MakeTileId(const FPlanetTerrainNextNodeId& Id) const;
	int32 AcquireSlot();
	void ReleaseNode(const FPlanetTerrainNextNodeId& Id);
	bool BuildNode(const FPlanetTerrainNextNodeId& Id);
	void UploadPatch(FSlot& Slot, const FPlanetTerrainNextPatchData& Patch);
	void ApplyDrawSet(const TArray<FPlanetTerrainNextDrawItem>& DrawSet);
	void ApplyMorph(const TArray<FPlanetTerrainNextDrawItem>& DrawSet);
	void RefreshTilePins(const TArray<FPlanetTerrainNextDrawItem>& DrawSet);
	void UpdateCollision(const FPlanetTerrainNextView& View);
	bool CookCollisionNode(const FPlanetTerrainNextNodeId& Id);
	void ReleaseCollisionNode(const FPlanetTerrainNextNodeId& Id);
	void HideAll();
	void ApplyDebugColor(FSlot& Slot) const;
	void RefreshDebugColors();
	bool GetView(FPlanetTerrainNextView& OutView) const;
	void RecordBuildTime(double Milliseconds);
	void PrintStats() const;

	FPlanetTerrainNextDescriptor Descriptor;
	/** Thread-safe: bake workers sample the pure height field concurrently. */
	TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe> HeightField;
	FPlanetTerrainNextQuadtree Quadtree;
	/** Shared: UCLASS members must be copyable; the cache holds non-copyable maps internally. */
	TSharedPtr<FPlanetTerrainNextTileCache> TileCache;
	TSharedPtr<FPlanetTerrainNextGpuSlotPool> GpuPool;
	TSharedPtr<FPlanetTerrainNextTileScheduler> Scheduler;
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
	/** Collision-only meshes (invisible, QueryAndPhysics). Independent of visual LOD. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> CollisionMeshes;

	TWeakObjectPtr<UPrimitiveComponent> SuspendedProxy;

	TArray<FSlot> Slots;
	TArray<int32> FreeSlots;
	TMap<FPlanetTerrainNextNodeId, int32> Resident;
	TSet<FPlanetTerrainNextNodeId> Drawn;
	TMap<FPlanetTerrainNextNodeId, int32> CollisionResident;
	TArray<int32> FreeCollisionSlots;

	TArray<double> BuildTimesMs;
	int32 BuildTimeCursor = 0;
	FPlanetTerrainNextFrameStats FrameStats;
	EPlanetTerrainNextDebugColor AppliedDebugColor = EPlanetTerrainNextDebugColor::Regolith;
	bool bInitialized = false;
	bool bSuspended = false;
};
