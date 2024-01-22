
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

#define ESP_MAXIMUM_RETRY  5

#define WIFI_CONNECTED_BIT BIT0 
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t wifi_event_group;

void usr_wifi_init_sta(char* wifi_ssid, char* wifi_password);