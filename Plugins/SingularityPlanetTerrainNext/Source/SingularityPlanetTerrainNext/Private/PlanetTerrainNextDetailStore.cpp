#include "PlanetTerrainNextDetailStore.h"

#include "PlanetTerrainNextLog.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"

namespace
{
UTexture2D* CreateDetailTexture(
	UObject* Outer,
	FName Name,
	int32 Res,
	const TArray<FColor>& Pixels,
	bool bSRGB,
	TextureCompressionSettings Compression)
{
	if (!Outer || Pixels.Num() != Res * Res)
	{
		return nullptr;
	}

	UTexture2D* Tex = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8, Name);
	if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	Tex->Rename(nullptr, Outer, REN_DoNotDirty | REN_DontCreateRedirectors);
	Tex->SRGB = bSRGB;
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	Tex->CompressionSettings = Compression;
	Tex->MipGenSettings = TMGS_NoMipmaps;
	Tex->NeverStream = true;

	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}
} // namespace

FPlanetTerrainNextDetailStore::FPlanetTerrainNextDetailStore() = default;

FPlanetTerrainNextDetailStore::~FPlanetTerrainNextDetailStore()
{
	Shutdown();
}

void FPlanetTerrainNextDetailStore::Reset(int64 InPlanetId, uint64 InGeneratorHash, uint16 InResolution)
{
	Shutdown();
	PlanetId = InPlanetId;
	GeneratorHash = InGeneratorHash;
	Resolution = static_cast<uint16>(FMath::Clamp(static_cast<int32>(InResolution), 64, 1024));
	Stats = FPlanetTerrainNextDetailStoreStats();
	BakeTimesMs.Reset();
	BakeTimeCursor = 0;
	bDirty = false;

	for (int32 Face = 0; Face < 6; ++Face)
	{
		Faces[Face] = FFaceSlot();
		Faces[Face].Id = FPlanetTerrainNextDetailTileId::MakeRoot(
			PlanetId, GeneratorHash, static_cast<uint8>(Face), Resolution);
		Faces[Face].State = bEnabled ? EFaceState::Queued : EFaceState::Empty;
	}
	Stats.PendingBakes = bEnabled ? 6 : 0;
}

void FPlanetTerrainNextDetailStore::Shutdown()
{
	for (int32 Face = 0; Face < 6; ++Face)
	{
		if (Faces[Face].Job.IsValid())
		{
			Faces[Face].Job->bCancel.Store(true);
		}
		Faces[Face].Job.Reset();
		Faces[Face].Albedo = nullptr;
		Faces[Face].Normal = nullptr;
		Faces[Face].CpuTile.Reset();
		Faces[Face].State = EFaceState::Empty;
	}
	Stats = FPlanetTerrainNextDetailStoreStats();
	bDirty = false;
}

void FPlanetTerrainNextDetailStore::SetBudgets(int32 InMaxConcurrentBakes, int32 InUploadsPerFrame)
{
	MaxConcurrentBakes = FMath::Clamp(InMaxConcurrentBakes, 1, 6);
	UploadsPerFrame = FMath::Clamp(InUploadsPerFrame, 0, 6);
}

UTexture2D* FPlanetTerrainNextDetailStore::GetAlbedo(uint8 Face) const
{
	return Face < 6 ? Faces[Face].Albedo : nullptr;
}

UTexture2D* FPlanetTerrainNextDetailStore::GetNormal(uint8 Face) const
{
	return Face < 6 ? Faces[Face].Normal : nullptr;
}

bool FPlanetTerrainNextDetailStore::IsFaceReady(uint8 Face) const
{
	return Face < 6 && Faces[Face].State == EFaceState::ReadyGpu && Faces[Face].Albedo && Faces[Face].Normal;
}

void FPlanetTerrainNextDetailStore::RecountReady()
{
	Stats.ReadyFaces = 0;
	Stats.ResidentRoots = 0;
	Stats.PendingBakes = 0;
	Stats.Baking = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		switch (Faces[Face].State)
		{
		case EFaceState::ReadyGpu:
			++Stats.ReadyFaces;
			++Stats.ResidentRoots;
			break;
		case EFaceState::ReadyCpu:
			++Stats.ResidentRoots;
			++Stats.PendingBakes;
			break;
		case EFaceState::Queued:
			++Stats.PendingBakes;
			break;
		case EFaceState::Baking:
			++Stats.Baking;
			++Stats.PendingBakes;
			break;
		default:
			break;
		}
	}
}

void FPlanetTerrainNextDetailStore::RecordBakeTime(double Ms)
{
	Stats.LastBakeMs = Ms;
	if (BakeTimesMs.Num() < 64)
	{
		BakeTimesMs.Add(Ms);
	}
	else
	{
		BakeTimesMs[BakeTimeCursor % BakeTimesMs.Num()] = Ms;
		++BakeTimeCursor;
	}
	TArray<double> Sorted = BakeTimesMs;
	Sorted.Sort();
	Stats.P95BakeMs = Sorted[FMath::Clamp(FMath::CeilToInt(0.95 * Sorted.Num()) - 1, 0, Sorted.Num() - 1)];
}

