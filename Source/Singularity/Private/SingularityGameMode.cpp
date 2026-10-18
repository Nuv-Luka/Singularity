#include "SingularityGameMode.h"
#include "SpaceFlyPawn.h"

ASingularityGameMode::ASingularityGameMode()
{
	DefaultPawnClass = ASpaceFlyPawn::StaticClass();
}
