#include "PlanetTerrainNextMorphMaterial.h"

#include "PlanetTerrainNextLog.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace PlanetTerrainNextMorphMaterial
{
namespace
{
UMaterialInterface* LoadExisting(const TCHAR* InPath)
{
	return Cast<UMaterialInterface>(FSoftObjectPath(InPath).TryLoad());
}

#if WITH_EDITOR
UMaterialInterface* CreateAndSave(const TCHAR* InPackagePath, const TCHAR* InAssetName, bool bStable)
{
	const FString PackagePath(InPackagePath);
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
		InAssetName,
		RF_Public | RF_Standalone | RF_Transactional,
		nullptr,
		GWarn));
	if (!Material)
	{
		UE_LOG(LogPlanetTerrainNext, Error, TEXT("Morph material: FactoryCreateNew failed"));
		return nullptr;
	}

	// Stable patches are genuinely opaque and WPO-free. The transition variant
	// retains masked dither and the legacy WPO graph for active fades/morphing.
	Material->BlendMode = bStable ? BLEND_Opaque : BLEND_Masked;
	Material->SetShadingModel(MSM_DefaultLit);
	Material->TwoSided = !bStable;
	Material->SetMaterialUsage(MATUSAGE_StaticMesh);

	auto* Morph = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 0));
	Morph->ParameterName = TEXT("Morph");
	Morph->DefaultValue = 0.0f;

	auto* MorphScale = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 80));
	MorphScale->ParameterName = TEXT("MorphScaleCm");
	MorphScale->DefaultValue = 1.0f;

	auto* Color = Cast<UMaterialExpressionVectorParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -700, 200));
	Color->ParameterName = TEXT("Color");
	Color->DefaultValue = FLinearColor(0.55f, 0.53f, 0.50f, 1.0f);

	auto* UV1 = Cast<UMaterialExpressionTextureCoordinate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -700, -200));
	UV1->CoordinateIndex = 1;

	auto* UV2 = Cast<UMaterialExpressionTextureCoordinate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -700, -80));
	UV2->CoordinateIndex = 2;

	auto* MaskUV2X = Cast<UMaterialExpressionComponentMask>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -480, -80));
	MaskUV2X->R = true;
	MaskUV2X->G = false;
	MaskUV2X->B = false;
	MaskUV2X->A = false;
	MaskUV2X->Input.Connect(0, UV2);

	auto* Append = Cast<UMaterialExpressionAppendVector>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -280, -160));
	Append->A.Connect(0, UV1);
	Append->B.Connect(0, MaskUV2X);

	auto* TimesMorph = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -80, -120));
	TimesMorph->A.Connect(0, Append);
	TimesMorph->B.Connect(0, Morph);

	auto* TimesScale = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), 120, -120));
	TimesScale->A.Connect(0, TimesMorph);
	TimesScale->B.Connect(0, MorphScale);

	auto* ToWorld = Cast<UMaterialExpressionTransform>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTransform::StaticClass(), 320, -120));
	ToWorld->TransformSourceType = TRANSFORMSOURCE_Local;
	ToWorld->TransformType = TRANSFORM_World;
	ToWorld->Input.Connect(0, TimesScale);

	auto* UV0 = Cast<UMaterialExpressionTextureCoordinate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -700, 400));
	UV0->CoordinateIndex = 0;

	auto* DetailUVScale = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 480));
	DetailUVScale->ParameterName = TEXT("DetailUVScale");
	DetailUVScale->DefaultValue = 1.0f;

	auto* DetailUVOffset = Cast<UMaterialExpressionVectorParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -700, 560));
	DetailUVOffset->ParameterName = TEXT("DetailUVOffset");
	DetailUVOffset->DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	auto* DetailBlend = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 640));
	DetailBlend->ParameterName = TEXT("DetailBlend");
	DetailBlend->DefaultValue = 0.0f;

	auto* ScaledUV = Cast<UMaterialExpressionMultiply>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -480, 440));
	ScaledUV->A.Connect(0, UV0);
	ScaledUV->B.Connect(0, DetailUVScale);

	auto* OffsetMask = Cast<UMaterialExpressionComponentMask>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -480, 560));
	OffsetMask->R = true;
	OffsetMask->G = true;
	OffsetMask->B = false;
	OffsetMask->A = false;
	OffsetMask->Input.Connect(0, DetailUVOffset);

	auto* FaceUV = Cast<UMaterialExpressionAdd>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAdd::StaticClass(), -280, 480));
	FaceUV->A.Connect(0, ScaledUV);
	FaceUV->B.Connect(0, OffsetMask);

	auto* DetailAlbedo = Cast<UMaterialExpressionTextureSampleParameter2D>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -80, 400));
	DetailAlbedo->ParameterName = TEXT("DetailAlbedo");
	DetailAlbedo->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	DetailAlbedo->Coordinates.Connect(0, FaceUV);

	auto* DetailNormal = Cast<UMaterialExpressionTextureSampleParameter2D>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -80, 560));
	DetailNormal->ParameterName = TEXT("DetailNormal");
	DetailNormal->SamplerType = SAMPLERTYPE_Normal;
	DetailNormal->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));
	DetailNormal->Coordinates.Connect(0, FaceUV);

	auto* BaseColor = Cast<UMaterialExpressionLinearInterpolate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), 160, 280));
	BaseColor->A.Connect(0, Color);
	BaseColor->B.Connect(0, DetailAlbedo);
	BaseColor->Alpha.Connect(0, DetailBlend);

	auto* FlatNormal = Cast<UMaterialExpressionConstant3Vector>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), 160, 480));
	FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f, 0.0f);

	auto* NormalLerp = Cast<UMaterialExpressionLinearInterpolate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), 320, 520));
	NormalLerp->A.Connect(0, FlatNormal);
	NormalLerp->B.Connect(0, DetailNormal);
	NormalLerp->Alpha.Connect(0, DetailBlend);

	// --- R10.2 complementary dither ---
	// EffectiveAlpha = lerp(1 - LodFade, LodFade, LodFadeRole)
	// OpacityMask = DitherTemporalAA(EffectiveAlpha)
	auto* LodFade = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 720));
	LodFade->ParameterName = TEXT("LodFade");
	LodFade->DefaultValue = 0.0f;

	auto* LodFadeRole = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -700, 800));
	LodFadeRole->ParameterName = TEXT("LodFadeRole");
	LodFadeRole->DefaultValue = 0.0f; // 0=parent/outgoing, 1=child/incoming

	auto* OneMinusFade = Cast<UMaterialExpressionOneMinus>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionOneMinus::StaticClass(), -480, 720));
	OneMinusFade->Input.Connect(0, LodFade);

	auto* EffectiveAlpha = Cast<UMaterialExpressionLinearInterpolate>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -280, 760));
	EffectiveAlpha->A.Connect(0, OneMinusFade);
	EffectiveAlpha->B.Connect(0, LodFade);
	EffectiveAlpha->Alpha.Connect(0, LodFadeRole);

	UMaterialExpression* OpacitySource = EffectiveAlpha;
	if (UMaterialFunction* DitherFn = LoadObject<UMaterialFunction>(
			nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA")))
	{
		auto* DitherCall = Cast<UMaterialExpressionMaterialFunctionCall>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMaterialFunctionCall::StaticClass(), -80, 760));
		if (DitherCall && DitherCall->SetMaterialFunction(DitherFn))
		{
			DitherCall->UpdateFromFunctionResource();
			if (DitherCall->FunctionInputs.Num() > 0)
			{
				DitherCall->FunctionInputs[0].Input.Connect(0, EffectiveAlpha);
				OpacitySource = DitherCall;
			}
		}
	}
	else
	{
		UE_LOG(LogPlanetTerrainNext, Warning, TEXT("DitherTemporalAA MF missing — OpacityMask uses raw EffectiveAlpha"));
	}

	UMaterialEditingLibrary::ConnectMaterialProperty(BaseColor, TEXT(""), MP_BaseColor);
	UMaterialEditingLibrary::ConnectMaterialProperty(NormalLerp, TEXT(""), MP_Normal);
	if (!bStable)
	{
		UMaterialEditingLibrary::ConnectMaterialProperty(ToWorld, TEXT(""), MP_WorldPositionOffset);
		UMaterialEditingLibrary::ConnectMaterialProperty(OpacitySource, TEXT(""), MP_OpacityMask);
	}
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
			TEXT("Terrain material v%d created in memory but save to %s failed. PIE still works this session."),
			MaterialFormatVersion, *PackageFilename);
	}
	else
	{
		UE_LOG(LogPlanetTerrainNext, Log, TEXT("Created terrain material v%d at %s"), MaterialFormatVersion, *PackageFilename);
	}
	return Material;
}
#endif
} // namespace