void FPlanetTerrainNextDetailStore::StartBake(
	int32 Face,
	const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField)
{
	if (Face < 0 || Face >= 6 || !HeightField.IsValid())
	{
		return;
	}
	FFaceSlot& Slot = Faces[Face];
	TSharedPtr<FBakeJob> Job = MakeShared<FBakeJob>();
	Job->Id = Slot.Id;
	Job->Result = MakeShared<FPlanetTerrainNextDetailTile, ESPMode::ThreadSafe>();
	Job->bDone.Store(false);
	Job->bCancel.Store(false);
	Slot.Job = Job;
	Slot.State = EFaceState::Baking;

	const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe> Field = HeightField;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Field, Job]()
	{
		if (!Field.IsValid() || !Job.IsValid() || Job->bCancel.Load())
		{
			if (Job.IsValid())
			{
				Job->bDone.Store(true);
			}
			return;
		}
		const double Start = FPlatformTime::Seconds();
		PlanetTerrainNextDetailTile::Bake(*Field, Job->Id, *Job->Result);
		Job->BakeMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		Job->bDone.Store(true);
	});
	++Stats.BakeStarts;
}

bool FPlanetTerrainNextDetailStore::UploadFace(int32 Face, UObject* TextureOuter)
{
	if (Face < 0 || Face >= 6 || !TextureOuter)
	{
		return false;
	}
	FFaceSlot& Slot = Faces[Face];
	if (!Slot.CpuTile.IsValid() || !Slot.CpuTile->bValid)
	{
		Slot.State = EFaceState::Queued;
		Slot.CpuTile.Reset();
		return false;
	}

	const double Start = FPlatformTime::Seconds();
	const int32 Res = Slot.Id.Resolution;
	const FName AlbedoName(*FString::Printf(TEXT("PTN_DetailAlbedo_F%d"), Face));
	const FName NormalName(*FString::Printf(TEXT("PTN_DetailNormal_F%d"), Face));

	Slot.Albedo = CreateDetailTexture(
		TextureOuter, AlbedoName, Res, Slot.CpuTile->Albedo, true, TC_Default);
	Slot.Normal = CreateDetailTexture(
		TextureOuter, NormalName, Res, Slot.CpuTile->Normal, false, TC_Normalmap);

	Stats.LastUploadMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	Slot.CpuTile.Reset();

	if (!Slot.Albedo || !Slot.Normal)
	{
		UE_LOG(LogPlanetTerrainNext, Warning, TEXT("DetailStore: GPU upload failed face %d"), Face);
		Slot.Albedo = nullptr;
		Slot.Normal = nullptr;
		Slot.State = EFaceState::Queued;
		return false;
	}

	Slot.State = EFaceState::ReadyGpu;
	bDirty = true;
	return true;
}

bool FPlanetTerrainNextDetailStore::Tick(
	const TSharedPtr<FPlanetTerrainNextHeightField, ESPMode::ThreadSafe>& HeightField,
	UObject* TextureOuter)
{
	Stats.CompletedThisFrame = 0;
	Stats.UploadsThisFrame = 0;
	Stats.BakeStarts = 0;

	if (!bEnabled || !HeightField.IsValid())
	{
		RecountReady();
		return false;
	}

	for (int32 Face = 0; Face < 6; ++Face)
	{
		FFaceSlot& Slot = Faces[Face];
		if (Slot.State == EFaceState::Baking && Slot.Job.IsValid() && Slot.Job->bDone.Load())
		{
			if (Slot.Job->bCancel.Load() || !Slot.Job->Result.IsValid() || !Slot.Job->Result->bValid)
			{
				Slot.Job.Reset();
				Slot.State = EFaceState::Queued;
			}
			else
			{
				RecordBakeTime(Slot.Job->BakeMs);
				Slot.CpuTile = Slot.Job->Result;
				Slot.Job.Reset();
				Slot.State = EFaceState::ReadyCpu;
				++Stats.CompletedThisFrame;
			}
		}
	}

	int32 InFlight = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		if (Faces[Face].State == EFaceState::Baking
			&& Faces[Face].Job.IsValid()
			&& !Faces[Face].Job->bDone.Load())
		{
			++InFlight;
		}
	}
	for (int32 Face = 0; Face < 6 && InFlight < MaxConcurrentBakes; ++Face)
	{
		if (Faces[Face].State == EFaceState::Queued)
		{
			StartBake(Face, HeightField);
			++InFlight;
		}
	}

	for (int32 Face = 0; Face < 6 && Stats.UploadsThisFrame < UploadsPerFrame; ++Face)
	{
		if (Faces[Face].State == EFaceState::ReadyCpu)
		{
			if (UploadFace(Face, TextureOuter))
			{
				++Stats.UploadsThisFrame;
			}
		}
	}

	RecountReady();
	return Stats.UploadsThisFrame > 0;
}
