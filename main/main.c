#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mqtt_agent.h"
#include "ota_agent.h"
#include "cert_renew_agent.h"
#include "application.h"
#include "gen_csr.h"

#define WORD 4


#define STACK_SIZE_MQTT 8600U
#define MQTT_AGENT_TASK_NAME "mqtt_agent"

StackType_t xStackMQTT_Agent[ STACK_SIZE_MQTT * WORD ];
StaticTask_t xTaskMQTTBuffer;

#define STACK_SIZE_APP 3000U
#define APP_TASK_NAME "app"

StackType_t xStackApp[ STACK_SIZE_APP * WORD ];
StaticTask_t xTaskAppBuffer;

#define STACK_SIZE_RENEW 9000U
#define RENEW_AGENT_TASK_NAME "renew_agent"

StackType_t xStackRenew_Agent[ STACK_SIZE_RENEW * WORD ];
StaticTask_t xTaskRenewBuffer;

#define STACK_SIZE_OTA 6000U
#define OTA_AGENT_TASK_NAME "ota_agent"

StackType_t xStackOTA_Agent[ STACK_SIZE_OTA * WORD ];
StaticTask_t xTaskOTABuffer;


#define STACK_SIZE_OTA 8000U
#define OTA_AGENT_TASK_NAME "ota_agent"

StackType_t xStackOTA_Agent[ STACK_SIZE_OTA * WORD ];
StaticTask_t xTaskOTABuffer;

static BaseType_t prvWaitForSync( uint32_t * pulNotifiedValue );

/* cppcheck-suppress unusedFunction */
void app_main()
{
  uint32_t ulNotificationValue = 0;
  TaskHandle_t xHandle = NULL;

  xHandle = xTaskCreateStatic( (void *) mqttAgentTask,
                              MQTT_AGENT_TASK_NAME,
                              STACK_SIZE_MQTT,
                              NULL,
                              tskIDLE_PRIORITY,
                              xStackMQTT_Agent,
                              &xTaskMQTTBuffer);

  if( xHandle == NULL )
  {
      ESP_LOGE( "main","Failed to create mqtt agent Task" );
  }

  prvWaitForSync(&ulNotificationValue);

  xHandle = xTaskCreateStatic( (void *) applicationTask,
                              APP_TASK_NAME,
                              STACK_SIZE_APP,
                              NULL,
                              tskIDLE_PRIORITY,
                              xStackApp,
                              &xTaskAppBuffer);

  if( xHandle == NULL )
  {
      ESP_LOGE( "main","Failed to create application Task" );
  }

  xHandle = xTaskCreateStatic( (void *) otaAgentTask,
                              OTA_AGENT_TASK_NAME,
                              STACK_SIZE_OTA,
                              NULL,
                              tskIDLE_PRIORITY,
                              xStackOTA_Agent,
                              &xTaskOTABuffer);

  if( xHandle == NULL )
  {
      ESP_LOGE( "main","Failed to create otaAgenteTask" );
  }

  xHandle = xTaskCreateStatic( (void *) renewAgentTask,
                              RENEW_AGENT_TASK_NAME,
                              STACK_SIZE_RENEW,
                              NULL,
                              tskIDLE_PRIORITY,
                              xStackRenew_Agent,
                              &xTaskRenewBuffer);

  if( xHandle == NULL )
  {
    ESP_LOGE( "main","Failed to create renew agent Task" );
  }

}

/*-----------------------------------------------------------*/
static BaseType_t prvWaitForSync( uint32_t * pulNotifiedValue )
{
    BaseType_t xReturn;

    /* Wait for this task to get notified, passing out the value it gets
     * notified with. */
    xReturn = xTaskNotifyWait( 0, 0, pulNotifiedValue, portMAX_DELAY);
    
    return xReturn;
}