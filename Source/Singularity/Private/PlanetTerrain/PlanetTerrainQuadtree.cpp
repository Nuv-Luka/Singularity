#include "PlanetTerrain/PlanetTerrainQuadtree.h"

FPlanetTerrainTileKey FPlanetTerrainQuadtree::MakeKey(
	uint64 InPlanetId, uint32 InEpoch, uint8 Face, uint8 Level, uint32 X, uint32 Y)
{
	FPlanetTerrainTileKey Key;
	Key.PlanetID = InPlanetId;
	Key.GenerationEpoch = InEpoch;
	Key.Face = Face;
	Key.Level = Level;
	Key.X = X;
	Key.Y = Y;
	return Key;
}

void FPlanetTerrainQuadtree::Initialize(uint64 InPlanetId, uint32 InEpoch, double RadiusMeters, const FPlanetTerrainLodParams& Params)
{
	PlanetId = InPlanetId;
	Epoch = InEpoch;
	RadiusM = RadiusMeters;
	Lod = Params;
	if (Lod.ActivateDistanceM <= 0.0)
	{
		Lod.ActivateDistanceM = RadiusM * 8.0;
	}
	Roots.Reset();
	Roots.Reserve(6);
	for (uint8 Face = 0; Face < 6; ++Face)
	{
		TUniquePtr<FPlanetTerrainNode> Root = MakeUnique<FPlanetTerrainNode>();
		Root->Face = Face;
		Root->Level = 0;
		Root->X = 0;
		Root->Y = 0;
		Root->U0 = 0.0;
		Root->V0 = 0.0;
		Root->U1 = 1.0;
		Root->V1 = 1.0;
		Root->Key = MakeKey(PlanetId, Epoch, Face, 0, 0, 0);
		Root->CenterLocal = PlanetTerrainFacePoint(Face, 0.5, 0.5) * RadiusM;
		Root->CenterSurfaceLocal = Root->CenterLocal;
		Root->WorldSizeM = PI * RadiusM;
		Root->bMandatoryCoverage = true;
		Root->bGenPending = true;
		Root->bVisibleLeaf = true;
		Roots.Add(MoveTemp(Root));
	}
	SplitCount = MergeCount = ActiveLeafCount = 0;
	FrustumCulledCount = HorizonCulledCount = ScreenErrorSplitCount = BudgetExhaustedCount = 0;
}

void FPlanetTerrainQuadtree::Reset(uint32 NewEpoch)
{
	Epoch = NewEpoch;
	FPlanetTerrainLodParams Keep = Lod;
	Initialize(PlanetId, Epoch, RadiusM, Keep);
}

bool FPlanetTerrainQuadtree::AllChildrenReady(const FPlanetTerrainNode& Node)
{
	// Require Resident (mesh uploaded). ReadyForUpload alone must not hide the parent.
	for (int32 I = 0; I < 4; ++I)
	{
		if (!Node.Children[I].IsValid())
		{
			return false;
		}
		const FPlanetTerrainNode* C = Node.Children[I].Get();
		if (!C->Tile.IsValid() || C->Tile->State != EPlanetTerrainTileState::Resident)
		{
			return false;
		}
	}
	return true;
}

void FPlanetTerrainQuadtree::Split(FPlanetTerrainNode& Node)
{
	if (Node.Children[0].IsValid())
	{
		return;
	}
	const double Um = 0.5 * (Node.U0 + Node.U1);
	const double Vm = 0.5 * (Node.V0 + Node.V1);
	const uint8 ChildLevel = static_cast<uint8>(Node.Level + 1);
	const uint32 BaseX = Node.X * 2;
	const uint32 BaseY = Node.Y * 2;
	const double URect[4][2] = {{Node.U0, Um}, {Um, Node.U1}, {Node.U0, Um}, {Um, Node.U1}};
	const double VRect[4][2] = {{Node.V0, Vm}, {Node.V0, Vm}, {Vm, Node.V1}, {Vm, Node.V1}};
	const uint32 OffX[4] = {0, 1, 0, 1};
	const uint32 OffY[4] = {0, 0, 1, 1};

	for (int32 I = 0; I < 4; ++I)
	{
		TUniquePtr<FPlanetTerrainNode> Child = MakeUnique<FPlanetTerrainNode>();
		Child->Face = Node.Face;
		Child->Level = ChildLevel;
		Child->X = BaseX + OffX[I];
		Child->Y = BaseY + OffY[I];
		Child->U0 = URect[I][0];
		Child->U1 = URect[I][1];
		Child->V0 = VRect[I][0];
		Child->V1 = VRect[I][1];
		Child->Key = MakeKey(PlanetId, Epoch, Child->Face, Child->Level, Child->X, Child->Y);
		const double Uc = 0.5 * (Child->U0 + Child->U1);
		const double Vc = 0.5 * (Child->V0 + Child->V1);
		Child->CenterLocal = PlanetTerrainFacePoint(Child->Face, Uc, Vc) * RadiusM;
		Child->CenterSurfaceLocal = Child->CenterLocal;
		Child->WorldSizeM = Node.WorldSizeM * 0.5;
		Node.Children[I] = MoveTemp(Child);
	}
	++SplitCount;
}

