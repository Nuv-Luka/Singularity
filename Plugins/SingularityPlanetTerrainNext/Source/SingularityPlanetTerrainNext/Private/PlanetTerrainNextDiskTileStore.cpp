#include "PlanetTerrainNextDiskTileStore.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
template <typename T>
void WritePod(FArchive& Ar, const T& Value)
{
	Ar.Serialize(const_cast<T*>(&Value), sizeof(T));
}

template <typename T>
void ReadPod(FArchive& Ar, T& Value)
{
	Ar.Serialize(&Value, sizeof(T));
}

void WriteFloatArray(FArchive& Ar, const TArray<float>& Data)
{
	const int32 Num = Data.Num();
	WritePod(Ar, Num);
	if (Num > 0)
	{
		Ar.Serialize(const_cast<float*>(Data.GetData()), Num * sizeof(float));
	}
}

bool ReadFloatArray(FArchive& Ar, TArray<float>& Data, int32 Expected)
{
	int32 Num = 0;
	ReadPod(Ar, Num);
	if (Num != Expected || Num < 0 || Num > 1024 * 1024)
	{
		return false;
	}
	Data.SetNumUninitialized(Num);
	Ar.Serialize(Data.GetData(), Num * sizeof(float));
	return !Ar.IsError();
}
} // namespace

FString FPlanetTerrainNextDiskTileStore::DefaultRootDirectory()
{
	return FPaths::ProjectSavedDir() / TEXT("SingularityPlanetTerrainNext") / TEXT("Tiles");
}

FPlanetTerrainNextDiskTileStore::FPlanetTerrainNextDiskTileStore(const FString& InRootDirectory)
	: RootDirectory(InRootDirectory.IsEmpty() ? DefaultRootDirectory() : InRootDirectory)
{
}

void FPlanetTerrainNextDiskTileStore::SetRootDirectory(const FString& InRoot)
{
	FScopeLock Lock(&Mutex);
	RootDirectory = InRoot.IsEmpty() ? DefaultRootDirectory() : InRoot;
}

void FPlanetTerrainNextDiskTileStore::BeginFrame()
{
	FScopeLock Lock(&Mutex);
	Stats = FPlanetTerrainNextDiskTileStoreStats();
}

bool FPlanetTerrainNextDiskTileStore::EnsureDirectory() const
{
	return IFileManager::Get().MakeDirectory(*RootDirectory, true);
}

FString FPlanetTerrainNextDiskTileStore::FilePathFor(const FPlanetTerrainNextTileId& Id) const
{
	// Stable, filesystem-safe name from full identity (not just GetTypeHash — avoid collisions).
	const FString Name = FString::Printf(
		TEXT("%016llX_%016llX_%08X_%016llX_F%u_L%u_%u_%u_R%d_D%d.sptn"),
		static_cast<uint64>(Id.PlanetId),
		static_cast<uint64>(Id.Seed),
		static_cast<uint32>(Id.GeneratorVersion),
		Id.ParameterHash,
		static_cast<uint32>(Id.Node.Face),
		static_cast<uint32>(Id.Node.Level),
		Id.Node.X,
		Id.Node.Y,
		Id.Resolution,
		Id.DataFormat);
	return RootDirectory / Name;
}

int64 FPlanetTerrainNextDiskTileStore::EstimateDirectoryBytes() const
{
	int64 Total = 0;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(RootDirectory / TEXT("*.sptn")), true, false);
	for (const FString& File : Files)
	{
		const int64 Size = IFileManager::Get().FileSize(*(RootDirectory / File));
		if (Size > 0)
		{
			Total += Size;
		}
	}
	return Total;
}

void FPlanetTerrainNextDiskTileStore::PruneToBudget(int64 NeededBytes)
{
	if (MaxBytes <= 0)
	{
		return;
	}
	int64 Total = EstimateDirectoryBytes();
	if (Total + NeededBytes <= MaxBytes)
	{
		return;
	}

	struct FDated
	{
		FString Path;
		FDateTime Time;
		int64 Size = 0;
	};
	TArray<FDated> Dated;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(RootDirectory / TEXT("*.sptn")), true, false);
	Dated.Reserve(Files.Num());
	for (const FString& File : Files)
	{
		const FString Path = RootDirectory / File;
		FDated Entry;
		Entry.Path = Path;
		Entry.Time = IFileManager::Get().GetTimeStamp(*Path);
		Entry.Size = FMath::Max<int64>(0, IFileManager::Get().FileSize(*Path));
		Dated.Add(MoveTemp(Entry));
	}
	Dated.Sort([](const FDated& A, const FDated& B) { return A.Time < B.Time; });

	for (const FDated& Entry : Dated)
	{
		if (Total + NeededBytes <= MaxBytes)
		{
			break;
		}
		if (IFileManager::Get().Delete(*Entry.Path))
		{
			Total -= Entry.Size;
			++Stats.Prunes;
		}
	}
}

