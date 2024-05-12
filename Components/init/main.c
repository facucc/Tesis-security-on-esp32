// user headers
#include "wifi_config.h"
// system headers
#include "nvs_flash.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_log.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
//#include "backoff_algorithm.h"
//#include "core_mqtt.h"
#include <unistd.h>
/* POSIX includes. */
#include <unistd.h>

#include "aws_headers.h"

static const char *TAG = "INIT";

static void usr_prv_init_hw(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %"PRIu32" bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init(); 
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
}
void usr_start_app(void)
{
    
    usr_prv_init_hw();
    usr_wifi_init_sta();
    start_wifi();

    while (1)
    {
        printf("Hello world\n");
        vTaskDelay(5000/ portTICK_PERIOD_MS);
    }
}