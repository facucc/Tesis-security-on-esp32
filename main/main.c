#include "init_app.h"
#include "mqtt_agent.h"
#include "ota_agent.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
/**
 * @brief Stack size required for Init task.
 */
#define INIT_TASK_STACK_SIZE ( 4000U )
/**
 * @brief Priority required for Init task.
 */
#define INIT_TASK_PRIORITY ( 2 )


#define STACK_SIZE_OTA 8000U

StackType_t xStackOTA_Agent[ (STACK_SIZE_OTA)*4 ];
StaticTask_t xTaskOTABuffer;

#define STACK_SIZE_MQTT 8000U

StackType_t xStackMQTT_Agent[ STACK_SIZE_MQTT *4 ];
StaticTask_t xTaskMQTTBuffer;

#define STACK_SIZE_INIT 1000U

StackType_t xStackInit[ STACK_SIZE_INIT ];
StaticTask_t xTaskINITBuffer;

void app_main()
{
  TaskHandle_t xHandle = NULL;
    
  xHandle = xTaskCreateStatic( (void *)usr_start_app,
                              "Init Task",
                              STACK_SIZE_INIT,
                              NULL,
                              INIT_TASK_PRIORITY,
                              xStackInit,
                              &xTaskINITBuffer );

  if( xHandle == NULL )
  {
      ESP_LOGE( "main", "Failed to create Init task:");
  }

  vTaskDelay( pdMS_TO_TICKS(5000) );
  xHandle = xTaskCreateStatic( (void *) mqttAgenteTask,
                              "mqtt agent Task",
                              STACK_SIZE_MQTT,
                              NULL,
                              3,
                              xStackMQTT_Agent,
                              &xTaskMQTTBuffer);

  if( xHandle == NULL )
  {
      ESP_LOGE( "main","Failed to create mqtt agent Task:" );
  }

  vTaskDelay( pdMS_TO_TICKS(5000) );
  xHandle = xTaskCreateStatic( (void *) otaAgenteTask,
                              "ota agent Task",
                              STACK_SIZE_OTA,
                              NULL,
                              3,
                              xStackOTA_Agent,
                              &xTaskOTABuffer);

  if( xHandle == NULL )
  {
      ESP_LOGE( "main","Failed to create otaAgenteTask:" );
  }
  
}