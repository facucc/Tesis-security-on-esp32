#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mqtt_agent.h"
#include "ota_agent.h"


#define STACK_SIZE_OTA 6400U
#define WORD 4

StackType_t xStackOTA_Agent[ STACK_SIZE_OTA * WORD ];
StaticTask_t xTaskOTABuffer;

#define STACK_SIZE_MQTT 4000U

StackType_t xStackMQTT_Agent[ STACK_SIZE_MQTT * WORD ];
StaticTask_t xTaskMQTTBuffer;

void app_main()
{
  TaskHandle_t xHandle = NULL;

  xHandle = xTaskCreateStatic( (void *) mqttAgenteTask,
                              "mqtt agent Task",
                              STACK_SIZE_MQTT,
                              NULL,
                              tskIDLE_PRIORITY,
                              xStackMQTT_Agent,
                              &xTaskMQTTBuffer);

  if( xHandle == NULL )
  {
      ESP_LOGE( "main","Failed to create mqtt agent Task:" );
  }

  vTaskDelay( pdMS_TO_TICKS(20000) );
  xHandle = xTaskCreateStatic( (void *) otaAgenteTask,
                              "ota agent Task",
                              STACK_SIZE_OTA,
                              NULL,
                              tskIDLE_PRIORITY,
                              xStackOTA_Agent,
                              &xTaskOTABuffer);

  if( xHandle == NULL )
  {
      ESP_LOGE( "main","Failed to create otaAgenteTask:" );
  }
  
}