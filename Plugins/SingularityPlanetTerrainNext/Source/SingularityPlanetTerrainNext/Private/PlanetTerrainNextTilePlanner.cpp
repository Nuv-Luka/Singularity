#include "PlanetTerrainNextTilePlanner.h"

void FPlanetTerrainNextTilePlanner::Plan(
	const TArray<FPlanetTerrainNextPendingBuild>& Pending,
	TFunctionRef<FPlanetTerrainNextTileId(const FPlanetTerrainNextNodeId&)> MakeTileId,
	double TargetPixelError,
	int64 Frame,
	int32 MaxPrefetch,
	TArray<FPlanetTerrainNextTileRequest>& OutRequests)
{
	Stats = FPlanetTerrainNextPlannerStats();
	OutRequests.Reset();
	OutRequests.Reserve(Pending.Num() + FMath::Max(MaxPrefetch, 0));

	TSet<FPlanetTerrainNextTileId> Seen;
	const double Target = FMath::Max(TargetPixelError, 1.0e-3);

	for (const FPlanetTerrainNextPendingBuild& PendingItem : Pending)
	{
		FPlanetTerrainNextTileRequest Request;
		Request.NodeId = PendingItem.Id;
		Request.TileId = MakeTileId(PendingItem.Id);
		Request.Priority = PendingItem.Priority;

		// CollectPending already boosts visible nodes by 1e12. Treat those as coverage/
		// refine; offscreen pending is frustum-or-prefetch class for scheduling.
		const bool bVisibleBoost = PendingItem.Priority >= 1.0e12;
		const double PixelError = bVisibleBoost ? (PendingItem.Priority - 1.0e12) : PendingItem.Priority;

		if (bVisibleBoost && PendingItem.Id.Level == 0)
		{
			Request.Class = EPlanetTerrainNextRequestClass::Coverage;
			Request.bRequiredForCoverage = true;
		}
		else if (bVisibleBoost && PixelError > Target)
		{
			Request.Class = EPlanetTerrainNextRequestClass::VisibleRefine;
			Request.bRequiredForCoverage = true; // parent stays drawn until children ready
		}
		else if (bVisibleBoost)
		{
			Request.Class = EPlanetTerrainNextRequestClass::Frustum;
			Request.bRequiredForCoverage = true;
		}
		else
		{
			Request.Class = EPlanetTerrainNextRequestClass::Prefetch;
			Request.bRequiredForCoverage = false;
		}

		// Class weight so coverage always sorts above optional warmup when priorities tie.
		const double ClassBoost = (3 - static_cast<int32>(Request.Class)) * 1.0e13;
		Request.Priority = ClassBoost + PendingItem.Priority;
		Request.DeadlineFrame = Frame + (Request.bRequiredForCoverage ? 2 : 30);

		if (Seen.Contains(Request.TileId))
		{
			continue;
		}
		Seen.Add(Request.TileId);
		OutRequests.Add(Request);

		switch (Request.Class)
		{
		case EPlanetTerrainNextRequestClass::Coverage: ++Stats.Coverage; break;
		case EPlanetTerrainNextRequestClass::VisibleRefine: ++Stats.VisibleRefine; break;
		case EPlanetTerrainNextRequestClass::Frustum: ++Stats.Frustum; break;
		case EPlanetTerrainNextRequestClass::Prefetch: ++Stats.Prefetch; break;
		}
	}

	// Bounded speculative: siblings of the highest-priority visible refine (same parent).
	int32 PrefetchLeft = MaxPrefetch;
	for (int32 Index = 0; Index < OutRequests.Num() && PrefetchLeft > 0; ++Index)
	{
		const FPlanetTerrainNextTileRequest& Seed = OutRequests[Index];
		if (Seed.Class != EPlanetTerrainNextRequestClass::VisibleRefine || Seed.NodeId.Level == 0)
		{
			continue;
		}
		const FPlanetTerrainNextNodeId Parent(
			Seed.NodeId.Face,
			static_cast<uint8>(Seed.NodeId.Level - 1),
			Seed.NodeId.X >> 1,
			Seed.NodeId.Y >> 1);
		for (int32 Child = 0; Child < 4 && PrefetchLeft > 0; ++Child)
		{
			const FPlanetTerrainNextNodeId Sibling = Parent.Child(Child);
			if (Sibling == Seed.NodeId)
			{
				continue;
			}
			const FPlanetTerrainNextTileId SiblingTile = MakeTileId(Sibling);
			if (Seen.Contains(SiblingTile))
			{
				continue;
			}
			Seen.Add(SiblingTile);
			FPlanetTerrainNextTileRequest Warm;
			Warm.NodeId = Sibling;
			Warm.TileId = SiblingTile;
			Warm.Class = EPlanetTerrainNextRequestClass::Prefetch;
			Warm.bRequiredForCoverage = false;
			Warm.Priority = static_cast<double>(PrefetchLeft);
			Warm.DeadlineFrame = Frame + 60;
			OutRequests.Add(Warm);
			++Stats.Prefetch;
			--PrefetchLeft;
		}
		break; // one seed family per frame
	}

	OutRequests.Sort([](const FPlanetTerrainNextTileRequest& A, const FPlanetTerrainNextTileRequest& B)
	{
		return A.Priority > B.Priority;
	});
	Stats.Emitted = OutRequests.Num();
}