void FPlanetTerrainQuadtree::DisposeChildren(FPlanetTerrainNode& Node)
{
	for (int32 I = 0; I < 4; ++I)
	{
		if (Node.Children[I].IsValid())
		{
			DisposeChildren(*Node.Children[I]);
			Node.Children[I]->bDisposed = true;
			Node.Children[I].Reset();
			++MergeCount;
		}
	}
	Node.bChildrenPromoted = false;
}

double FPlanetTerrainQuadtree::EstimateGeometricErrorM(const FPlanetTerrainNode& Node, double AmplitudeM) const
{
	const double AmpTerm = FMath::Max(1.0, AmplitudeM) * Lod.GeometricErrorAmpFrac;
	const double SizeTerm = Node.WorldSizeM * Lod.GeometricErrorSizeFrac;
	return FMath::Min(AmpTerm, SizeTerm);
}

double FPlanetTerrainQuadtree::ProjectErrorPx(
	double GeometricErrorM, double DistM, const FPlanetTerrainCameraQuery& Camera) const
{
	const double Dist = FMath::Max(1.0, DistM);
	const double HalfFov = FMath::Max(0.05, Camera.VerticalFovRad * 0.5);
	const double PxPerMeter = Camera.ScreenHeightPx / (2.0 * Dist * FMath::Tan(HalfFov));
	return GeometricErrorM * PxPerMeter;
}

double FPlanetTerrainQuadtree::TileHalfAngleRad(const FPlanetTerrainNode& Node) const
{
	return FMath::Max(0.01, Node.WorldSizeM / (2.0 * FMath::Max(1.0, RadiusM)));
}

void FPlanetTerrainQuadtree::EvalCornerDirs(
	const FPlanetTerrainNode& Node, TArray<FVector3d, TInlineAllocator<5>>& OutDirs) const
{
	OutDirs.Reset();
	const double Us[2] = {Node.U0, Node.U1};
	const double Vs[2] = {Node.V0, Node.V1};
	for (int32 Iu = 0; Iu < 2; ++Iu)
	{
		for (int32 Iv = 0; Iv < 2; ++Iv)
		{
			OutDirs.Add(PlanetTerrainFacePoint(Node.Face, Us[Iu], Vs[Iv]));
		}
	}
	OutDirs.Add(Node.CenterLocal.GetSafeNormal());
}

bool FPlanetTerrainQuadtree::PassesHorizonBounds(
	const FPlanetTerrainNode& Node, const FPlanetTerrainCameraQuery& Camera, double AmplitudeM) const
{
	const double CamDist = Camera.PlanetLocalPosition.Size();
	if (CamDist <= RadiusM + AmplitudeM + 1.0)
	{
		return true;
	}
	const FVector3d CamDir = Camera.PlanetLocalPosition / CamDist;
	const double REff = FMath::Max(1.0, RadiusM - Lod.HorizonReliefMarginM - AmplitudeM);
	const double CosHorizon = FMath::Clamp(REff / CamDist, -1.0, 1.0);
	const double HalfAng = TileHalfAngleRad(Node) + Lod.CullAngularMarginRad;
	const double Margin = FMath::Sin(HalfAng);

	TArray<FVector3d, TInlineAllocator<5>> Dirs;
	EvalCornerDirs(Node, Dirs);
	for (const FVector3d& D : Dirs)
	{
		if (FVector3d::DotProduct(CamDir, D.GetSafeNormal()) + Margin >= CosHorizon - 0.04)
		{
			return true;
		}
	}
	return false;
}