UMaterialInterface* GetOrCreateTransition()
{
	if (UMaterialInterface* Existing = LoadExisting(TransitionPath))
	{
		return Existing;
	}
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (UMaterialInterface* Created = CreateAndSave(
			TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v5"),
			TEXT("M_PlanetTerrainNextPatch_v5"), false))
		{
			return Created;
		}
	}
#endif
	UE_LOG(LogPlanetTerrainNext, Warning,
		TEXT("Transition material missing at %s — falling back to BasicShapeMaterial."),
		TransitionPath);
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UMaterialInterface* GetOrCreateStable()
{
	if (UMaterialInterface* Existing = LoadExisting(StablePath))
	{
		return Existing;
	}
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (UMaterialInterface* Created = CreateAndSave(
			TEXT("/SingularityPlanetTerrainNext/Materials/M_PlanetTerrainNextPatch_v6_Stable"),
			TEXT("M_PlanetTerrainNextPatch_v6_Stable"), true))
		{
			return Created;
		}
	}
#endif
	UE_LOG(LogPlanetTerrainNext, Warning,
		TEXT("Stable terrain material missing at %s — using transition material."), StablePath);
	return GetOrCreateTransition();
}

UMaterialInterface* GetOrCreate()
{
	return GetOrCreateTransition();
}
}
