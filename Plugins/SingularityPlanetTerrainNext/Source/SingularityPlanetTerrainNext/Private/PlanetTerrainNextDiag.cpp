#include "PlanetTerrainNextDiag.h"

#include "PlanetTerrainNextCoordinates.h"
#include "PlanetTerrainNextLog.h"
#include "PlanetTerrainNextPatchMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
TAutoConsoleVariable<bool> CVarDiagEnable(
	TEXT("singularity.TerrainNext.Diag.Enable"),
	false,
	TEXT("R10.1: bypass streaming; render frozen diagnostic patches only."));

TAutoConsoleVariable<int32> CVarDiagMode(
	TEXT("singularity.TerrainNext.Diag.Mode"),
	1,
	TEXT("R10.1: 0=SinglePatch, 1=SixFaces."));

TAutoConsoleVariable<int32> CVarDiagLevel(
	TEXT("singularity.TerrainNext.Diag.Level"),
	0,
	TEXT("R10.1: frozen LOD level (0–2 recommended)."));

TAutoConsoleVariable<int32> CVarDiagFace(
	TEXT("singularity.TerrainNext.Diag.Face"),
	0,
	TEXT("R10.1: cube face 0–5 when Mode=SinglePatch."));

TAutoConsoleVariable<bool> CVarDiagHeight(
	TEXT("singularity.TerrainNext.Diag.Height"),
	false,
	TEXT("R10.1: 0=flat sphere, 1=HeightField relief."));

TAutoConsoleVariable<bool> CVarDiagTwoSided(
	TEXT("singularity.TerrainNext.Diag.TwoSided"),
	true,
	TEXT("R10.1: diagnostic material two-sided (off = front cull test)."));

TAutoConsoleVariable<bool> CVarDiagForceFreshSection(
	TEXT("singularity.TerrainNext.Diag.ForceFreshSection"),
	true,
	TEXT("R10.1: always Clear+Create mesh sections (never UpdateMeshSection)."));

constexpr TCHAR DiagMaterialPathTwoSided[] =
	TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextDiag_v1.M_PlanetTerrainNextDiag_v1");
constexpr TCHAR DiagMaterialPathOneSided[] =
	TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextDiag_OneSided_v1.M_PlanetTerrainNextDiag_OneSided_v1");

#if WITH_EDITOR
UMaterialInterface* CreateDiagMaterial(const FString& PackagePath, const FName AssetName, bool bTwoSided)
{
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		return nullptr;
	}
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
		UMaterial::StaticClass(), Package, AssetName,
		RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Material)
	{
		return nullptr;
	}
	Material->BlendMode = BLEND_Opaque;
	Material->SetShadingModel(MSM_Unlit);
	Material->TwoSided = bTwoSided;
	Material->SetMaterialUsage(MATUSAGE_StaticMesh);

	auto* Color = Cast<UMaterialExpressionVectorParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -400, 0));
	Color->ParameterName = TEXT("Color");
	Color->DefaultValue = FLinearColor(0.75f, 0.72f, 0.68f, 1.0f);
	UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT("RGB"), MP_EmissiveColor);
	UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);

	FAssetRegistryModule::AssetCreated(Material);
	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Material, *Filename, SaveArgs);
	return Material;
}
#endif
} // namespace

FPlanetTerrainNextDiagSettings FPlanetTerrainNextDiagSettings::Read()
{
	FPlanetTerrainNextDiagSettings Out;
	Out.bEnable = CVarDiagEnable.GetValueOnGameThread();
	Out.Mode = static_cast<EPlanetTerrainNextDiagMode>(FMath::Clamp(CVarDiagMode.GetValueOnGameThread(), 0, 1));
	Out.Level = FMath::Clamp(CVarDiagLevel.GetValueOnGameThread(), 0, 24);
	Out.Face = FMath::Clamp(CVarDiagFace.GetValueOnGameThread(), 0, 5);
	Out.bHeight = CVarDiagHeight.GetValueOnGameThread();
	Out.bTwoSided = CVarDiagTwoSided.GetValueOnGameThread();
	Out.bForceFreshSection = CVarDiagForceFreshSection.GetValueOnGameThread();
	return Out;
}

uint32 FPlanetTerrainNextDiagSettings::GetRebuildKey() const
{
	uint32 H = GetTypeHash(bEnable);
	H = HashCombine(H, GetTypeHash(static_cast<int32>(Mode)));
	H = HashCombine(H, GetTypeHash(Level));
	H = HashCombine(H, GetTypeHash(Face));
	H = HashCombine(H, GetTypeHash(bHeight));
	H = HashCombine(H, GetTypeHash(bTwoSided));
	H = HashCombine(H, GetTypeHash(bForceFreshSection));
	return H;
}