bool FPlanetTerrainQuadtree::PassesFrustumBounds(
	const FPlanetTerrainNode& Node, const FPlanetTerrainCameraQuery& Camera, bool bPrefetch) const
{
	if (!Camera.bHasFrustum)
	{
		return true;
	}
	const FVector3d Fwd = Camera.Forward.GetSafeNormal();
	const double MinDot = (bPrefetch ? Lod.FrustumPrefetchMinDot : Lod.FrustumMinDot)
		- FMath::Sin(TileHalfAngleRad(Node) + Lod.CullAngularMarginRad);

	TArray<FVector3d, TInlineAllocator<5>> Dirs;
	EvalCornerDirs(Node, Dirs);
	const FVector3d Cam = Camera.PlanetLocalPosition;
	const double RSurf = RadiusM + Camera.TerrainAmplitudeM;
	for (const FVector3d& D : Dirs)
	{
		const FVector3d Surf = D.GetSafeNormal() * RSurf;
		const FVector3d To = (Surf - Cam).GetSafeNormal();
		if (FVector3d::DotProduct(Fwd, To) >= MinDot)
		{
			return true;
		}
	}
	// Camera inside / very near the tile's cone — always keep.
	const FVector3d Nadir = Cam.GetSafeNormal();
	if (FVector3d::DotProduct(Nadir, Node.CenterLocal.GetSafeNormal()) > FMath::Cos(TileHalfAngleRad(Node) + 0.2))
	{
		return true;
	}
	return false;
}

double FPlanetTerrainQuadtree::ScreenRadiusFoveationWeight(
	const FVector3d& TileSurface, const FPlanetTerrainCameraQuery& Camera) const
{
	const FVector3d Fwd = Camera.Forward.GetSafeNormal();
	FVector3d Right = Camera.Right.GetSafeNormal();
	FVector3d Up = Camera.Up.GetSafeNormal();
	if (Right.SizeSquared() < 1e-8 || Up.SizeSquared() < 1e-8)
	{
		Right = FVector3d::CrossProduct(Fwd, FVector3d(0, 0, 1));
		if (Right.SizeSquared() < 1e-8)
		{
			Right = FVector3d::CrossProduct(Fwd, FVector3d(0, 1, 0));
		}
		Right.Normalize();
		Up = FVector3d::CrossProduct(Right, Fwd).GetSafeNormal();
	}

	const FVector3d To = (TileSurface - Camera.PlanetLocalPosition).GetSafeNormal();
	const double Fd = FVector3d::DotProduct(Fwd, To);
	if (Fd <= 1e-4)
	{
		return 0.0;
	}
	const double HalfV = FMath::Max(0.05, Camera.VerticalFovRad * 0.5);
	const double TanV = FMath::Tan(HalfV);
	const double TanH = TanV * FMath::Max(0.5, Camera.AspectRatio);
	const double Sx = FVector3d::DotProduct(Right, To) / (Fd * TanH);
	const double Sy = FVector3d::DotProduct(Up, To) / (Fd * TanV);
	const double ScreenR = FMath::Sqrt(Sx * Sx + Sy * Sy);
	const double Inner = Lod.FoveaInnerRadius;
	const double Outer = FMath::Max(Inner + 1e-3, Lod.FoveaOuterRadius);
	// smoothstep(Inner, Outer, ScreenR) then invert → 1 at center
	const double T = FMath::Clamp((ScreenR - Inner) / (Outer - Inner), 0.0, 1.0);
	const double Smooth = T * T * (3.0 - 2.0 * T);
	return 1.0 - Smooth;
}

bool FPlanetTerrainQuadtree::HasFullRootCoverage() const
{
	for (const TUniquePtr<FPlanetTerrainNode>& Root : Roots)
	{
		if (!Root.IsValid() || !Root->Tile.IsValid()
			|| Root->Tile->State != EPlanetTerrainTileState::Resident)
		{
			return false;
		}
	}
	return true;
}

void FPlanetTerrainQuadtree::EnsureMandatoryRootCoverage()
{
	for (TUniquePtr<FPlanetTerrainNode>& Root : Roots)
	{
		if (!Root.IsValid())
		{
			continue;
		}
		Root->bMandatoryCoverage = true;
		if (!Root->Tile.IsValid())
		{
			Root->bGenPending = true;
			Root->bVisibleLeaf = true;
		}
	}
}

