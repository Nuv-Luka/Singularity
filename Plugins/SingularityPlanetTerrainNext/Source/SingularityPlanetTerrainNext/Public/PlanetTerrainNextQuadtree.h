#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** Quadtree node identity on one cube face. Level L has 2^L nodes per face edge. */
struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextNodeId
{
	uint8 Face = 0;
	uint8 Level = 0;
	uint32 X = 0;
	uint32 Y = 0;

	FPlanetTerrainNextNodeId() = default;
	FPlanetTerrainNextNodeId(uint8 InFace, uint8 InLevel, uint32 InX, uint32 InY)
		: Face(InFace), Level(InLevel), X(InX), Y(InY)
	{
	}

	bool operator==(const FPlanetTerrainNextNodeId& Other) const
	{
		return Face == Other.Face && Level == Other.Level && X == Other.X && Y == Other.Y;
	}
	bool operator!=(const FPlanetTerrainNextNodeId& Other) const
	{
		return !(*this == Other);
	}

	/** 0 = (low X, low Y), 1 = high X, 2 = high Y, 3 = both. */
	FPlanetTerrainNextNodeId Child(int32 Index) const
	{
		return FPlanetTerrainNextNodeId(Face, static_cast<uint8>(Level + 1), X * 2 + (Index & 1), Y * 2 + ((Index >> 1) & 1));
	}
	int32 ChildIndex() const
	{
		return static_cast<int32>((X & 1u) | ((Y & 1u) << 1));
	}
	double Size() const
	{
		return 1.0 / static_cast<double>(1ull << Level);
	}
	double U0() const { return static_cast<double>(X) * Size(); }
	double V0() const { return static_cast<double>(Y) * Size(); }
	double U1() const { return static_cast<double>(X + 1) * Size(); }
	double V1() const { return static_cast<double>(Y + 1) * Size(); }
	bool ContainsUV(double U, double V) const
	{
		return U >= U0() && U <= U1() && V >= V0() && V <= V1();
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("F%d L%d (%u,%u)"), Face, Level, X, Y);
	}

	friend uint32 GetTypeHash(const FPlanetTerrainNextNodeId& Id)
	{
		return HashCombine(HashCombine(::GetTypeHash(static_cast<uint32>(Id.Face) | (static_cast<uint32>(Id.Level) << 8)), ::GetTypeHash(Id.X)), ::GetTypeHash(Id.Y));
	}
};

/** Camera in planet-local meters. */
struct FPlanetTerrainNextView
{
	FVector3d CameraM = FVector3d(0.0, 0.0, 1.0e8);
	FVector3d Forward = FVector3d(0.0, 0.0, -1.0);
	/** Half-angle of the cone that encloses the view frustum (radians). */
	double HalfDiagonalFovRad = 0.9;
	/** Pixels per unit of (size / distance): viewport height / (2 tan(vertical FOV / 2)). */
	double ProjectionScalePx = 960.0;
	double TimeSeconds = 0.0;
};

struct FPlanetTerrainNextLodSettings
{
	double TargetPixelError = 4.0;
	/** Merge when the projected error falls below Target / MergeHysteresis. */
	double MergeHysteresis = 1.5;
	int32 MaxLevel = 17;
	/** Hard cap on tree nodes (each node owns one resident patch). */
	int32 MaxNodes = 512;
	/** Seconds an offscreen split survives before merging (also after horizon fade). */
	double InvisibleGraceSeconds = 2.0;
	/** Nodes this close to the camera nadir (geodesic) count as visible. */
	double SafetyRadiusMinM = 5000.0;
	bool bFreeze = false;
	/**
	 * Optional peripheral LOD weight (R8). Multiplies projected pixel error by
	 * Lerp(1, PeripheralLodScale, edge) where edge=0 at view center and 1 at frustum rim.
	 * Values < 1 coarsen the periphery (saves splits). 1 = disabled (no bias).
	 */
	double PeripheralLodScale = 1.0;
};

struct FPlanetTerrainNextPendingBuild
{
	FPlanetTerrainNextNodeId Id;
	double Priority = 0.0;
};

/** A node that should be drawn this frame, with the SSE used for morph. */
struct FPlanetTerrainNextDrawItem
{
	FPlanetTerrainNextNodeId Id;
	double PixelError = 0.0;
};

struct FPlanetTerrainNextQuadtreeStats
{
	int32 Nodes = 0;
	int32 Splits = 0;
	int32 Merges = 0;
	int32 BudgetBlocked = 0;
	int32 VisibleNodes = 0;
};

