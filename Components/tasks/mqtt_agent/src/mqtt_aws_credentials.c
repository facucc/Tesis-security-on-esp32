#include "mqtt_aws_credentials.h"
#include "setup_config.h"
#include "key_value_store.h"
#include "mqtt_agent.h"
#include "mqtt_common.h"

extern AWSConnectSettings_t AWSConnectSettings;

static const char* TAG = "MQTT_AGENT";

/* Loads AWS IoT Core connection settings from non-volatile storage (NVS). */
void LoadAWSSettings(bool OnBoarding)
{
    size_t itemLength = 0;

    ESP_LOGI(TAG, "Opening AWS Namespace");

    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);

    AWSConnectSettings.rootCA   = LoadValueFromNVS(xHandle, ROOT_CA_NVS_KEY, &itemLength);
    AWSConnectSettings.endpoint = LoadValueFromNVS(xHandle, ENDPOINT_NVS_KEY, &itemLength);

    if (OnBoarding) {
        AWSConnectSettings.thingName   = GetMacAddress();
        AWSConnectSettings.certificate = LoadValueFromNVS(xHandle, CLAIM_CERTIFICATE_NVS_KEY, &itemLength);
        AWSConnectSettings.privateKey  = LoadValueFromNVS(xHandle, CLAIM_PRIVATE_KEY_NVS_KEY, &itemLength);
    } else {
        AWSConnectSettings.thingName   = LoadValueFromNVS(xHandle, THING_NAME_NVS_KEY, &itemLength);
        AWSConnectSettings.certificate = LoadValueFromNVS(xHandle, CERTIFICATE_NVS_KEY, &itemLength);
        AWSConnectSettings.privateKey  = LoadValueFromNVS(xHandle, PRIVATE_KEY_NVS_KEY, &itemLength);
    }
    nvs_close(xHandle);
}
#if defined(CONNECTION_TEST)
void LoadFailedTLSSettings(void)
{
    size_t itemLength = 0;
    ESP_LOGI(TAG, "Opening google Namespace");

    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(GOOGLE_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);

    TLSFailedSettings.rootCA = LoadValueFromNVS(xHandle, ROOT_CA_NVS_KEY, &itemLength);
    //TLSFailedSettings.certificate = LoadValueFromNVS(xHandle, CERTIFICATE_NVS_KEY, &itemLength);

    nvs_close(xHandle);
}
#endif

/*
 * Reloads the AWS IoT credentials from non-volatile storage (NVS)
 * in case a connection cannot be established with the new certificate.
 */
void ResetAWSCredentials(NetworkContext_t* pNetworkContext)
{
    size_t itemLength = 0;
    nvs_handle xHandle;
    ESP_LOGI(TAG, "Opening AWS Namespace");

    if (AWSConnectSettings.certificate != NULL) {
        free(AWSConnectSettings.certificate);
        AWSConnectSettings.certificate = NULL;
    }

    if (AWSConnectSettings.privateKey != NULL) {
        free(AWSConnectSettings.privateKey);
        AWSConnectSettings.privateKey = NULL;
    }
    
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);

    AWSConnectSettings.certificate    = LoadValueFromNVS(xHandle, CERTIFICATE_NVS_KEY, &itemLength);
    pNetworkContext->pcClientCert     = AWSConnectSettings.certificate;
    pNetworkContext->pcClientCertSize = strlen(AWSConnectSettings.certificate) + 1;

    AWSConnectSettings.privateKey    = LoadValueFromNVS(xHandle, PRIVATE_KEY_NVS_KEY, &itemLength);
    pNetworkContext->pcClientKey     = AWSConnectSettings.privateKey;
    pNetworkContext->pcClientKeySize = strlen(AWSConnectSettings.privateKey) + 1;

    nvs_close(xHandle);
}

/* Updates the AWS IoT Core connection settings with new credentials (private key and certificate). */
void UpdateAWSSettings(NetworkContext_t* pNetworkContext)
{
    if (AWSConnectSettings.certificate != NULL) {
        free(AWSConnectSettings.certificate);
        AWSConnectSettings.certificate = NULL;
    }

    if (AWSConnectSettings.privateKey != NULL) {
        free(AWSConnectSettings.privateKey);
        AWSConnectSettings.privateKey = NULL;
    }

    ESP_LOGI(TAG, "Updating aws settings");

    AWSConnectSettings.certificate = strndup(AWSConnectSettings.newCertificate, strlen(AWSConnectSettings.newCertificate));

    if (AWSConnectSettings.certificate == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for the certificate");
        assert(AWSConnectSettings.certificate != NULL);
    }
    pNetworkContext->pcClientCert     = AWSConnectSettings.certificate;
    pNetworkContext->pcClientCertSize = strlen(AWSConnectSettings.certificate) + 1;

    AWSConnectSettings.privateKey = strndup(AWSConnectSettings.newPrivateKey, strlen(AWSConnectSettings.newPrivateKey));

    if (AWSConnectSettings.privateKey == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for the private key");
        assert(AWSConnectSettings.privateKey != NULL);
    }
    pNetworkContext->pcClientKey     = AWSConnectSettings.privateKey;
    pNetworkContext->pcClientKeySize = strlen(AWSConnectSettings.privateKey) + 1;

    ESP_LOGI(TAG, "New Certificate:\n%s", pNetworkContext->pcClientCert);

    free(AWSConnectSettings.newCertificate);
    AWSConnectSettings.newCertificate = NULL;

    free(AWSConnectSettings.newPrivateKey);
    AWSConnectSettings.newPrivateKey = NULL;
}
