#include "PlanetTerrainNextQuadtree.h"
#include "PlanetTerrainNextCoordinates.h"

struct FPlanetTerrainNextQuadtree::FNode
{
	FPlanetTerrainNextNodeId Id;
	FVector3d CenterDir = FVector3d::UnitZ();
	double AngularRadius = 0.0;
	double MinH = 0.0;
	double MaxH = 0.0;
	FVector3d BoundCenterM = FVector3d::ZeroVector;
	double BoundRadiusM = 0.0;
	double LastVisibleTime = -1.0e30;
	double LastPixelError = 0.0;
	bool bVisible = false;
	TUniquePtr<FNode> Children[4];

	bool HasChildren() const
	{
		return Children[0].IsValid();
	}
};

namespace
{
FORCEINLINE double QuadtreeAngleBetweenUnit(const FVector3d& A, const FVector3d& B)
{
	return FMath::Atan2(FVector3d::CrossProduct(A, B).Length(), FVector3d::DotProduct(A, B));
}

FORCEINLINE double GeometricErrorAt(const TArray<double>& Errors, int32 Level)
{
	return Errors.Num() == 0 ? 0.0 : Errors[FMath::Clamp(Level, 0, Errors.Num() - 1)];
}
} // namespace

FPlanetTerrainNextQuadtree::FPlanetTerrainNextQuadtree() = default;
FPlanetTerrainNextQuadtree::~FPlanetTerrainNextQuadtree() = default;

void FPlanetTerrainNextQuadtree::Initialize(double InRadiusM, double MinHeightM, double MaxHeightM, const TArray<double>& GeometricErrorByLevel)
{
	RadiusM = InRadiusM;
	GlobalMinH = MinHeightM;
	GlobalMaxH = MaxHeightM;
	GeometricError = GeometricErrorByLevel;
	NodeCount = 0;
	Stats = FPlanetTerrainNextQuadtreeStats();
	for (int32 Face = 0; Face < 6; ++Face)
	{
		Roots[Face] = MakeUnique<FNode>();
		MakeNode(*Roots[Face], FPlanetTerrainNextNodeId(static_cast<uint8>(Face), 0, 0, 0), GlobalMinH, GlobalMaxH);
		++NodeCount;
	}
}

void FPlanetTerrainNextQuadtree::MakeNode(FNode& Node, const FPlanetTerrainNextNodeId& Id, double MinH, double MaxH) const
{
	Node.Id = Id;
	const double Uc = 0.5 * (Id.U0() + Id.U1());
	const double Vc = 0.5 * (Id.V0() + Id.V1());
	Node.CenterDir = PlanetTerrainNext::FacePoint(Id.Face, Uc, Vc);

	// Patch edges are great-circle arcs, so the farthest point from the center is a corner.
	double MaxAngle = 0.0;
	const double Us[2] = {Id.U0(), Id.U1()};
	const double Vs[2] = {Id.V0(), Id.V1()};
	for (double U : Us)
	{
		for (double V : Vs)
		{
			MaxAngle = FMath::Max(MaxAngle, QuadtreeAngleBetweenUnit(Node.CenterDir, PlanetTerrainNext::FacePoint(Id.Face, U, V)));
		}
	}
	Node.AngularRadius = MaxAngle;
	Node.MinH = MinH;
	Node.MaxH = MaxH;
	RecomputeBounds(Node);
}

void FPlanetTerrainNextQuadtree::RecomputeBounds(FNode& Node) const
{
	const double Margin = 2.0 * GeometricErrorAt(GeometricError, Node.Id.Level) + 1.0;
	const double MinH = FMath::Max(GlobalMinH, Node.MinH - Margin);
	const double MaxH = FMath::Min(GlobalMaxH, Node.MaxH + Margin);
	const double Rmin = RadiusM + MinH;
	const double Rmax = RadiusM + FMath::Max(MinH, MaxH);
	const double CosA = FMath::Cos(Node.AngularRadius);

	// Distance from C to a point at (radius R, angle t) grows with t and is convex in R,
	// so the farthest point of the shell sector is at t in {0, A} and R in {Rmin, Rmax}.
	const double Rc = 0.5 * (Rmin * CosA + Rmax);
	auto DistSq = [Rc](double R, double CosT)
	{
		return R * R + Rc * Rc - 2.0 * R * Rc * CosT;
	};
	const double Far = FMath::Max(
		FMath::Max(DistSq(Rmin, 1.0), DistSq(Rmax, 1.0)),
		FMath::Max(DistSq(Rmin, CosA), DistSq(Rmax, CosA)));

	Node.BoundCenterM = Node.CenterDir * Rc;
	Node.BoundRadiusM = FMath::Sqrt(FMath::Max(0.0, Far)) * 1.0001 + 0.01;
}

