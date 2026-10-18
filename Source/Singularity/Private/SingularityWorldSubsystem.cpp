#include "SingularityWorldSubsystem.h"
#include "GameFramework/GameModeBase.h"
#include "SpaceFlyPawn.h"
#include "UniverseActor.h"

bool USingularityWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USingularityWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (AGameModeBase* GameMode = InWorld.GetAuthGameMode())
	{
		GameMode->DefaultPawnClass = ASpaceFlyPawn::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AUniverseActor>(AUniverseActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
}
