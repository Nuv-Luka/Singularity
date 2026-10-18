#include "SingularityAtmosphereShaders.h"

IMPLEMENT_GLOBAL_SHADER(FSingularityAtmosphereIdentityCS, "/SingularityAtmosphere/Private/IdentityCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSingularityAtmosphereTransmittanceCS, "/SingularityAtmosphere/Private/TransmittanceCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSingularityAtmosphereMultiScatteringCS, "/SingularityAtmosphere/Private/MultiScatteringCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSingularityAtmosphereSingleScatterCS, "/SingularityAtmosphere/Private/AtmosphereSingleScatterCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSingularityAtmosphereSkyViewCS, "/SingularityAtmosphere/Private/SkyViewCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSingularityAtmosphereAerialPerspectiveCS, "/SingularityAtmosphere/Private/AerialPerspectiveCS.usf", "MainCS", SF_Compute);
