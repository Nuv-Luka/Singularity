#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain/PlanetTerrainTile.h"

struct FPlanetTerrainNode
{
	FPlanetTerrainTileKey Key;
	uint8 Face = 0;
	uint8 Level = 0;
	uint32 X = 0;
	uint32 Y = 0;
	double U0 = 0.0, V0 = 0.0, U1 = 1.0, V1 = 1.0;
	FVector3d CenterLocal = FVector3d::ZeroVector;
	FVector3d CenterSurfaceLocal = FVector3d::ZeroVector;
	double WorldSizeM = 1.0;

	TSharedPtr<FPlanetTerrainTileData> Tile;
	TUniquePtr<FPlanetTerrainNode> Children[4];
	bool bGenPending = false;
	bool bDisposed = false;
	float DrawMorph = 0.0f;
	float TemporalMorph = 0.0f;
	float UploadedMorph = -1.0f;
	int32 DrawnFrame = -1;
	bool bVisibleLeaf = false;
	bool bChildrenPromoted = false;
	/** Last projected screen error (px) — used for scheduler priority. */
	float ScreenErrorPx = 0.0f;
	/** Continuous foveation weight 1=center … 0=periphery (screen-radius). */
	float FoveationWeight = 1.0f;
	bool bFrustumVisible = true;
	bool bHorizonVisible = true;
	/** Bounds overlap frustum (may be true even when center is offscreen). */
	bool bBoundsInFrustum = true;
	EPlanetTerrainRejectReason RejectReason = EPlanetTerrainRejectReason::None;
	/** True when this node is required for full-planet coverage (roots / hole fill). */
	bool bMandatoryCoverage = false;
};

struct FPlanetTerrainLodParams
{
	double LodDistanceFactor = 2.4; // legacy fallback when screen-space disabled
	double MergeHysteresis = 1.35;
	int32 MaxLevel = 12;
	int32 MaxActivePatches = 192;
	int32 MinNeighborhoodLevel = 2;
	double ActivateDistanceM = 0.0;

	/** Streaming v2: projected geometric error (primary). */
	bool bUseScreenSpaceLod = true;
	double TargetPixelError = 2.0;
	double PeripheralPixelError = 5.0;
	double PrefetchPixelError = 12.0;

	/** Screen-radius foveation (normalized viewport radius). */
	double FoveaInnerRadius = 0.35;
	double FoveaOuterRadius = 0.9;

	double GeometricErrorAmpFrac = 0.35;
	double GeometricErrorSizeFrac = 0.18;
	double HorizonReliefMarginM = 8000.0;
	/** Extra angular margin (rad) for conservative frustum / horizon. */
	double CullAngularMarginRad = 0.12;
	/** Min forward·dir when testing tile corners (prefetch looser). */
	double FrustumMinDot = -0.15;
	double FrustumPrefetchMinDot = -0.45;
};

struct FPlanetTerrainCameraQuery
{
	FVector3d PlanetLocalPosition = FVector3d::ZeroVector;
	FVector3d Forward = FVector3d(1, 0, 0);
	FVector3d Right = FVector3d(0, 1, 0);
	FVector3d Up = FVector3d(0, 0, 1);
	FVector3d Velocity = FVector3d::ZeroVector;
	double NearClip = 1.0;
	double VerticalFovRad = 1.047; // ~60°
	double AspectRatio = 16.0 / 9.0;
	double ScreenHeightPx = 1080.0;
	double TerrainAmplitudeM = 5000.0;
	bool bHasFrustum = true;
};

class FPlanetTerrainQuadtree
{
public:
	void Initialize(uint64 PlanetId, uint32 Epoch, double RadiusMeters, const FPlanetTerrainLodParams& Params);
	void Reset(uint32 NewEpoch);

	void UpdateLod(const FPlanetTerrainCameraQuery& Camera, const FPlanetTerrainHeight& Height, int32 FrameIndex);

	void CollectGenerationRequests(TArray<FPlanetTerrainNode*>& OutRequests) const;
	void CollectDrawLeaves(TArray<FPlanetTerrainNode*>& OutLeaves) const;
	/** Keys that must stay resident (draw leaves + parents of pending children + roots). */
	void CollectCoveragePinKeys(TSet<FPlanetTerrainTileKey>& OutPins) const;