FPlanetTerrainNextQuadtree::FNode* FPlanetTerrainNextQuadtree::FindNode(const FPlanetTerrainNextNodeId& Id) const
{
	if (Id.Face >= 6 || !Roots[Id.Face].IsValid())
	{
		return nullptr;
	}
	FNode* Node = Roots[Id.Face].Get();
	for (int32 Level = 1; Level <= Id.Level; ++Level)
	{
		if (!Node->HasChildren())
		{
			return nullptr;
		}
		const int32 Shift = Id.Level - Level;
		const int32 Index = static_cast<int32>(((Id.X >> Shift) & 1u) | (((Id.Y >> Shift) & 1u) << 1));
		Node = Node->Children[Index].Get();
	}
	return Node && Node->Id == Id ? Node : nullptr;
}

void FPlanetTerrainNextQuadtree::SetNodeHeightRange(const FPlanetTerrainNextNodeId& Id, double MinHeightM, double MaxHeightM)
{
	if (FNode* Node = FindNode(Id))
	{
		Node->MinH = MinHeightM;
		Node->MaxH = MaxHeightM;
		RecomputeBounds(*Node);
	}
}

bool FPlanetTerrainNextQuadtree::IsVisible(const FNode& Node, const FPlanetTerrainNextView& View, const FPlanetTerrainNextLodSettings& Settings) const
{
	const FVector3d ToCenter = Node.BoundCenterM - View.CameraM;
	const double Distance = ToCenter.Length();
	if (Distance <= Node.BoundRadiusM)
	{
		return true;
	}

	const double CameraRadius = View.CameraM.Length();
	const FVector3d CameraDir = CameraRadius > 0.0 ? View.CameraM / CameraRadius : FVector3d::UnitZ();
	const double AngleGap = FMath::Max(0.0, QuadtreeAngleBetweenUnit(CameraDir, Node.CenterDir) - Node.AngularRadius);

	const double Altitude = FMath::Max(0.0, CameraRadius - RadiusM);
	if (AngleGap * RadiusM <= FMath::Max(Settings.SafetyRadiusMinM, Altitude))
	{
		return true;
	}

	const double Occluder = RadiusM + GlobalMinH;
	const double Highest = RadiusM + GlobalMaxH;
	if (CameraRadius > Occluder)
	{
		// Horizon + patch angular radius + 15% slack: grazing limb patches stay
		// visible/split-eligible instead of vanishing on approach.
		const double Horizon = FMath::Acos(FMath::Clamp(Occluder / CameraRadius, -1.0, 1.0))
			+ FMath::Acos(FMath::Clamp(Occluder / Highest, -1.0, 1.0));
		const double HorizonLimit = (Horizon + Node.AngularRadius) * 1.15;
		if (AngleGap > HorizonLimit)
		{
			return false;
		}
	}

	const double HalfAngle = FMath::Asin(FMath::Clamp(Node.BoundRadiusM / Distance, 0.0, 1.0));
	const double ToCenterAngle = QuadtreeAngleBetweenUnit(View.Forward.GetSafeNormal(), ToCenter / Distance);
	return ToCenterAngle - HalfAngle <= View.HalfDiagonalFovRad;
}

double FPlanetTerrainNextQuadtree::PixelError(const FNode& Node, const FPlanetTerrainNextView& View) const
{
	const double Distance = FMath::Max((Node.BoundCenterM - View.CameraM).Length() - Node.BoundRadiusM, 1.0);
	return GeometricErrorAt(GeometricError, Node.Id.Level) * View.ProjectionScalePx / Distance;
}

double FPlanetTerrainNextQuadtree::WeightedPixelError(
	const FNode& Node,
	const FPlanetTerrainNextView& View,
	const FPlanetTerrainNextLodSettings& Settings) const
{
	const double Base = PixelError(Node, View);
	const double Scale = FMath::Clamp(Settings.PeripheralLodScale, 0.25, 1.0);
	if (Scale >= 1.0 - KINDA_SMALL_NUMBER)
	{
		return Base;
	}
	const FVector3d ToCenter = Node.BoundCenterM - View.CameraM;
	const double Distance = ToCenter.Length();
	if (Distance < 1.0e-3)
	{
		return Base;
	}
	const double ToCenterAngle = QuadtreeAngleBetweenUnit(View.Forward.GetSafeNormal(), ToCenter / Distance);
	const double HalfFov = FMath::Max(View.HalfDiagonalFovRad, 1.0e-3);
	const double Edge = FMath::Clamp(ToCenterAngle / HalfFov, 0.0, 1.0);
	// Smooth center→rim: full error on-axis, Scale·error at the frustum edge.
	const double Weight = FMath::Lerp(1.0, Scale, FMath::SmoothStep(0.0, 1.0, Edge));
	return Base * Weight;
}

