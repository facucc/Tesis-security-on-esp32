#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/* Tasks implementations */
#include "mqtt_agent.h"
#include "ota_agent.h"
#include "cert_renew_agent.h"
#include "application.h"

#define WORD 4

/* MQTT Agent Task */
#if defined(CONFIG_MQTT_AGENT_ENABLE)
    StackType_t xStackMQTT_Agent[CONFIG_MQTT_AGENT_STACK_SIZE * WORD];
    StaticTask_t xTaskMQTTBuffer;
#endif

/* Application Task */
#if defined(CONFIG_APP_ENABLE)
    StackType_t xStackApp[CONFIG_APP_STACK_SIZE * WORD];
    StaticTask_t xTaskAppBuffer;
#endif

/* Certificate Renew Agent Task */
#if defined(CONFIG_CERT_RENEWAL_AGENT_ENABLE)
    StackType_t xStackRenew_Agent[CONFIG_CERT_RENEWAL_AGENT_STACK_SIZE * WORD];
    StaticTask_t xTaskRenewBuffer;
#endif

/* OTA Agent Task */
#if defined(CONFIG_OTA_AGENT_ENABLE)
    StackType_t xStackOTA_Agent[CONFIG_OTA_AGENT_STACK_SIZE * WORD];
    StaticTask_t xTaskOTABuffer;
#endif

static BaseType_t prvWaitForSync ( uint32_t* pulNotifiedValue );

/* cppcheck-suppress unusedFunction */
void app_main()
{
    uint32_t ulNotificationValue = 0;
    TaskHandle_t xHandle = NULL;

    /* MQTT Agent Task */
#if defined(CONFIG_MQTT_AGENT_ENABLE)
    xHandle = xTaskCreateStatic ( ( void* ) mqttAgentTask,
                                  CONFIG_MQTT_AGENT_TASK_NAME,
                                  CONFIG_MQTT_AGENT_STACK_SIZE,
                                  NULL,
                                  tskIDLE_PRIORITY,
                                  xStackMQTT_Agent,
                                  &xTaskMQTTBuffer );

    if ( xHandle == NULL ) {
        ESP_LOGE ( "main", "Failed to create mqtt agent Task" );
        assert ( xHandle != NULL );
    }
#endif

    prvWaitForSync ( &ulNotificationValue );

    /* Application Task */
#if defined(CONFIG_APP_ENABLE)
    xHandle = xTaskCreateStatic ( ( void* ) applicationTask,
                                  CONFIG_APP_TASK_NAME,
                                  CONFIG_APP_STACK_SIZE,
                                  NULL,
                                  tskIDLE_PRIORITY,
                                  xStackApp,
                                  &xTaskAppBuffer );

    if ( xHandle == NULL ) {
        ESP_LOGE ( "main", "Failed to create application Task" );
        assert ( xHandle != NULL );
    }
#endif

    /* OTA Agent Task */
#if defined(CONFIG_OTA_AGENT_ENABLE)
    xHandle = xTaskCreateStatic ( ( void* ) otaAgentTask,
                                  CONFIG_OTA_AGENT_TASK_NAME,
                                  CONFIG_OTA_AGENT_STACK_SIZE,
                                  NULL,
                                  tskIDLE_PRIORITY,
                                  xStackOTA_Agent,
                                  &xTaskOTABuffer );

    if ( xHandle == NULL ) {
        ESP_LOGE ( "main", "Failed to create ota agent Task" );
        assert ( xHandle != NULL );
    }
#endif

    /* Certificate Renewal Agent Task */
#if defined(CONFIG_CERT_RENEWAL_AGENT_ENABLE)
    xHandle = xTaskCreateStatic ( ( void* ) renewAgentTask,
                                  CONFIG_CERT_RENEWAL_AGENT_TASK_NAME,
                                  CONFIG_CERT_RENEWAL_AGENT_STACK_SIZE,
                                  NULL,
                                  tskIDLE_PRIORITY,
                                  xStackRenew_Agent,
                                  &xTaskRenewBuffer );

    if ( xHandle == NULL ) {
        ESP_LOGE ( "main", "Failed to create renew agent Task" );
        assert ( xHandle != NULL );
    }
#endif
}

/*-----------------------------------------------------------*/
static BaseType_t prvWaitForSync ( uint32_t* pulNotifiedValue )
{
    BaseType_t xReturn;

    /* Wait for this task to get notified, passing out the value it gets notified with. */
    xReturn = xTaskNotifyWait ( 0, 0, pulNotifiedValue, portMAX_DELAY );

    return xReturn;
}
