#include "PlanetTerrain/PlanetTerrainDebug.h"
#include "PlanetTerrain/PlanetTerrainComponent.h"
#include "PlanetTerrain/PlanetTerrainScheduler.h"
#include "Pug/PugCore.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

TWeakObjectPtr<UPlanetTerrainComponent> FPlanetTerrainDebug::ActiveTerrain;
bool FPlanetTerrainDebug::bFreezeLod = false;
bool FPlanetTerrainDebug::bForceCpu = false; // GpuFloat default for streaming speed
bool FPlanetTerrainDebug::bShowFaces = false;
bool FPlanetTerrainDebug::bShowSeams = false;
bool FPlanetTerrainDebug::bShowCollision = false;
bool FPlanetTerrainDebug::bGeomorph = true;
bool FPlanetTerrainDebug::bForceShellVisible = false;
bool FPlanetTerrainDebug::bShowRequestedTiles = false;
bool FPlanetTerrainDebug::bShowCulledTiles = false;
bool FPlanetTerrainDebug::bShowCoverage = false;
bool FPlanetTerrainDebug::bShowRejectionReason = false;
bool FPlanetTerrainDebug::bShowMorphAlpha = false;
bool FPlanetTerrainDebug::bShowFoveation = false;
bool FPlanetTerrainDebug::bPauseGeneration = false;

static void TerrainToast(const TCHAR* Msg)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 3.f, FColor::Cyan, Msg);
	}
}

static FAutoConsoleCommand CCmdTerrainFreeze(
	TEXT("singularity.Terrain.FreezeLod"),
	TEXT("Toggle terrain LOD freeze"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bFreezeLod = !FPlanetTerrainDebug::bFreezeLod;
		if (UPlanetTerrainComponent* T = FPlanetTerrainDebug::ActiveTerrain.Get())
		{
			T->SetLodFrozen(FPlanetTerrainDebug::bFreezeLod);
		}
	}));

static FAutoConsoleCommand CCmdTerrainFaces(
	TEXT("singularity.Terrain.ShowFaces"),
	TEXT("Toggle cube-face debug colors + boundary wireframes"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowFaces = !FPlanetTerrainDebug::bShowFaces;
		if (UPlanetTerrainComponent* T = FPlanetTerrainDebug::ActiveTerrain.Get())
		{
			T->SetDebugDraw(FPlanetTerrainDebug::bShowSeams, FPlanetTerrainDebug::bShowFaces, FPlanetTerrainDebug::bShowCollision);
		}
	}));

static FAutoConsoleCommand CCmdTerrainSeams(
	TEXT("singularity.Terrain.ShowSeams"),
	TEXT("Toggle tile boundary wireframes + edge height-mismatch heatmap"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowSeams = !FPlanetTerrainDebug::bShowSeams;
		if (UPlanetTerrainComponent* T = FPlanetTerrainDebug::ActiveTerrain.Get())
		{
			T->SetDebugDraw(FPlanetTerrainDebug::bShowSeams, FPlanetTerrainDebug::bShowFaces, FPlanetTerrainDebug::bShowCollision);
			T->bShowDebugHud = true;
		}
		TerrainToast(FPlanetTerrainDebug::bShowSeams ? TEXT("Terrain seams: ON") : TEXT("Terrain seams: OFF"));
	}));

static FAutoConsoleCommand CCmdTerrainRegen(
	TEXT("singularity.Terrain.RegenSeed"),
	TEXT("Regenerate active moon with a new random terrain seed"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (UPlanetTerrainComponent* T = FPlanetTerrainDebug::ActiveTerrain.Get())
		{
			const uint64 NewSeed = PugSplitMix64(T->GetTerrainSeed() ^ 0xA5A5A5A5ull);
			T->RegenerateWithSeed(NewSeed);
		}
	}));

static FAutoConsoleCommand CCmdTerrainForceCpu(
	TEXT("singularity.Terrain.ForceCpu"),
	TEXT("Toggle Phase 7 bake: on=double CPU, off=GpuFloat workers (+ GPU compute on land pad)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bForceCpu = !FPlanetTerrainDebug::bForceCpu;
		if (UPlanetTerrainComponent* T = FPlanetTerrainDebug::ActiveTerrain.Get())
		{
			T->SetForceCpu(FPlanetTerrainDebug::bForceCpu);
		}
		TerrainToast(FPlanetTerrainDebug::bForceCpu
			? TEXT("Terrain bake: double CPU")
			: TEXT("Terrain bake: GPU-float / compute"));
	}));

static FAutoConsoleCommand CCmdForceShell(
	TEXT("singularity.Terrain.ForceShell"),
	TEXT("Force distant fallback shell always visible (coverage diagnostic)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bForceShellVisible = !FPlanetTerrainDebug::bForceShellVisible;
		TerrainToast(FPlanetTerrainDebug::bForceShellVisible
			? TEXT("ForceShell: ON") : TEXT("ForceShell: OFF"));
	}));

static FAutoConsoleCommand CCmdPauseGen(
	TEXT("singularity.Terrain.PauseGeneration"),
	TEXT("Stop launching new terrain bake jobs (shell/parents must remain)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bPauseGeneration = !FPlanetTerrainDebug::bPauseGeneration;
		FPlanetTerrainScheduler::SetGlobalPauseGeneration(FPlanetTerrainDebug::bPauseGeneration);
		TerrainToast(FPlanetTerrainDebug::bPauseGeneration
			? TEXT("PauseGeneration: ON") : TEXT("PauseGeneration: OFF"));
	}));

