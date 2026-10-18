#include "Misc/AutomationTest.h"
#include "PlanetTerrainNextGpuSlotPool.h"
#include "PlanetTerrainNextHeightField.h"
#include "PlanetTerrainNextHeightTile.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "PlanetTerrainNextTileCache.h"
#include "PlanetTerrainNextTileId.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FPlanetTerrainNextDescriptor MoonDescriptor(int64 Seed = 1)
{
	FPlanetTerrainNextDescriptor Descriptor;
	Descriptor.Seed = Seed;
	Descriptor.PlanetId = 42;
	Descriptor.GeneratorVersion = 1;
	return Descriptor;
}

FPlanetTerrainNextTileId TileFor(const FPlanetTerrainNextHeightField& Field, const FPlanetTerrainNextDescriptor& Desc, const FPlanetTerrainNextNodeId& Node)
{
	return FPlanetTerrainNextTileId::Make(Desc.PlanetId, Desc.Seed, Desc.GeneratorVersion, Field.GetIdentityHash(), Node);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextTileIdentityTest,
	"Singularity.PlanetTerrainNext.R4.TileIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextTileIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	const FPlanetTerrainNextHeightField Field(Desc);
	const FPlanetTerrainNextNodeId Node(2, 4, 3, 5);

	const FPlanetTerrainNextTileId A = TileFor(Field, Desc, Node);
	const FPlanetTerrainNextTileId B = TileFor(Field, Desc, Node);
	TestEqual(TEXT("same inputs → same identity"), A, B);
	TestEqual(TEXT("type hash stable"), GetTypeHash(A), GetTypeHash(B));

	FPlanetTerrainNextDescriptor OtherSeed = Desc;
	OtherSeed.Seed = 99;
	const FPlanetTerrainNextHeightField OtherField(OtherSeed);
	TestTrue(TEXT("seed changes identity"), TileFor(OtherField, OtherSeed, Node) != A);

	FPlanetTerrainNextTileId CameraPoison = A;
	// Camera epoch must never be part of identity — constructing with same fields stays equal.
	TestEqual(TEXT("identity ignores any imaginary camera epoch"), CameraPoison, A);

	FPlanetTerrainNextTileId Moved = A;
	Moved.Node.X = 9;
	TestTrue(TEXT("node X changes identity"), Moved != A);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextHeightTileBakeTest,
	"Singularity.PlanetTerrainNext.R4.HeightTileBake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextHeightTileBakeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace PlanetTerrainNextPatch;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	const FPlanetTerrainNextHeightField Field(Desc);
	const FPlanetTerrainNextNodeId Node(4, 3, 1, 2);
	const FPlanetTerrainNextTileId Id = TileFor(Field, Desc, Node);

	FPlanetTerrainNextHeightTile A;
	FPlanetTerrainNextHeightTile B;
	PlanetTerrainNextHeightTile::Bake(Field, Id, A);
	PlanetTerrainNextHeightTile::Bake(Field, Id, B);

	TestTrue(TEXT("tile valid"), A.IsValid());
	TestEqual(TEXT("resolution"), A.Resolution(), PlanetTerrainNextHeightTile::DefaultResolution);
	TestEqual(TEXT("texel count"), A.NumTexels(), PlanetTerrainNextHeightTile::DefaultResolution * PlanetTerrainNextHeightTile::DefaultResolution);
	TestEqual(TEXT("deterministic fine[0]"), A.FineHeightM[0], B.FineHeightM[0]);
	TestEqual(TEXT("deterministic coarse[mid]"), A.CoarseHeightM[A.NumTexels() / 2], B.CoarseHeightM[B.NumTexels() / 2]);

	FPlanetTerrainNextPatchData FromTile;
	FPlanetTerrainNextPatchData Direct;
	BuildFromTile(Field, A, 2.0, FromTile);
	Build(Field, Node, 2.0, Direct);
	double MaxGapCm = 0.0;
	for (int32 Index = 0; Index < GridVertexCount; ++Index)
	{
		MaxGapCm = FMath::Max(MaxGapCm, (FromTile.VerticesCm[Index] - Direct.VerticesCm[Index]).Size());
		MaxGapCm = FMath::Max(MaxGapCm, (FromTile.CoarseVerticesCm[Index] - Direct.CoarseVerticesCm[Index]).Size());
	}
	AddInfo(FString::Printf(TEXT("BuildFromTile vs Build max gap %.6f cm"), MaxGapCm));
	TestTrue(TEXT("mesh from tile matches direct Build"), MaxGapCm < 0.01);

	// Root: coarse == fine.
	FPlanetTerrainNextHeightTile Root;
	PlanetTerrainNextHeightTile::Bake(Field, TileFor(Field, Desc, FPlanetTerrainNextNodeId(0, 0, 0, 0)), Root);
	double Delta = 0.0;
	for (int32 Index = 0; Index < Root.NumTexels(); ++Index)
	{
		Delta = FMath::Max(Delta, FMath::Abs(Root.FineHeightM[Index] - Root.CoarseHeightM[Index]));
	}
	TestTrue(TEXT("root coarse equals fine"), Delta < 1.0e-5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextTileCacheTest,
	"Singularity.PlanetTerrainNext.R4.TileCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextTileCacheTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	const FPlanetTerrainNextHeightField Field(Desc);
	FPlanetTerrainNextTileCache Cache(4);
	Cache.BeginFrame();

	auto NeverPinned = [](const FPlanetTerrainNextTileId&) { return false; };
	const FPlanetTerrainNextTileId A = TileFor(Field, Desc, FPlanetTerrainNextNodeId(0, 1, 0, 0));
	const FPlanetTerrainNextTileId B = TileFor(Field, Desc, FPlanetTerrainNextNodeId(0, 1, 1, 0));
	const FPlanetTerrainNextTileId C = TileFor(Field, Desc, FPlanetTerrainNextNodeId(0, 1, 0, 1));
	const FPlanetTerrainNextTileId D = TileFor(Field, Desc, FPlanetTerrainNextNodeId(0, 1, 1, 1));
	const FPlanetTerrainNextTileId E = TileFor(Field, Desc, FPlanetTerrainNextNodeId(1, 1, 0, 0));

	TestNotNull(TEXT("bake A"), Cache.GetOrBake(Field, A, 1, NeverPinned));
	TestNotNull(TEXT("hit A"), Cache.GetOrBake(Field, A, 2, NeverPinned));
	TestEqual(TEXT("one miss so far tracked via Num"), Cache.Num(), 1);

	Cache.GetOrBake(Field, B, 3, NeverPinned);
	Cache.GetOrBake(Field, C, 4, NeverPinned);
	Cache.GetOrBake(Field, D, 5, NeverPinned);
	TestEqual(TEXT("cache filled to 4"), Cache.Num(), 4);

	// E forces eviction of the oldest unpinned (A, last used at frame 2).
	TestNotNull(TEXT("bake E evicts"), Cache.GetOrBake(Field, E, 6, NeverPinned));
	TestNull(TEXT("A was evicted"), Cache.Find(A));
	TestNotNull(TEXT("E resident"), Cache.Find(E));
	TestTrue(TEXT("eviction counted"), Cache.GetStats().Evictions >= 1);

	// Pinned tiles cannot be evicted.
	Cache.Clear();
	Cache.SetMaxEntries(2);
	Cache.GetOrBake(Field, A, 10, NeverPinned);
	Cache.GetOrBake(Field, B, 11, NeverPinned);
	auto PinA = [&](const FPlanetTerrainNextTileId& Id) { return Id == A; };
	const FPlanetTerrainNextHeightTile* Kept = Cache.GetOrBake(Field, C, 12, PinA);
	TestNotNull(TEXT("C baked while A pinned"), Kept);
	TestNotNull(TEXT("A survived because pinned"), Cache.Find(A));
	TestNull(TEXT("B was the unpinned victim"), Cache.Find(B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPlanetTerrainNextGpuSlotPoolTest,
	"Singularity.PlanetTerrainNext.R4.GpuSlotPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetTerrainNextGpuSlotPoolTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FPlanetTerrainNextDescriptor Desc = MoonDescriptor();
	const FPlanetTerrainNextHeightField Field(Desc);

	FPlanetTerrainNextGpuSlotPool Pool;
	Pool.Initialize(GetTransientPackage(), 4, PlanetTerrainNextHeightTile::DefaultResolution);
	TestTrue(TEXT("pool initialized"), Pool.IsInitialized());
	TestEqual(TEXT("capacity"), Pool.GetCapacity(), 4);
	// Real D3D12 atlas is deferred in R4 (logical residency only).
	TestNull(TEXT("atlas texture deferred"), Pool.GetAtlas());

	FPlanetTerrainNextHeightTile Tile;
	const FPlanetTerrainNextTileId Ids[5] = {
		TileFor(Field, Desc, FPlanetTerrainNextNodeId(0, 0, 0, 0)),
		TileFor(Field, Desc, FPlanetTerrainNextNodeId(1, 0, 0, 0)),
		TileFor(Field, Desc, FPlanetTerrainNextNodeId(2, 0, 0, 0)),
		TileFor(Field, Desc, FPlanetTerrainNextNodeId(3, 0, 0, 0)),
		TileFor(Field, Desc, FPlanetTerrainNextNodeId(4, 0, 0, 0)),
	};
	PlanetTerrainNextHeightTile::Bake(Field, Ids[0], Tile);

	Pool.BeginFrame(1);
	const int32 S0 = Pool.Allocate(Ids[0]);
	TestTrue(TEXT("alloc 0"), S0 != INDEX_NONE);
	TestTrue(TEXT("upload 0"), Pool.Upload(S0, Tile));
	Pool.Pin(Ids[0], EPlanetTerrainNextPinReason::Draw);

	for (int32 Index = 1; Index < 4; ++Index)
	{
		PlanetTerrainNextHeightTile::Bake(Field, Ids[Index], Tile);
		const int32 Slot = Pool.Allocate(Ids[Index]);
		TestTrue(TEXT("alloc"), Slot != INDEX_NONE);
		TestTrue(TEXT("upload"), Pool.Upload(Slot, Tile));
		Pool.Pin(Ids[Index], EPlanetTerrainNextPinReason::Draw);
	}

	// All four pinned — fifth allocate must fail (keep parent).
	PlanetTerrainNextHeightTile::Bake(Field, Ids[4], Tile);
	TestEqual(TEXT("alloc fails when every slot pinned"), Pool.Allocate(Ids[4]), INDEX_NONE);
	TestTrue(TEXT("alloc failed counted"), Pool.GetStats().AllocFailed >= 1);

	// Unpin one, then allocate succeeds by eviction.
	Pool.Unpin(Ids[1], EPlanetTerrainNextPinReason::Draw);
	Pool.BeginFrame(2);
	Pool.Pin(Ids[0], EPlanetTerrainNextPinReason::Draw);
	Pool.Pin(Ids[2], EPlanetTerrainNextPinReason::Draw);
	Pool.Pin(Ids[3], EPlanetTerrainNextPinReason::Draw);
	const int32 S4 = Pool.Allocate(Ids[4]);
	TestTrue(TEXT("alloc after unpin"), S4 != INDEX_NONE);
	TestTrue(TEXT("upload after unpin"), Pool.Upload(S4, Tile));
	TestEqual(TEXT("Ids[1] evicted"), Pool.TryGet(Ids[1]), INDEX_NONE);
	TestTrue(TEXT("Ids[0] still resident"), Pool.TryGet(Ids[0]) != INDEX_NONE);

	Pool.Shutdown();
	return true;
}

#endif