void FPlanetTerrainQuadtree::ProcessNode(
	FPlanetTerrainNode& Node,
	const FPlanetTerrainCameraQuery& Camera,
	const FPlanetTerrainHeight& Height,
	int32 FrameIndex,
	int32& PatchBudget)
{
	if (Node.bDisposed)
	{
		return;
	}

	const FVector3d Dir = Node.CenterLocal.GetSafeNormal();
	const double Anchor = Height.LodAnchorElevation(Dir);
	Node.CenterSurfaceLocal = Dir * (RadiusM + Anchor);
	const double Amp = Camera.TerrainAmplitudeM > 0.0 ? Camera.TerrainAmplitudeM : Height.GetAmplitudeMeters();

	const double Dist = FVector3d::Distance(Camera.PlanetLocalPosition, Node.CenterSurfaceLocal);
	const FVector3d CamDir = Camera.PlanetLocalPosition.GetSafeNormal();
	const double DotNadir = FVector3d::DotProduct(CamDir, Dir);
	const double AngularDist = FMath::Acos(FMath::Clamp(DotNadir, -1.0, 1.0));
	const double GeodesicM = AngularDist * RadiusM;
	const double Speed = Camera.Velocity.Size();
	const double SafetyCorridorM = FMath::Clamp(3000.0 + Speed * 2.0, 2000.0, 40000.0);

	Node.RejectReason = EPlanetTerrainRejectReason::None;
	Node.bHorizonVisible = PassesHorizonBounds(Node, Camera, Amp);
	Node.bBoundsInFrustum = PassesFrustumBounds(Node, Camera, false);
	Node.bFrustumVisible = Node.bBoundsInFrustum;
	const bool bPrefetchVisible = PassesFrustumBounds(Node, Camera, true);

	// Peak path: screen-radius foveation only (look-force splits removed — they wrecked coverage).
	const double FoveaW = ScreenRadiusFoveationWeight(Node.CenterSurfaceLocal, Camera);
	Node.FoveationWeight = static_cast<float>(FoveaW);

	const double GeomErr = EstimateGeometricErrorM(Node, Amp);
	const double RawPx = ProjectErrorPx(GeomErr, Dist, Camera);
	Node.ScreenErrorPx = static_cast<float>(RawPx);

	const double PixelBudget = FMath::Lerp(Lod.PeripheralPixelError, Lod.TargetPixelError, FoveaW);

	const double SplitDist = Lod.LodDistanceFactor * Node.WorldSizeM;
	const double AltitudeAboveSphere = FMath::Max(0.0, Camera.PlanetLocalPosition.Size() - RadiusM);
	const double EffectiveDist = FMath::Max(Dist, AltitudeAboveSphere + Node.WorldSizeM * 0.2);

	// Physically bounded underfoot corridor (emergency-repair peak).
	const bool bForceNear = GeodesicM < SafetyCorridorM
		&& Node.Level < Lod.MinNeighborhoodLevel
		&& Dist < FMath::Max(Lod.ActivateDistanceM, RadiusM * 0.35);

	// Budget exhausted: stop refinement but KEEP existing coverage as a draw leaf.
	if (PatchBudget <= 0)
	{
		++BudgetExhaustedCount;
		Node.RejectReason = EPlanetTerrainRejectReason::Budget;
		if (Node.Children[0].IsValid() && AllChildrenReady(Node))
		{
			// Already promoted — still walk children for visibility bookkeeping without splitting further.
			for (int32 I = 0; I < 4; ++I)
			{
				ProcessNode(*Node.Children[I], Camera, Height, FrameIndex, PatchBudget);
			}
			Node.bVisibleLeaf = false;
			return;
		}
		Node.bVisibleLeaf = true;
		Node.DrawnFrame = FrameIndex;
		if (!Node.Tile.IsValid())
		{
			Node.bGenPending = true;
			Node.bMandatoryCoverage = (Node.Level == 0) || Node.bMandatoryCoverage;
		}
		return;
	}

	// Far-side horizon: do not refine, but never strip a resident parent without replacement.
	if (!Node.bHorizonVisible && !bForceNear)
	{
		++HorizonCulledCount;
		Node.RejectReason = EPlanetTerrainRejectReason::Horizon;
		// Soft merge only when children exist and we are clearly past the limb.
		if (!bFrozen && Node.Children[0].IsValid() && DotNadir < -0.1
			&& Dist > Node.WorldSizeM * Lod.LodDistanceFactor * Lod.MergeHysteresis)
		{
			DisposeChildren(Node);
		}
		Node.bVisibleLeaf = true;
		Node.DrawnFrame = FrameIndex;
		if (!Node.Tile.IsValid() && Node.Level == 0)
		{
			Node.bGenPending = true;
			Node.bMandatoryCoverage = true;
		}
		--PatchBudget;
		++ActiveLeafCount;
		return;
	}

	// Guided-descent / streaming v3: when screen-space LOD is active, distance
	// must NOT OR-bypass visibility + projected-error limits (was flooding workers).
	const bool bScreenWantSplit = Lod.bUseScreenSpaceLod
		&& Node.bHorizonVisible
		&& (Node.bBoundsInFrustum || bPrefetchVisible || bForceNear)
		&& Node.Level < Lod.MaxLevel
		&& RawPx > PixelBudget
		&& PatchBudget > 4;

	const bool bDistanceWant = !Lod.bUseScreenSpaceLod
		&& Node.bHorizonVisible
		&& (Node.bBoundsInFrustum || bPrefetchVisible || bForceNear)
		&& Dist < Lod.ActivateDistanceM
		&& Node.Level < Lod.MaxLevel
		&& EffectiveDist < SplitDist
		&& PatchBudget > 4;

	const bool bWantSplit = (bScreenWantSplit || bDistanceWant || bForceNear) && !bFrozen;

	if (bWantSplit)
	{
		if (bScreenWantSplit)
		{
			++ScreenErrorSplitCount;
		}
		Split(Node);
		if (AllChildrenReady(Node))
		{
			if (!Node.bChildrenPromoted)
			{
				Node.bChildrenPromoted = true;
				for (int32 I = 0; I < 4; ++I)
				{
					if (Node.Children[I].IsValid())
					{
						// Start at parent surface (coarse = parent displayed).
						Node.Children[I]->TemporalMorph = 1.0f;
						Node.Children[I]->DrawMorph = 1.0f;
					}
				}
			}
			for (int32 I = 0; I < 4; ++I)
			{
				ProcessNode(*Node.Children[I], Camera, Height, FrameIndex, PatchBudget);
			}
			Node.bVisibleLeaf = false;
			return;
		}
		// Children not ready — keep PARENT as the sole coverage representation.
		for (int32 I = 0; I < 4; ++I)
		{
			if (Node.Children[I].IsValid() && !Node.Children[I]->Tile.IsValid() && !Node.Children[I]->bGenPending)
			{
				Node.Children[I]->bGenPending = true;
			}
		}
		Node.bVisibleLeaf = true;
		Node.DrawnFrame = FrameIndex;
		--PatchBudget;
		++ActiveLeafCount;
		if (!Node.Tile.IsValid())
		{
			Node.bGenPending = true;
			if (Node.Level == 0)
			{
				Node.bMandatoryCoverage = true;
			}
		}
		if (Node.Level > 0 && SplitDist > 0.0)
		{
			const double T = FMath::Clamp((SplitDist - Dist) / (SplitDist * 0.35), 0.0, 1.0);
			Node.DrawMorph = static_cast<float>(1.0 - T);
		}
		return;
	}

	if (!bFrozen && Node.Children[0].IsValid())
	{
		const bool bMerge = !bForceNear && (Lod.bUseScreenSpaceLod
			? (RawPx < PixelBudget / Lod.MergeHysteresis)
			: (EffectiveDist > SplitDist * Lod.MergeHysteresis));
		if (bMerge)
		{
			DisposeChildren(Node);
		}
	}

	// Off-frustum: skip further refinement spend, but keep resident mesh if we have one.
	if (!Node.bBoundsInFrustum && !bPrefetchVisible && !bForceNear && Node.Level > 0)
	{
		++FrustumCulledCount;
		Node.RejectReason = EPlanetTerrainRejectReason::Frustum;
		if (Node.Tile.IsValid() && Node.Tile->State == EPlanetTerrainTileState::Resident)
		{
			Node.bVisibleLeaf = true;
			Node.DrawnFrame = FrameIndex;
			--PatchBudget;
			++ActiveLeafCount;
		}
		else
		{
			Node.bVisibleLeaf = false;
		}
		return;
	}

	Node.bVisibleLeaf = true;
	Node.DrawnFrame = FrameIndex;
	--PatchBudget;
	++ActiveLeafCount;

	if (!Node.Tile.IsValid() && !Node.bGenPending)
	{
		Node.bGenPending = true;
	}
	if (Node.Level == 0 && !Node.Tile.IsValid())
	{
		Node.bMandatoryCoverage = true;
	}

	if (Node.Level > 0 && SplitDist > 0.0)
	{
		const double T = FMath::Clamp((SplitDist - Dist) / (SplitDist * 0.35), 0.0, 1.0);
		Node.DrawMorph = static_cast<float>(1.0 - T);
	}
	else
	{
		Node.DrawMorph = 0.0f;
	}
}

