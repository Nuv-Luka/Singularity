#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrainNextHeightTile.h"

struct FPlanetTerrainNextDiskTileStoreStats
{
	int32 Loads = 0;
	int32 LoadHits = 0;
	int32 LoadMisses = 0;
	int32 Saves = 0;
	int32 SaveSkipped = 0;
	int32 Prunes = 0;
};

/**
 * Versioned binary height tiles under Saved/SingularityPlanetTerrainNext/Tiles.
 * Thread-safe for concurrent TryLoad/Save (each file is independent).
 */
class SINGULARITYPLANETTERRAINNEXT_API FPlanetTerrainNextDiskTileStore
{
public:
	static constexpr uint32 Magic = 0x544E5053; // 'SPNT' LE
	static constexpr uint32 FormatVersion = 1;

	explicit FPlanetTerrainNextDiskTileStore(const FString& InRootDirectory = FString());

	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }
	bool IsEnabled() const { return bEnabled; }

	/** Soft cap on total bytes under Root; 0 = unlimited. Prunes oldest files before Save. */
	void SetMaxBytes(int64 InMaxBytes) { MaxBytes = FMath::Max<int64>(InMaxBytes, 0); }
	int64 GetMaxBytes() const { return MaxBytes; }

	void SetRootDirectory(const FString& InRoot);
	const FString& GetRootDirectory() const { return RootDirectory; }

	bool TryLoad(const FPlanetTerrainNextTileId& Id, FPlanetTerrainNextHeightTile& OutTile);
	bool Save(const FPlanetTerrainNextHeightTile& Tile);

	FString FilePathFor(const FPlanetTerrainNextTileId& Id) const;
	void BeginFrame();
	const FPlanetTerrainNextDiskTileStoreStats& GetStats() const { return Stats; }

	/** Default: ProjectSavedDir/SingularityPlanetTerrainNext/Tiles */
	static FString DefaultRootDirectory();

private:
	bool EnsureDirectory() const;
	void PruneToBudget(int64 NeededBytes);
	int64 EstimateDirectoryBytes() const;

	FString RootDirectory;
	bool bEnabled = true;
	int64 MaxBytes = 512 * 1024 * 1024; // 512 MB
	FPlanetTerrainNextDiskTileStoreStats Stats;
	FCriticalSection Mutex;
};