	FPlanetTerrainNode* FindLeafContaining(const FVector3d& UnitDir) const;
	FPlanetTerrainNode* FindNodeAlongPath(const FVector3d& UnitDir, int32 MinLevel) const;
	FPlanetTerrainNode* FindParent(const FPlanetTerrainTileKey& Key) const;
	FPlanetTerrainNode* EnsurePathToLevel(const FVector3d& UnitDir, int32 MinLevel);

	static bool DirectionToFaceUV(const FVector3d& UnitDir, uint8& OutFace, double& OutU, double& OutV);

	const TArray<TUniquePtr<FPlanetTerrainNode>>& GetRoots() const { return Roots; }

	int32 GetSplitCount() const { return SplitCount; }
	int32 GetMergeCount() const { return MergeCount; }
	int32 GetActiveLeafCount() const { return ActiveLeafCount; }
	uint32 GetEpoch() const { return Epoch; }
	bool IsFrozen() const { return bFrozen; }
	void SetFrozen(bool bIn) { bFrozen = bIn; }

	void SetLodParams(const FPlanetTerrainLodParams& Params) { Lod = Params; }
	const FPlanetTerrainLodParams& GetLodParams() const { return Lod; }

	int32 GetFrustumCulledCount() const { return FrustumCulledCount; }
	int32 GetHorizonCulledCount() const { return HorizonCulledCount; }
	int32 GetScreenErrorSplitCount() const { return ScreenErrorSplitCount; }
	int32 GetBudgetExhaustedCount() const { return BudgetExhaustedCount; }
	/** True when all six face roots have Resident meshes (shell may hide). */
	bool HasFullRootCoverage() const;
	/** Mark face roots as mandatory gen targets (safe with invalid/zero camera). */
	void EnsureMandatoryRootCoverage();
	/**
	 * After LOD walk: force-split coarser neighbors so draw-leaf ΔLOD ≤ 1
	 * (skirts remain fallback for T-junctions / face edges).
	 */
	void EnforceNeighborLodDelta(int32& PatchBudget);

private:
	TArray<TUniquePtr<FPlanetTerrainNode>> Roots;
	FPlanetTerrainLodParams Lod;
	uint64 PlanetId = 0;
	uint32 Epoch = 0;
	double RadiusM = 1.0;
	int32 SplitCount = 0;
	int32 MergeCount = 0;
	int32 ActiveLeafCount = 0;
	int32 FrustumCulledCount = 0;
	int32 HorizonCulledCount = 0;
	int32 ScreenErrorSplitCount = 0;
	int32 BudgetExhaustedCount = 0;
	bool bFrozen = false;

	void ProcessNode(FPlanetTerrainNode& Node, const FPlanetTerrainCameraQuery& Camera, const FPlanetTerrainHeight& Height, int32 FrameIndex, int32& PatchBudget);
	void Split(FPlanetTerrainNode& Node);
	void DisposeChildren(FPlanetTerrainNode& Node);
	static bool AllChildrenReady(const FPlanetTerrainNode& Node);
	static FPlanetTerrainTileKey MakeKey(uint64 PlanetId, uint32 Epoch, uint8 Face, uint8 Level, uint32 X, uint32 Y);

	double EstimateGeometricErrorM(const FPlanetTerrainNode& Node, double AmplitudeM) const;
	double ProjectErrorPx(double GeometricErrorM, double DistM, const FPlanetTerrainCameraQuery& Camera) const;
	double TileHalfAngleRad(const FPlanetTerrainNode& Node) const;
	void EvalCornerDirs(const FPlanetTerrainNode& Node, TArray<FVector3d, TInlineAllocator<5>>& OutDirs) const;
	bool PassesHorizonBounds(const FPlanetTerrainNode& Node, const FPlanetTerrainCameraQuery& Camera, double AmplitudeM) const;
	bool PassesFrustumBounds(const FPlanetTerrainNode& Node, const FPlanetTerrainCameraQuery& Camera, bool bPrefetch) const;
	double ScreenRadiusFoveationWeight(const FVector3d& TileSurface, const FPlanetTerrainCameraQuery& Camera) const;
};