bool FPlanetTerrainNextDiskTileStore::TryLoad(const FPlanetTerrainNextTileId& Id, FPlanetTerrainNextHeightTile& OutTile)
{
	FScopeLock Lock(&Mutex);
	++Stats.Loads;
	if (!bEnabled)
	{
		++Stats.LoadMisses;
		return false;
	}

	const FString Path = FilePathFor(Id);
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 64)
	{
		++Stats.LoadMisses;
		return false;
	}

	FMemoryReader Reader(Bytes, true);
	uint32 FileMagic = 0;
	uint32 Version = 0;
	ReadPod(Reader, FileMagic);
	ReadPod(Reader, Version);
	if (FileMagic != Magic || Version != FormatVersion || Reader.IsError())
	{
		++Stats.LoadMisses;
		return false;
	}

	FPlanetTerrainNextHeightTile Tile;
	ReadPod(Reader, Tile.Id.PlanetId);
	ReadPod(Reader, Tile.Id.Seed);
	ReadPod(Reader, Tile.Id.GeneratorVersion);
	ReadPod(Reader, Tile.Id.ParameterHash);
	ReadPod(Reader, Tile.Id.Node.Face);
	ReadPod(Reader, Tile.Id.Node.Level);
	uint16 Pad = 0;
	ReadPod(Reader, Pad);
	ReadPod(Reader, Tile.Id.Node.X);
	ReadPod(Reader, Tile.Id.Node.Y);
	ReadPod(Reader, Tile.Id.Resolution);
	ReadPod(Reader, Tile.Id.DataFormat);
	ReadPod(Reader, Tile.MinHeightM);
	ReadPod(Reader, Tile.MaxHeightM);
	ReadPod(Reader, Tile.RadiusM);

	if (Tile.Id != Id || Tile.Id.Resolution <= 0)
	{
		++Stats.LoadMisses;
		return false;
	}

	const int32 Expected = Tile.Id.Resolution * Tile.Id.Resolution;
	if (!ReadFloatArray(Reader, Tile.FineHeightM, Expected)
		|| !ReadFloatArray(Reader, Tile.CoarseHeightM, Expected)
		|| Reader.IsError()
		|| !Tile.IsValid())
	{
		++Stats.LoadMisses;
		return false;
	}

	OutTile = MoveTemp(Tile);
	++Stats.LoadHits;
	return true;
}

bool FPlanetTerrainNextDiskTileStore::Save(const FPlanetTerrainNextHeightTile& Tile)
{
	FScopeLock Lock(&Mutex);
	if (!bEnabled || !Tile.IsValid())
	{
		++Stats.SaveSkipped;
		return false;
	}
	if (!EnsureDirectory())
	{
		++Stats.SaveSkipped;
		return false;
	}

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true);
	WritePod(Writer, Magic);
	WritePod(Writer, FormatVersion);
	WritePod(Writer, Tile.Id.PlanetId);
	WritePod(Writer, Tile.Id.Seed);
	WritePod(Writer, Tile.Id.GeneratorVersion);
	WritePod(Writer, Tile.Id.ParameterHash);
	WritePod(Writer, Tile.Id.Node.Face);
	WritePod(Writer, Tile.Id.Node.Level);
	const uint16 Pad = 0;
	WritePod(Writer, Pad);
	WritePod(Writer, Tile.Id.Node.X);
	WritePod(Writer, Tile.Id.Node.Y);
	WritePod(Writer, Tile.Id.Resolution);
	WritePod(Writer, Tile.Id.DataFormat);
	WritePod(Writer, Tile.MinHeightM);
	WritePod(Writer, Tile.MaxHeightM);
	WritePod(Writer, Tile.RadiusM);
	WriteFloatArray(Writer, Tile.FineHeightM);
	WriteFloatArray(Writer, Tile.CoarseHeightM);

	PruneToBudget(Bytes.Num());

	const FString Path = FilePathFor(Tile.Id);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		++Stats.SaveSkipped;
		return false;
	}
	++Stats.Saves;
	return true;
}
