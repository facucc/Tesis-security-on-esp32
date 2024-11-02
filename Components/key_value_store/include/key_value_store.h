#include <string.h>

#include "assert.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stddef.h>

#define AWS_NAMESPACE "aws"
#define GOOGLE_NAMESPACE "google"
/* AWS Connect Settings */
#define ONBOARDING_ENABLED "OnBoarding"
#define CERTIFICATE_NVS_KEY "Certificate"
#define CLAIM_CERTIFICATE_NVS_KEY "ClaimCert"
#define CLAIM_PRIVATE_KEY_NVS_KEY "ClaimPrivateKey"
#define PRIVATE_KEY_NVS_KEY "PrivateKey"
#define ROOT_CA_NVS_KEY "RootCA"
#define ENDPOINT_NVS_KEY "Endpoint"
#define THING_NAME_NVS_KEY "ThingName"

// Helper function that loads a value from NVS. 
// It returns NULL when the value doesn't exist.
char * load_value_from_nvs(nvs_handle handle, const char* key, size_t *value_size);
void load_value_to_nvs( const char * key, const char * value);