static FAutoConsoleCommand CCmdShowRequested(
	TEXT("singularity.Terrain.ShowRequestedTiles"),
	TEXT("Highlight tiles awaiting generation."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowRequestedTiles = !FPlanetTerrainDebug::bShowRequestedTiles;
		TerrainToast(FPlanetTerrainDebug::bShowRequestedTiles ? TEXT("ShowRequested: ON") : TEXT("ShowRequested: OFF"));
	}));

static FAutoConsoleCommand CCmdShowCulled(
	TEXT("singularity.Terrain.ShowCulledTiles"),
	TEXT("Highlight frustum/horizon/budget rejected nodes."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowCulledTiles = !FPlanetTerrainDebug::bShowCulledTiles;
		TerrainToast(FPlanetTerrainDebug::bShowCulledTiles ? TEXT("ShowCulled: ON") : TEXT("ShowCulled: OFF"));
	}));

static FAutoConsoleCommand CCmdShowCoverage(
	TEXT("singularity.Terrain.ShowCoverage"),
	TEXT("Green=resident draw leaf, Red=coverage hole."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowCoverage = !FPlanetTerrainDebug::bShowCoverage;
		TerrainToast(FPlanetTerrainDebug::bShowCoverage ? TEXT("ShowCoverage: ON") : TEXT("ShowCoverage: OFF"));
	}));

static FAutoConsoleCommand CCmdShowReject(
	TEXT("singularity.Terrain.ShowRejectionReason"),
	TEXT("Label nodes with frustum/horizon/budget rejection."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowRejectionReason = !FPlanetTerrainDebug::bShowRejectionReason;
		TerrainToast(FPlanetTerrainDebug::bShowRejectionReason ? TEXT("ShowRejection: ON") : TEXT("ShowRejection: OFF"));
	}));

static FAutoConsoleCommand CCmdShowMorph(
	TEXT("singularity.Terrain.ShowMorphAlpha"),
	TEXT("Overlay geomorph alpha (red=coarse/parent, green=fine)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowMorphAlpha = !FPlanetTerrainDebug::bShowMorphAlpha;
		TerrainToast(FPlanetTerrainDebug::bShowMorphAlpha ? TEXT("ShowMorph: ON") : TEXT("ShowMorph: OFF"));
	}));

static FAutoConsoleCommand CCmdShowFovea(
	TEXT("singularity.Terrain.ShowFoveation"),
	TEXT("Overlay continuous screen-radius foveation weight."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FPlanetTerrainDebug::bShowFoveation = !FPlanetTerrainDebug::bShowFoveation;
		TerrainToast(FPlanetTerrainDebug::bShowFoveation ? TEXT("ShowFoveation: ON") : TEXT("ShowFoveation: OFF"));
	}));

void FPlanetTerrainDebug::DrawHud(UCanvas* Canvas, UPlanetTerrainComponent* Terrain)
{
	if (!Canvas || !Terrain)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const FString Line = FString::Printf(
		TEXT("PlanetTerrain seed=%llu leaves=%d q=%d workers=%d bake=%.2fms"),
		Terrain->GetTerrainSeed(), Terrain->GetActiveLeaves(), Terrain->GetQueueLength(),
		Terrain->GetWorkerCount(), Terrain->GetLastBakeMs());
	Canvas->DrawText(Font, Line, 32.0f, 64.0f);
	const FString LineAlt = FString::Printf(
		TEXT("radial=%.1fkm  AGL=%.2fkm (%.0fm)  terrainH=%.0fm  freeze=%d pauseGen=%d forceShell=%d"),
		Terrain->GetDistanceFromCenterM() / 1000.0,
		Terrain->GetAltitudeAboveSurfaceM() / 1000.0,
		Terrain->GetAltitudeAboveSurfaceM(),
		Terrain->GetHeightUnderCamera(),
		bFreezeLod ? 1 : 0,
		bPauseGeneration ? 1 : 0,
		bForceShellVisible ? 1 : 0);
	Canvas->DrawText(Font, LineAlt, 32.0f, 80.0f);
	const FString Line2 = FString::Printf(
		TEXT("upload=%.2fms (x%d frame=%.2fms) landPad=%.1fms cache=%d/%d ForceCpu=%d seams=%d"),
		Terrain->GetLastUploadMs(), Terrain->GetUploadsThisFrame(), Terrain->GetUploadMsThisFrame(),
		Terrain->GetLastLandingPadMs(), Terrain->GetCacheHits(), Terrain->GetCacheMisses(),
		bForceCpu ? 1 : 0, bShowSeams ? 1 : 0);
	Canvas->DrawText(Font, Line2, 32.0f, 96.0f);
	const FString LineV2 = Terrain->GetStreamingStatsLine();
	if (!LineV2.IsEmpty())
	{
		Canvas->DrawText(Font, LineV2, 32.0f, 112.0f);
	}
}

void PlanetTerrainRegisterDebugCommands()
{
	// Commands registered via FAutoConsoleCommand statics.
}

void PlanetTerrainUnregisterDebugCommands()
{
}
