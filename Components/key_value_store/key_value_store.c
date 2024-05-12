#include "key_value_store.h"

static const char * TAG = "NVS";
// Helper function that loads a value from NVS. 
// It returns NULL when the value doesn't exist.
char * nvs_load_value_if_exist(nvs_handle handle, const char* key, size_t *value_size)
{
    // Try to get the size of the item
    if(nvs_get_str(handle, key, NULL, value_size) != ESP_OK){
        ESP_LOGE(TAG, "Failed to get size of key: %s", key);
        return NULL;
    }

    char* value = malloc(*value_size);
    if(nvs_get_str(handle, key, value, value_size) != ESP_OK){
        ESP_LOGE(TAG, "Failed to load key: %s", key);
        return NULL;
    }

    return value;
}