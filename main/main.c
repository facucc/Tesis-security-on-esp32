#include "init_app.h"
#include "mqtt_agent.h"

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

void app_main()
{
  BaseType_t xStatus = pdFAIL;
    
  xStatus = xTaskCreate( (void *)usr_start_app,
                              "Init Task",
                              INIT_TASK_STACK_SIZE,
                              NULL,
                              INIT_TASK_PRIORITY,
                              NULL );

  if( xStatus != pdPASS )
  {
      ESP_LOGE( "main", "Failed to create Init task:");
  }
  vTaskDelay( pdMS_TO_TICKS(5000) );
  xStatus = xTaskCreate( (void *) mqttAgenteTask,
                              "mqtt agent Task",
                              20000,
                              NULL,
                              3,
                              NULL );

  if( xStatus != pdPASS )
  {
      ESP_LOGE( "main","Failed to create mqtt agent Task:" );
  }

  while (true)
  {
    ESP_LOGI( "main","Tareas creadas" );
    vTaskDelay( pdMS_TO_TICKS(5000) );
  }
  
}