void FPlanetTerrainNextQuadtree::Evaluate(FNode& Node, const FPlanetTerrainNextView& View, const FPlanetTerrainNextLodSettings& Settings)
{
	Node.bVisible = IsVisible(Node, View, Settings);
	Node.LastPixelError = WeightedPixelError(Node, View, Settings);
	if (Node.bVisible)
	{
		Node.LastVisibleTime = View.TimeSeconds;
		++Stats.VisibleNodes;
	}
	if (Node.HasChildren())
	{
		for (TUniquePtr<FNode>& Child : Node.Children)
		{
			Evaluate(*Child, View, Settings);
		}
	}
}

void FPlanetTerrainNextQuadtree::ReleaseChildren(FNode& Node, TArray<FPlanetTerrainNextNodeId>& OutReleased)
{
	if (!Node.HasChildren())
	{
		return;
	}
	for (TUniquePtr<FNode>& Child : Node.Children)
	{
		ReleaseChildren(*Child, OutReleased);
		OutReleased.Add(Child->Id);
		Child.Reset();
		--NodeCount;
	}
}

void FPlanetTerrainNextQuadtree::MergePass(
	FNode& Node,
	const FPlanetTerrainNextView& View,
	const FPlanetTerrainNextLodSettings& Settings,
	FReadyFn IsReady,
	TArray<FPlanetTerrainNextNodeId>& OutReleased)
{
	if (!Node.HasChildren())
	{
		return;
	}

	const double MergeBelow = Settings.TargetPixelError / FMath::Max(1.0, Settings.MergeHysteresis);
	const bool bWithinGrace = View.TimeSeconds - Node.LastVisibleTime < Settings.InvisibleGraceSeconds;
	const bool bKeep = Node.bVisible ? Node.LastPixelError > MergeBelow : bWithinGrace;
	const bool bTooDeep = Node.Id.Level >= Settings.MaxLevel;

	if ((!bKeep || bTooDeep) && IsReady(Node.Id))
	{
		ReleaseChildren(Node, OutReleased);
		++Stats.Merges;
		return;
	}

	for (TUniquePtr<FNode>& Child : Node.Children)
	{
		MergePass(*Child, View, Settings, IsReady, OutReleased);
	}
}

void FPlanetTerrainNextQuadtree::CollectSplitCandidates(FNode& Node, const FPlanetTerrainNextLodSettings& Settings, FReadyFn IsReady, TArray<FNode*>& OutCandidates)
{
	if (Node.HasChildren())
	{
		for (TUniquePtr<FNode>& Child : Node.Children)
		{
			CollectSplitCandidates(*Child, Settings, IsReady, OutCandidates);
		}
		return;
	}
	if (Node.bVisible
		&& Node.Id.Level < Settings.MaxLevel
		&& Node.LastPixelError > Settings.TargetPixelError
		&& IsReady(Node.Id))
	{
		OutCandidates.Add(&Node);
	}
}

void FPlanetTerrainNextQuadtree::Update(
	const FPlanetTerrainNextView& View,
	const FPlanetTerrainNextLodSettings& Settings,
	FReadyFn IsReady,
	TArray<FPlanetTerrainNextNodeId>& OutReleased)
{
	Stats.Splits = 0;
	Stats.Merges = 0;
	Stats.BudgetBlocked = 0;
	Stats.VisibleNodes = 0;
	if (!IsInitialized())
	{
		return;
	}

	for (TUniquePtr<FNode>& Root : Roots)
	{
		Evaluate(*Root, View, Settings);
	}

	if (!Settings.bFreeze)
	{
		for (TUniquePtr<FNode>& Root : Roots)
		{
			MergePass(*Root, View, Settings, IsReady, OutReleased);
		}

		TArray<FNode*> Candidates;
		for (TUniquePtr<FNode>& Root : Roots)
		{
			CollectSplitCandidates(*Root, Settings, IsReady, Candidates);
		}
		Candidates.Sort([](const FNode& A, const FNode& B)
		{
			return A.LastPixelError > B.LastPixelError;
		});

		const int32 MaxNodes = FMath::Max(Settings.MaxNodes, 6);
		for (FNode* Node : Candidates)
		{
			if (NodeCount + 4 > MaxNodes)
			{
				++Stats.BudgetBlocked;
				continue;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Node->Children[Index] = MakeUnique<FNode>();
				MakeNode(*Node->Children[Index], Node->Id.Child(Index), Node->MinH, Node->MaxH);
				Node->Children[Index]->LastVisibleTime = View.TimeSeconds;
			}
			NodeCount += 4;
			++Stats.Splits;
		}
	}

	Stats.Nodes = NodeCount;
}