void FPlanetTerrainQuadtree::UpdateLod(
	const FPlanetTerrainCameraQuery& Camera,
	const FPlanetTerrainHeight& Height,
	int32 FrameIndex)
{
	ActiveLeafCount = 0;
	FrustumCulledCount = HorizonCulledCount = ScreenErrorSplitCount = BudgetExhaustedCount = 0;
	int32 Budget = Lod.MaxActivePatches;
	for (TUniquePtr<FPlanetTerrainNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			ProcessNode(*Root, Camera, Height, FrameIndex, Budget);
		}
	}
	EnsureMandatoryRootCoverage();
	// ΔLOD≤1 stitch deferred — EnsurePath flood here destroyed freeflight coverage.
	(void)Budget;
}

void FPlanetTerrainQuadtree::EnforceNeighborLodDelta(int32& PatchBudget)
{
	// Intentionally disabled in rebuild baseline (skirts + geomorph only until stable).
	(void)PatchBudget;
}

void FPlanetTerrainQuadtree::CollectGenerationRequests(TArray<FPlanetTerrainNode*>& OutRequests) const
{
	TArray<FPlanetTerrainNode*> Stack;
	for (const TUniquePtr<FPlanetTerrainNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			Stack.Add(Root.Get());
		}
	}
	while (Stack.Num() > 0)
	{
		FPlanetTerrainNode* N = Stack.Pop(EAllowShrinking::No);
		if (N->bGenPending && !N->Tile.IsValid() && !N->bDisposed)
		{
			OutRequests.Add(N);
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

void FPlanetTerrainQuadtree::CollectDrawLeaves(TArray<FPlanetTerrainNode*>& OutLeaves) const
{
	TArray<FPlanetTerrainNode*> Stack;
	for (const TUniquePtr<FPlanetTerrainNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			Stack.Add(Root.Get());
		}
	}
	while (Stack.Num() > 0)
	{
		FPlanetTerrainNode* N = Stack.Pop(EAllowShrinking::No);
		const bool bHasKids = N->Children[0].IsValid();
		if (bHasKids && AllChildrenReady(*N))
		{
			// Atomic handoff: only descend when all four children are Resident.
			for (int32 I = 0; I < 4; ++I)
			{
				Stack.Add(N->Children[I].Get());
			}
		}
		else
		{
			// Parent (or leaf) provides coverage until children are fully ready.
			OutLeaves.Add(N);
		}
	}
}

void FPlanetTerrainQuadtree::CollectCoveragePinKeys(TSet<FPlanetTerrainTileKey>& OutPins) const
{
	TArray<FPlanetTerrainNode*> Leaves;
	CollectDrawLeaves(Leaves);
	for (FPlanetTerrainNode* L : Leaves)
	{
		if (L)
		{
			FPlanetTerrainTileKey K = L->Key;
			K.GenerationEpoch = 0;
			OutPins.Add(K);
		}
	}
	for (const TUniquePtr<FPlanetTerrainNode>& Root : Roots)
	{
		if (Root.IsValid())
		{
			FPlanetTerrainTileKey K = Root->Key;
			K.GenerationEpoch = 0;
			OutPins.Add(K);
		}
	}
}

bool FPlanetTerrainQuadtree::DirectionToFaceUV(const FVector3d& UnitDir, uint8& OutFace, double& OutU, double& OutV)
{
	const FVector3d D = UnitDir.GetSafeNormal();
	if (D.IsNearlyZero())
	{
		return false;
	}
	const double Ax = FMath::Abs(D.X), Ay = FMath::Abs(D.Y), Az = FMath::Abs(D.Z);
	if (Ax >= Ay && Ax >= Az)
	{
		OutFace = D.X > 0.0 ? 0 : 1;
		OutU = D.X > 0.0 ? 0.5 * (1.0 - D.Z / Ax) : 0.5 * (1.0 + D.Z / Ax);
		OutV = 0.5 * (1.0 + D.Y / Ax);
	}
	else if (Ay >= Ax && Ay >= Az)
	{
		OutFace = D.Y > 0.0 ? 2 : 3;
		OutU = 0.5 * (1.0 + D.X / Ay);
		OutV = D.Y > 0.0 ? 0.5 * (1.0 - D.Z / Ay) : 0.5 * (1.0 + D.Z / Ay);
	}
	else
	{
		OutFace = D.Z > 0.0 ? 4 : 5;
		OutU = D.Z > 0.0 ? 0.5 * (1.0 + D.X / Az) : 0.5 * (1.0 - D.X / Az);
		OutV = 0.5 * (1.0 + D.Y / Az);
	}
	OutU = FMath::Clamp(OutU, 0.0, 1.0);
	OutV = FMath::Clamp(OutV, 0.0, 1.0);
	return true;
}

FPlanetTerrainNode* FPlanetTerrainQuadtree::FindNodeAlongPath(const FVector3d& UnitDir, int32 MinLevel) const
{
	uint8 Face = 0;
	double U = 0.0, V = 0.0;
	if (!DirectionToFaceUV(UnitDir, Face, U, V) || !Roots.IsValidIndex(Face))
	{
		return nullptr;
	}
	const int32 Target = FMath::Clamp(MinLevel, 0, Lod.MaxLevel);
	FPlanetTerrainNode* N = Roots[Face].Get();
	while (N && N->Level < Target && N->Children[0].IsValid())
	{
		const double Um = 0.5 * (N->U0 + N->U1);
		const double Vm = 0.5 * (N->V0 + N->V1);
		const int32 Ix = U < Um ? 0 : 1;
		const int32 Iy = V < Vm ? 0 : 1;
		N = N->Children[Iy * 2 + Ix].Get();
	}
	return N;
}

FPlanetTerrainNode* FPlanetTerrainQuadtree::FindParent(const FPlanetTerrainTileKey& Key) const
{
	if (Key.Level == 0 || !Roots.IsValidIndex(Key.Face))
	{
		return nullptr;
	}
	FPlanetTerrainNode* N = Roots[Key.Face].Get();
	for (uint8 L = 0; L + 1 < Key.Level; ++L)
	{
		if (!N || !N->Children[0].IsValid())
		{
			return nullptr;
		}
		const int32 Shift = static_cast<int32>(Key.Level) - 1 - static_cast<int32>(L);
		const int32 Ix = static_cast<int32>((Key.X >> Shift) & 1u);
		const int32 Iy = static_cast<int32>((Key.Y >> Shift) & 1u);
		N = N->Children[Iy * 2 + Ix].Get();
	}
	return N;
}

FPlanetTerrainNode* FPlanetTerrainQuadtree::FindLeafContaining(const FVector3d& UnitDir) const
{
	uint8 Face = 0;
	double U = 0.0, V = 0.0;
	if (!DirectionToFaceUV(UnitDir, Face, U, V) || !Roots.IsValidIndex(Face))
	{
		return nullptr;
	}
	FPlanetTerrainNode* N = Roots[Face].Get();
	while (N && N->Children[0].IsValid() && AllChildrenReady(*N))
	{
		const double Um = 0.5 * (N->U0 + N->U1);
		const double Vm = 0.5 * (N->V0 + N->V1);
		const int32 Ix = U < Um ? 0 : 1;
		const int32 Iy = V < Vm ? 0 : 1;
		N = N->Children[Iy * 2 + Ix].Get();
	}
	return N;
}

FPlanetTerrainNode* FPlanetTerrainQuadtree::EnsurePathToLevel(const FVector3d& UnitDir, int32 MinLevel)
{
	uint8 Face = 0;
	double U = 0.0, V = 0.0;
	if (!DirectionToFaceUV(UnitDir, Face, U, V) || !Roots.IsValidIndex(Face))
	{
		return nullptr;
	}
	const int32 Target = FMath::Clamp(MinLevel, 0, Lod.MaxLevel);
	FPlanetTerrainNode* N = Roots[Face].Get();
	while (N && N->Level < Target)
	{
		Split(*N);
		const double Um = 0.5 * (N->U0 + N->U1);
		const double Vm = 0.5 * (N->V0 + N->V1);
		const int32 Ix = U < Um ? 0 : 1;
		const int32 Iy = V < Vm ? 0 : 1;
		N = N->Children[Iy * 2 + Ix].Get();
	}
	if (N)
	{
		N->bVisibleLeaf = true;
	}
	return N;
}
