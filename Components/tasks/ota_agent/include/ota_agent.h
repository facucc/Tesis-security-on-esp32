#ifndef OTA_AGENT_H
#define OTA_AGENT_H

#include "queue_handler.h"

void otaAgenteTask(void *parameters);

/**
 * @brief OTA Agent states.
 *
 * The current state of the OTA Task (OTA Agent).
 */
typedef enum OtaState
{
    OtaAgentStateNoTransition = -1,
    OtaAgentStateInit = 0,
    OtaAgentStateReady,
    OtaAgentStateRequestingJob,
    OtaAgentStateWaitingForJob,
    OtaAgentStateCreatingFile,
    OtaAgentStateRequestingFileBlock,
    OtaAgentStateWaitingForFileBlock,
    OtaAgentStateClosingFile,
    OtaAgentStateSuspended,
    OtaAgentStateShuttingDown,
    OtaAgentStateStopped,
    OtaAgentStateAll
} OtaState_t;


#endif 