/**
 * Six-face coverage tree. Owns hierarchy, split/merge decisions, and bounds only.
 * Mesh readiness is supplied by the caller. No GPU resources, no workers.
 *
 * Invariants:
 * - The six roots always exist.
 * - A node gets children only while it is itself ready, so merging back never opens a hole.
 * - A node is drawn instead of its children until all four children are covered.
 * - Offscreen nodes never split. Splits are chosen by screen-space error only.
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextQuadtree
{
public:
	using FReadyFn = TFunctionRef<bool(const FPlanetTerrainNextNodeId&)>;

	FPlanetTerrainNextQuadtree();
	~FPlanetTerrainNextQuadtree();
	FPlanetTerrainNextQuadtree(const FPlanetTerrainNextQuadtree&) = delete;
	FPlanetTerrainNextQuadtree& operator=(const FPlanetTerrainNextQuadtree&) = delete;

	/** GeometricErrorByLevel[L] = estimated mesh error (m) of a level-L patch. */
	void Initialize(double RadiusM, double MinHeightM, double MaxHeightM, const TArray<double>& GeometricErrorByLevel);
	bool IsInitialized() const { return Roots[0].IsValid(); }

	/** Sampled height range of a built patch. Tightens bounds for this node and new children. */
	void SetNodeHeightRange(const FPlanetTerrainNextNodeId& Id, double MinHeightM, double MaxHeightM);

	void Update(const FPlanetTerrainNextView& View, const FPlanetTerrainNextLodSettings& Settings, FReadyFn IsReady, TArray<FPlanetTerrainNextNodeId>& OutReleased);
	void CollectDrawSet(FReadyFn IsReady, TArray<FPlanetTerrainNextNodeId>& OutDraw) const;
	void CollectDrawSet(FReadyFn IsReady, TArray<FPlanetTerrainNextDrawItem>& OutDraw) const;
	void CollectPending(FReadyFn IsReady, TArray<FPlanetTerrainNextPendingBuild>& OutPending) const;
	void CollectAll(TArray<FPlanetTerrainNextNodeId>& OutNodes) const;
	/** Last evaluated projected error for a resident node; 0 if unknown. */
	double GetNodePixelError(const FPlanetTerrainNextNodeId& Id) const;

	int32 NumNodes() const { return NodeCount; }
	const FPlanetTerrainNextQuadtreeStats& GetStats() const { return Stats; }

private:
	struct FNode;

	void MakeNode(FNode& Node, const FPlanetTerrainNextNodeId& Id, double MinH, double MaxH) const;
	void RecomputeBounds(FNode& Node) const;
	FNode* FindNode(const FPlanetTerrainNextNodeId& Id) const;
	bool IsVisible(const FNode& Node, const FPlanetTerrainNextView& View, const FPlanetTerrainNextLodSettings& Settings) const;
	double PixelError(const FNode& Node, const FPlanetTerrainNextView& View) const;
	double WeightedPixelError(const FNode& Node, const FPlanetTerrainNextView& View, const FPlanetTerrainNextLodSettings& Settings) const;
	void Evaluate(FNode& Node, const FPlanetTerrainNextView& View, const FPlanetTerrainNextLodSettings& Settings);
	void MergePass(FNode& Node, const FPlanetTerrainNextView& View, const FPlanetTerrainNextLodSettings& Settings, FReadyFn IsReady, TArray<FPlanetTerrainNextNodeId>& OutReleased);
	void CollectSplitCandidates(FNode& Node, const FPlanetTerrainNextLodSettings& Settings, FReadyFn IsReady, TArray<FNode*>& OutCandidates);
	void ReleaseChildren(FNode& Node, TArray<FPlanetTerrainNextNodeId>& OutReleased);
	bool CollectDraw(const FNode& Node, FReadyFn IsReady, TArray<FPlanetTerrainNextNodeId>& OutDraw) const;
	bool CollectDraw(const FNode& Node, FReadyFn IsReady, TArray<FPlanetTerrainNextDrawItem>& OutDraw) const;

	TUniquePtr<FNode> Roots[6];
	TArray<double> GeometricError;
	double RadiusM = 0.0;
	double GlobalMinH = 0.0;
	double GlobalMaxH = 0.0;
	int32 NodeCount = 0;
	FPlanetTerrainNextQuadtreeStats Stats;
};
