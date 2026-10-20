#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextDetailTile.h"
#include "PlanetTerrainNextHeightField.h"

class UTexture2D;

struct SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextDetailStoreStats
{
	int32 ResidentRoots = 0;
	int32 PendingBakes = 0;
	int32 Baking = 0;
	int32 BakeStarts = 0;
	int32 CompletedThisFrame = 0;
	int32 UploadsThisFrame = 0;
	int32 ReadyFaces = 0;
	double LastBakeMs = 0.0;
	double P95BakeMs = 0.0;
	double LastUploadMs = 0.0;
};

/**
 * R9.3: six root-face albedo/normal textures from the same HeightField as geometry.
 * Async CPU bake, budgeted GPU upload. Visual LOD is independent of mesh residency.
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextDetailStore
{
public:
	FPlanetTerrainNextDetailStore();
	~FPlanetTerrainNextDetailStore();

	void Reset(int64 PlanetId, uint64 GeneratorHash, uint16 Resolution);
	void Shutdown();

	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }
	bool IsEnabled() const { return bEnabled; }
	void SetBudgets(int32 MaxConcurrentBakes, int32 UploadsPerFrame);

	/**
	 * Queue missing roots, launch workers, drain finished CPU tiles, upload ≤ UploadsPerFrame.
	 * TextureOuter owns transient UTexture2Ds (also pin via component UPROPERTY).
	 * Returns true if any face became GPU-ready this tick.
	 */
	bool Tick(
		const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField,
		UObject* TextureOuter,
		const TArray<uint8>& FacePriority);

	UTexture2D* GetAlbedo(uint8 Face) const;
	UTexture2D* GetNormal(uint8 Face) const;
	bool IsFaceReady(uint8 Face) const;
	int32 ReadyFaceCount() const { return Stats.ReadyFaces; }
	uint16 GetResolution() const { return Resolution; }

	const FPlanetTerrainNextDetailStoreStats& GetStats() const { return Stats; }

	bool ConsumeDirty()
	{
		const bool b = bDirty;
		bDirty = false;
		return b;
	}

private:
	enum class EFaceState : uint8
	{
		Empty = 0,
		Queued,
		Baking,
		ReadyCpu,
		ReadyGpu
	};

	struct FBakeJob
	{
		FPlanetTerrainNextDetailTileId Id;
		TSharedPtr<FPlanetTerrainNextDetailTile, ESPMode::ThreadSafe> Result;
		TAtomic<bool> bDone{false};
		TAtomic<bool> bCancel{false};
		double BakeMs = 0.0;
	};

	struct FFaceSlot
	{
		FPlanetTerrainNextDetailTileId Id;
		EFaceState State = EFaceState::Empty;
		TSharedPtr<FBakeJob> Job;
		TSharedPtr<FPlanetTerrainNextDetailTile, ESPMode::ThreadSafe> CpuTile;
		UTexture2D* Albedo = nullptr;
		UTexture2D* Normal = nullptr;
	};

	void StartBake(int32 Face, const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField);
	bool UploadFace(int32 Face, UObject* TextureOuter);
	void RecordBakeTime(double Ms);
	void RecountReady();

	int64 PlanetId = 0;
	uint64 GeneratorHash = 0;
	uint16 Resolution = 512;
	bool bEnabled = true;
	bool bDirty = false;
	int32 MaxConcurrentBakes = 2;
	int32 UploadsPerFrame = 1;
	FFaceSlot Faces[6];
	FPlanetTerrainNextDetailStoreStats Stats;
	TArray<double> BakeTimesMs;
	int32 BakeTimeCursor = 0;
};
