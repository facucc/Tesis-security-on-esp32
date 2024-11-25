#include "key_value_store.h"

static const char* TAG = "NVS";

/* Helper function that loads a value from NVS.
* It returns NULL when the value doesn't exist.
*/

char* LoadValueFromNVS(nvs_handle handle, const char* key, size_t* value_size)
{
    if (nvs_get_str(handle, key, NULL, value_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get size of key: %s", key);
        return NULL;
    }
    char* value = (char*)malloc(*value_size);

    if (nvs_get_str(handle, key, value, value_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load key: %s", key);
        return NULL;
    }

    return value;
}

/* Helper function that loads a value to NVS. */

void LoadValueToNVS(const char* key, const char* value)
{
    esp_err_t err;
    nvs_handle xHandle;

    if (ESP_OK == nvs_open(AWS_NAMESPACE, NVS_READWRITE, &xHandle)) {
        err = nvs_set_str(xHandle, key, value);

        if (ESP_OK == err) {
            nvs_commit(xHandle);
        } else {
            ESP_ERROR_CHECK(err);
        }
        nvs_close(xHandle);
    } else {
        ESP_LOGE(TAG, "NVS Open Failed");
    }
}