bool FPlanetTerrainNextQuadtree::CollectDraw(const FNode& Node, FReadyFn IsReady, TArray<FPlanetTerrainNextNodeId>& OutDraw) const
{
	if (Node.HasChildren())
	{
		const int32 Mark = OutDraw.Num();
		bool bAllCovered = true;
		for (const TUniquePtr<FNode>& Child : Node.Children)
		{
			if (!CollectDraw(*Child, IsReady, OutDraw))
			{
				bAllCovered = false;
				break;
			}
		}
		if (bAllCovered)
		{
			return true;
		}
		OutDraw.SetNum(Mark, EAllowShrinking::No);
	}
	if (IsReady(Node.Id))
	{
		OutDraw.Add(Node.Id);
		return true;
	}
	return false;
}

void FPlanetTerrainNextQuadtree::CollectDrawSet(FReadyFn IsReady, TArray<FPlanetTerrainNextNodeId>& OutDraw) const
{
	OutDraw.Reset();
	for (const TUniquePtr<FNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			CollectDraw(*Root, IsReady, OutDraw);
		}
	}
}

bool FPlanetTerrainNextQuadtree::CollectDraw(const FNode& Node, FReadyFn IsReady, TArray<FPlanetTerrainNextDrawItem>& OutDraw) const
{
	if (Node.HasChildren())
	{
		const int32 Mark = OutDraw.Num();
		bool bAllCovered = true;
		for (const TUniquePtr<FNode>& Child : Node.Children)
		{
			if (!CollectDraw(*Child, IsReady, OutDraw))
			{
				bAllCovered = false;
				break;
			}
		}
		if (bAllCovered)
		{
			return true;
		}
		OutDraw.SetNum(Mark, EAllowShrinking::No);
	}
	if (IsReady(Node.Id))
	{
		FPlanetTerrainNextDrawItem Item;
		Item.Id = Node.Id;
		Item.PixelError = Node.LastPixelError;
		OutDraw.Add(Item);
		return true;
	}
	return false;
}

void FPlanetTerrainNextQuadtree::CollectDrawSet(FReadyFn IsReady, TArray<FPlanetTerrainNextDrawItem>& OutDraw) const
{
	OutDraw.Reset();
	for (const TUniquePtr<FNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			CollectDraw(*Root, IsReady, OutDraw);
		}
	}
}

double FPlanetTerrainNextQuadtree::GetNodePixelError(const FPlanetTerrainNextNodeId& Id) const
{
	if (const FNode* Node = FindNode(Id))
	{
		return Node->LastPixelError;
	}
	return 0.0;
}

void FPlanetTerrainNextQuadtree::CollectPending(FReadyFn IsReady, TArray<FPlanetTerrainNextPendingBuild>& OutPending) const
{
	OutPending.Reset();
	TArray<const FNode*, TInlineAllocator<64>> Stack;
	for (const TUniquePtr<FNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			Stack.Add(Root.Get());
		}
	}
	while (Stack.Num() > 0)
	{
		const FNode* Node = Stack.Pop(EAllowShrinking::No);
		if (!IsReady(Node->Id))
		{
			FPlanetTerrainNextPendingBuild Pending;
			Pending.Id = Node->Id;
			Pending.Priority = (Node->bVisible ? 1.0e12 : 0.0) + FMath::Min(Node->LastPixelError, 1.0e11);
			OutPending.Add(Pending);
		}
		if (Node->HasChildren())
		{
			for (const TUniquePtr<FNode>& Child : Node->Children)
			{
				Stack.Add(Child.Get());
			}
		}
	}
	OutPending.Sort([](const FPlanetTerrainNextPendingBuild& A, const FPlanetTerrainNextPendingBuild& B)
	{
		return A.Priority > B.Priority;
	});
}

void FPlanetTerrainNextQuadtree::CollectAll(TArray<FPlanetTerrainNextNodeId>& OutNodes) const
{
	OutNodes.Reset();
	TArray<const FNode*, TInlineAllocator<64>> Stack;
	for (const TUniquePtr<FNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			Stack.Add(Root.Get());
		}
	}
	while (Stack.Num() > 0)
	{
		const FNode* Node = Stack.Pop(EAllowShrinking::No);
		OutNodes.Add(Node->Id);
		if (Node->HasChildren())
		{
			for (const TUniquePtr<FNode>& Child : Node->Children)
			{
				Stack.Add(Child.Get());
			}
		}
	}
}
