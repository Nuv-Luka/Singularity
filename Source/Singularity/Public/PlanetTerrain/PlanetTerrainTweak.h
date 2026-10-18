#pragma once

#include "CoreMinimal.h"

class UPlanetTerrainComponent;

/**
 * Live-tunable planet terrain / streaming knobs.
 * Edited by SPlanetTerrainTweakPanel (F6). Applied each tick to ActiveTerrain.
 */
struct FPlanetTerrainTweakSettings
{
	/** When true, slider changes apply. Component only re-pushes when Dirty or UI open. */
	bool bApplyLive = true;
	bool bDirty = true;

	// ----- Screen-space LOD (peak post-repair defaults) -----
	bool bUseScreenSpaceLod = true;
	float TargetPixelError = 2.0f;
	float PeripheralPixelError = 6.0f;
	float PrefetchPixelError = 12.0f;
	float FoveaInnerRadius = 0.35f;
	float FoveaOuterRadius = 0.90f;
	float GeometricErrorAmpFrac = 0.35f;
	float GeometricErrorSizeFrac = 0.18f;
	float LodDistanceFactor = 3.2f;
	float MergeHysteresis = 1.35f;
	float CullAngularMarginDeg = 6.88f; // ~0.12 rad
	float FrustumMinDot = -0.15f;
	float FrustumPrefetchMinDot = -0.45f;
	float HorizonReliefMarginKm = 8.0f;
	float ActivateDistanceRadii = 12.0f; // × planet radius

	// ----- Depth / budget -----
	bool bUseAltitudeBands = true;
	int32 BandMaxLevel[5] = {6, 8, 10, 12, 12};
	int32 BandMinNeigh[5] = {1, 2, 3, 5, 6};
	int32 MaxLevelOverride = 12;
	int32 MinNeighborhoodOverride = 4;
	int32 MaxActivePatches = 192;
	int32 PatchResolution = 32;

	// ----- Mesh / workers / residency (rebuild baseline) -----
	float UploadBudgetMs = 5.0f;
	float MorphDurationSeconds = 0.5f;
	bool bGeomorph = true;
	int32 GpuMeshGraceFrames = 90;
	int32 MaxWorkers = 8;
	bool bForceCpu = false;
	bool bEnableCollision = true;
	float ShellScale = 0.990f;
	bool bForceShell = false;
	bool bPauseGeneration = false;
	int32 WorldOriginRebaseCooldownFrames = 180;

	// ----- Debug overlays -----
	bool bShowHud = true;
	bool bShowRequested = false;
	bool bShowCulled = false;
	bool bShowCoverage = false;
	bool bShowRejection = false;
	bool bShowMorph = false;
	bool bShowFoveation = false;
	bool bShowSeams = false;
	bool bShowFaces = false;

	void ResetToDefaults();
	FString DumpText() const;
	void ApplyTo(UPlanetTerrainComponent& Terrain) const;
};

struct FPlanetTerrainTweak
{
	static FPlanetTerrainTweakSettings& Settings();
	static void ToggleUI();
	static bool IsUIOpen();
	static void ApplyToActive();
};
