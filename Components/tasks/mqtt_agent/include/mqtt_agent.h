
/* CoreMQTT-Agent include. */
#include "core_mqtt_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"

void mqttAgenteTask( void * parameters);
void SubscribeCommandCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );
/**
 * @brief Defines the structure to use as the command callback context in this
 * demo.
 */
struct MQTTAgentCommandContext
{
    MQTTStatus_t xReturnStatus;
    TaskHandle_t xTaskToNotify;
    uint32_t ulNotificationValue;
    void * pxIncomingPublishCallback;
    void * pArgs;
};

typedef struct AWSConnectSettings { 
    char * certificate;
    char * privateKey;
    char * rootCA;
    char * endpoint;
    char * thingName;
} AWSConnectSettings_t;