namespace PlanetTerrainNextDiag
{
UMaterialInterface* GetOrCreateMaterial(bool bTwoSided)
{
	const TCHAR* Path = bTwoSided ? DiagMaterialPathTwoSided : DiagMaterialPathOneSided;
	if (UMaterialInterface* Existing = Cast<UMaterialInterface>(FSoftObjectPath(Path).TryLoad()))
	{
		return Existing;
	}
#if WITH_EDITOR
	if (GIsEditor)
	{
		const FString PackagePath = bTwoSided
			? TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextDiag_v1")
			: TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextDiag_OneSided_v1");
		const FName AssetName = bTwoSided ? TEXT("M_PlanetTerrainNextDiag_v1") : TEXT("M_PlanetTerrainNextDiag_OneSided_v1");
		if (UMaterialInterface* Created = CreateDiagMaterial(PackagePath, AssetName, bTwoSided))
		{
			return Created;
		}
	}
#endif
	UE_LOG(LogPlanetTerrainNext, Warning, TEXT("Diag material missing at %s — using DefaultMaterial"), Path);
	return UMaterial::GetDefaultMaterial(MD_Surface);
}

FPlanetTerrainNextPatchValidation ValidatePatch(const FPlanetTerrainNextPatchData& Patch, bool bValidateSkirts)
{
	using namespace PlanetTerrainNextPatch;
	FPlanetTerrainNextPatchValidation Out;
	const TArray<int32>& Idx = Indices();
	const int32 VertCount = Patch.VerticesCm.Num();
	Out.TriangleCount = Idx.Num() / 3;
	Out.GridTriangleCount = GridN * GridN * 2;
	const int32 MaxTris = bValidateSkirts ? Out.TriangleCount : Out.GridTriangleCount;

	for (int32 V = 0; V < VertCount; ++V)
	{
		const FVector& P = Patch.VerticesCm[V];
		if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z))
		{
			++Out.NonFiniteVertexCount;
		}
	}

	auto WorldM = [&](int32 Index) -> FVector3d
	{
		return Patch.OriginM + FVector3d(Patch.VerticesCm[Index]) / PlanetTerrainNext::CmPerMeter;
	};

	for (int32 Tri = 0; Tri < MaxTris; ++Tri)
	{
		const int32 Base = Tri * 3;
		const int32 IA = Idx[Base + 0];
		const int32 IB = Idx[Base + 1];
		const int32 IC = Idx[Base + 2];
		if (IA < 0 || IB < 0 || IC < 0 || IA >= VertCount || IB >= VertCount || IC >= VertCount)
		{
			++Out.InvalidIndexCount;
			continue;
		}
		const FVector3d A = WorldM(IA);
		const FVector3d B = WorldM(IB);
		const FVector3d C = WorldM(IC);
		const FVector3d Cross = FVector3d::CrossProduct(B - A, C - A);
		const double Area2 = Cross.Length();
		if (Area2 < 1.0e-12)
		{
			++Out.DegenerateTriangleCount;
			continue;
		}
		if (Tri < Out.GridTriangleCount)
		{
			const FVector3d Mid = (A + B + C) / 3.0;
			const FVector3d Radial = Mid.GetSafeNormal();
			// UE front = -Cross for this index winding (matches R2 / skirt tests).
			const FVector3d Front = (-Cross).GetSafeNormal();
			if (FVector3d::DotProduct(Front, Radial) < 0.15)
			{
				++Out.InwardFacingGridCount;
			}
		}
	}

	Out.bOk = Out.InvalidIndexCount == 0
		&& Out.NonFiniteVertexCount == 0
		&& Out.DegenerateTriangleCount == 0
		&& Out.InwardFacingGridCount == 0
		&& VertCount == VertexCount;
	return Out;
}

void CollectNodes(const FPlanetTerrainNextDiagSettings& Settings, TArray<FPlanetTerrainNextNodeId>& OutNodes)
{
	OutNodes.Reset();
	const uint8 Level = static_cast<uint8>(FMath::Clamp(Settings.Level, 0, 24));
	const uint32 Edge = 1u << Level;
	if (Settings.Mode == EPlanetTerrainNextDiagMode::SinglePatch)
	{
		const uint32 Mid = Edge > 1 ? (Edge / 2) : 0;
		OutNodes.Add(FPlanetTerrainNextNodeId(static_cast<uint8>(Settings.Face), Level, Mid, Mid));
		return;
	}
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (uint32 Y = 0; Y < Edge; ++Y)
		{
			for (uint32 X = 0; X < Edge; ++X)
			{
				OutNodes.Add(FPlanetTerrainNextNodeId(static_cast<uint8>(Face), Level, X, Y));
			}
		}
	}
}
}
