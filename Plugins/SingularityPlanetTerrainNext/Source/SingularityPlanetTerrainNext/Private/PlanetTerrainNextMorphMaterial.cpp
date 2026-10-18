#include "PlanetTerrainNextMorphMaterial.h"

#include "PlanetTerrainNextLog.h"
#include "Materials/Material.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace PlanetTerrainNextMorphMaterial
{
namespace
{
UMaterialInterface* LoadExisting()
{
	return Cast<UMaterialInterface>(FSoftObjectPath(Path).TryLoad());
}

#if WITH_EDITOR
UMaterialInterface* CreateAndSave()
{
	const FString PackagePath = TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch");
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogPlanetTerrainNext, Error, TEXT("Morph material: could not create package %s"), *PackagePath);
		return nullptr;
	}

	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
		UMaterial::StaticClass(),
		Package,
		TEXT("M_PlanetTerrainNextPatch"),
		RF_Public | RF_Standalone | RF_Transactional,
		nullptr,
		GWarn));
	if (!Material)
	{
		UE_LOG(LogPlanetTerrainNext, Error, TEXT("Morph material: FactoryCreateNew failed"));
		return nullptr;
	}

	Material->BlendMode = BLEND_Opaque;
	Material->SetShadingModel(MSM_DefaultLit);
	// ProceduralMesh reuses the local-vertex path; StaticMesh usage covers it in 5.8.
	Material->SetMaterialUsage(MATUSAGE_StaticMesh);

	auto* Morph = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -600, 0));
	Morph->ParameterName = TEXT("Morph");
	Morph->DefaultValue = 0.0f;

	auto* Color = Cast<UMaterialExpressionVectorParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -600, 200));
	Color->ParameterName = TEXT("Color");
	Color->DefaultValue = FLinearColor(0.55f, 0.53f, 0.50f, 1.0f);

	auto* UV1 = Cast<UMaterialExpressionTextureCoordinate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -600, -200));
	UV1->CoordinateIndex = 1;

	auto* UV2 = Cast<UMaterialExpressionTextureCoordinate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -600, -80));
	UV2->CoordinateIndex = 2;

	auto* MaskUV2X = Cast<UMaterialExpressionComponentMask>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -400, -80));
	MaskUV2X->R = true;
	MaskUV2X->G = false;
	MaskUV2X->B = false;
	MaskUV2X->A = false;
	MaskUV2X->Input.Connect(0, UV2);

	auto* Append = Cast<UMaterialExpressionAppendVector>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -200, -160));
	Append->A.Connect(0, UV1);
	Append->B.Connect(0, MaskUV2X);

	auto* Scale = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), 0, -120));
	Scale->A.Connect(0, Append);
	Scale->B.Connect(0, Morph);

	auto* ToWorld = Cast<UMaterialExpressionTransform>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTransform::StaticClass(), 220, -120));
	ToWorld->TransformSourceType = TRANSFORMSOURCE_Local;
	ToWorld->TransformType = TRANSFORM_World;
	ToWorld->Input.Connect(0, Scale);

	UMaterialEditingLibrary::ConnectMaterialProperty(Color, TEXT("RGB"), MP_BaseColor);
	UMaterialEditingLibrary::ConnectMaterialProperty(ToWorld, TEXT(""), MP_WorldPositionOffset);
	UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
	UMaterialEditingLibrary::RecompileMaterial(Material);

	FAssetRegistryModule::AssetCreated(Material);
	Package->MarkPackageDirty();

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Package, Material, *PackageFilename, SaveArgs))
	{
		UE_LOG(LogPlanetTerrainNext, Warning,
			TEXT("Morph material created in memory but save to %s failed. PIE still works this session."), *PackageFilename);
	}
	else
	{
		UE_LOG(LogPlanetTerrainNext, Log, TEXT("Created morph material at %s"), *PackageFilename);
	}
	return Material;
}
#endif
} // namespace

UMaterialInterface* GetOrCreate()
{
	if (UMaterialInterface* Existing = LoadExisting())
	{
		return Existing;
	}
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (UMaterialInterface* Created = CreateAndSave())
		{
			return Created;
		}
	}
#endif
	UE_LOG(LogPlanetTerrainNext, Warning,
		TEXT("Morph material missing at %s — falling back to BasicShapeMaterial (positions stay fine; Morph param ignored)."),
		Path);
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}
}
