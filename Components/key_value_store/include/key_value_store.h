#include <string.h>

#include "assert.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stddef.h>


// Helper function that loads a value from NVS. 
// It returns NULL when the value doesn't exist.
char * nvs_load_value_if_exist(nvs_handle handle, const char* key, size_t *value_size);