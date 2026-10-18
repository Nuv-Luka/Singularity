#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetTerrain/PlanetTerrainTypes.h"
#include "PlanetTerrain/PlanetTerrainHeight.h"
#include "PlanetTerrain/PlanetTerrainQuadtree.h"
#include "PlanetTerrain/PlanetTerrainScheduler.h"
#include "PlanetTerrain/PlanetTerrainTweak.h"
#include "PlanetTerrainComponent.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(ClassGroup = (Singularity), meta = (BlueprintSpawnableComponent))
class SINGULARITY_API UPlanetTerrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetTerrainComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void InitializeTerrain(const FRockyPlanetDescriptor& Descriptor, const FVector& PlanetWorldCenter);

private:
	/** Stop legacy jobs and hide legacy meshes while Next owns the moon. */
	void SuspendForNextRenderer();

public:
	void SetPlanetWorldCenter(const FVector& Center);
	void SetCameraPlanetLocal(const FVector3d& PlanetLocalPos, const FVector3d& Forward, const FVector3d& Velocity);

	/**
	 * Recompute DistanceFromCenterM / AltitudeM (above terrain) from a world-space camera.
	 * Call whenever the camera moves — do not wait for component Tick.
	 */
	void RefreshAltitudeMetrics(const FVector& CameraWorld);

	/**
	 * Legacy API — no longer changes physical scale. Continuous traversal keeps
	 * UuPerMeter = PugCmPerM. Prefer ApplyAltitudeLodPolicy.
	 */
	void SetSurfacePlayMode(bool bEnabled);
	bool IsSurfacePlayMode() const { return false; }
	double GetUuPerMeter() const { return UuPerMeter; }
	double GetRenderRadiusUu() const { return Descriptor.RadiusMeters * UuPerMeter; }

	/** Destroy every patch mesh (keeps distant shell). */
	void ClearPatchMeshes();

	/**
	 * Legacy mode switch. Does NOT teleport or shrink the moon.
	 * At most refreshes LOD policy / distant shell without wiping residency.
	 */
	void RebuildForMode(bool bSurfacePlay);

	/** Adjust quadtree thresholds from geometric altitude (no tree wipe). */
	void ApplyAltitudeLodPolicy(double AltitudeMeters);

	/** Planet-relative camera/sim position (metres). */
	FPlanetRelativePosition GetCameraRelativePosition() const;

	/** Request high-priority tiles along predicted impact / underfoot (async). */
	void RequestApproachStreaming(const FVector3d& UnitDir, int32 MinLevel = 4, bool bAsLandingPrep = false);

	const FRockyPlanetDescriptor& GetDescriptor() const { return Descriptor; }
	const FPlanetTerrainHeight* GetHeightField() const { return HeightField.Get(); }
	double GetRadiusMeters() const { return Descriptor.RadiusMeters; }
	FVector GetPlanetWorldCenter() const { return PlanetWorldCenter; }

	/** Height under a planet-local unit direction (unrestricted). */
	double SampleHeight(const FVector3d& UnitDir) const;
	FVector3d SampleSurfacePoint(const FVector3d& UnitDir) const;

	/** World-space point on the terrain surface along UnitDir, plus HoverUu along the outward radial. */
	FVector GetSurfaceWorldLocation(const FVector3d& UnitDir, float HoverUu = 0.0f) const;

	/**
	 * Legacy sync bake (tests / emergency). Prefer BeginLandingPrep + IsLandingPadReady.
	 * Still blocks the game thread — do not call from gameplay land path.
	 */
	bool EnsureLandingPad(const FVector3d& UnitDir, int32 MinLevel = 6);

	/**
	 * Split the quadtree path / neighborhood and mark tiles for async generation
	 * at Underfoot priority. Does not bake on the game thread.
	 */
	bool BeginLandingPrep(const FVector3d& UnitDir, int32 MinLevel = 5);

	/** True when prep tiles (roots + pad neighborhood) are Resident with meshes. */
	bool IsLandingPadReady(const FVector3d& UnitDir, int32 MinLevel = 5) const;

	/** Apply live F6 tweak panel settings (LOD, workers, debug). */
	void ApplyTweakSettings(const FPlanetTerrainTweakSettings& S);

	/** Collision query matching drawn geomorph when possible. */
	bool SampleDrawnHeight(const FVector3d& UnitDir, double& OutHeightM, float& OutMorph) const;

	void RegenerateWithSeed(uint64 NewTerrainSeed);
	void SetLodFrozen(bool bFrozen);
	void SetForceCpu(bool bForce);
	void SetDebugDraw(bool bSeams, bool bFaces, bool bCollision);

	int32 GetActiveLeaves() const;
	int32 GetQueueLength() const;
	int32 GetWorkerCount() const;
	double GetLastBakeMs() const;
	uint64 GetPlanetId() const { return Descriptor.PlanetID; }
	uint64 GetTerrainSeed() const { return Descriptor.TerrainSeed; }
	double GetHeightUnderCamera() const { return HeightUnderCameraM; }
	/** Radial distance from planet center to camera (metres). */
	double GetDistanceFromCenterM() const { return DistanceFromCenterM; }
	/** Geometric altitude above the terrain surface (metres), not above the sphere center. */
	double GetAltitudeAboveSurfaceM() const { return AltitudeM; }
	int32 GetSplitCount() const;
	int32 GetMergeCount() const;
	int32 GetCacheHits() const;
	int32 GetCacheMisses() const;
	/** Streaming v2 counters for HUD / Insights correlation. */
	FString GetStreamingStatsLine() const;
	double GetLastUploadMs() const { return LastUploadMs; }
	double GetUploadMsThisFrame() const { return UploadMsAccum; }
	int32 GetUploadsThisFrame() const { return UploadsThisFrame; }
	double GetLastLandingPadMs() const { return LastLandingPadMs; }

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	TObjectPtr<UMaterialInterface> SurfaceMaterial;

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	float LodDistanceFactor = 3.2f;

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	int32 MaxActivePatches = 192;

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	int32 PatchResolution = 32;

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	bool bEnableCollision = true;

	UPROPERTY(EditAnywhere, Category = "PlanetTerrain")
	bool bShowDebugHud = false;

	/** Seconds for child tiles to morph from parent-displayed coarse → fine (0 = off). */
	UPROPERTY(EditAnywhere, Category = "PlanetTerrain", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MorphDurationSeconds = 0.5f;

	/** Soft game-thread budget for CreateMeshSection commits (ms/frame). */
	UPROPERTY(EditAnywhere, Category = "PlanetTerrain", meta = (ClampMin = "0.25", ClampMax = "8.0"))
	float UploadBudgetMs = 5.0f;

	/** Frames to keep a non-visible GPU mesh before destroying the PMC. */
	UPROPERTY(EditAnywhere, Category = "PlanetTerrain", meta = (ClampMin = "0", ClampMax = "1200"))
	int32 GpuMeshGraceFrames = 90;

	/** Last frame we called SetNewWorldOrigin (prevents per-frame rebase thrash). */
	int32 LastWorldOriginRebaseFrame = -100000;

protected:
	void OnTileReady(TSharedPtr<FPlanetTerrainTileData> Tile);
	void UploadTileMesh(FPlanetTerrainNode& Node, const FPlanetTerrainTileData& Data);
	void BuildDistantFallback();
	void UpdateDistantVisibility(double CamDistM);
	void UpdateWorldOriginIfNeeded(const FVector3d& CamPlanetLocal);
	/**
	 * Authoritative camera → planet-local (metres). Must run before LOD each tick.
	 * Samples player viewpoint. Fixes PrePhysics/stale-zero camera.
	 */
	void SyncCameraFromPlayerView();
	void DrawDebugOverlays() const;
	void BuildSharedIndices(int32 Resolution);
	void EnsurePatchMesh(FPlanetTerrainNode& Node);
	void EnsureSurfaceMaterialInstance();
	void ConfigureTerrainMeshShadows(UProceduralMeshComponent* Mesh, bool bCast) const;

	/** Enumerate pad coverage nodes (roots + nadir path siblings + 5×5). May Split. */
	void CollectLandingPadNodes(const FVector3d& UnitDir, int32 MinLevel, TArray<FPlanetTerrainNode*>& OutNodes);

	FRockyPlanetDescriptor Descriptor;
	TUniquePtr<FPlanetTerrainHeight> HeightField;
	TUniquePtr<FPlanetTerrainQuadtree> Quadtree;
	TSharedPtr<FPlanetTerrainScheduler, ESPMode::ThreadSafe> Scheduler;

	FVector PlanetWorldCenter = FVector::ZeroVector;
	FVector3d CameraPlanetLocal = FVector3d::ZeroVector;
	FVector3d CameraForward = FVector3d(1, 0, 0);
	FVector3d CameraVelocity = FVector3d::ZeroVector;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> DistantMesh;

	/** Non-UMap: FPlanetTerrainTileKey is not a USTRUCT (UHT). Meshes kept alive via PatchMeshList. */
	TMap<FPlanetTerrainTileKey, TObjectPtr<UProceduralMeshComponent>> PatchMeshes;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> PatchMeshList;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;

	TArray<int32> SharedIndices;
	TArray<int32> SharedSkirtIndices;
	int32 SharedIndexResolution = -1;
	int32 FrameIndex = 0;
	uint32 Epoch = 1;
	double HeightUnderCameraM = 0.0;
	double DistanceFromCenterM = 0.0;
	double AltitudeM = 0.0;
	bool bDebugSeams = false;
	bool bDebugFaces = false;
	bool bDebugCollision = false;
	bool bGeomorphEnabled = true;
	FVector LastRebasingOrigin = FVector::ZeroVector;
	/** Deprecated flag — always false under continuous traversal. */
	bool bSurfacePlayMode = false;
	double UuPerMeter = 100.0; // always PugCmPerM (physical cm)
	/** Brief freeze after aggressive pad prep (optional). */
	float SurfaceLodFreezeSeconds = 0.0f;
	int32 LastLodPolicyBand = -1;
	FVector3d PredictedImpactDir = FVector3d(0, 0, 1);

	/** Instrumentation (game-thread mesh commit / landing stall). */
	double LastUploadMs = 0.0;
	double UploadMsAccum = 0.0;
	int32 UploadsThisFrame = 0;
	double LastLandingPadMs = 0.0;

	TSet<FPlanetTerrainTileKey> LandingPrepKeys;
	FVector3d LandingPrepDir = FVector3d(0, 0, 1);
	int32 LandingPrepMinLevel = 5;
	bool bLandingPrepActive = false;

	/** Nodes with ReadyForUpload tiles waiting for budgeted CreateMeshSection. */
	TArray<FPlanetTerrainTileKey> PendingUploadKeys;
	/** Underfoot tiles awaiting separate collision cook (visual already resident). */
	TArray<FPlanetTerrainTileKey> PendingCollisionKeys;
	void FlushUploadBudget();
	void FlushCollisionBudget();
	void EvictUnusedGpuMeshes(const TSet<FPlanetTerrainTileKey>& KeepKeys);
	void PruneOrphanPatchMeshes();
	void DestroyPatchMesh(const FPlanetTerrainTileKey& Key);
	/** Max morph of a leaf covering Dir (edge lock between neighbors). */
	float SampleLeafMorphAtDir(const FVector3d& UnitDir) const;
};
