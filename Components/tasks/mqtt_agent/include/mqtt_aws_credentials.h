#include <stdbool.h>
/* Transport interface include. */
#include "network_transport.h"

void LoadAWSSettings(bool OnBoarding);
#if defined(CONNECTION_TEST)
    void LoadFailedTLSSettings(void);
#endif
void ResetAWSCredentials(NetworkContext_t* pNetworkContext);
void UpdateAWSSettings(NetworkContext_t* pNetworkContext);