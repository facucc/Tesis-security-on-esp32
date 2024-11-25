#include "esp_ota_ops.h"

/* CoreMQTT-Agent APIS for running MQTT in a multithreaded environment. */
#include "freertos_agent_message.h"
#include "freertos_command_pool.h"

/* CoreMQTT-Agent include. */
#include "core_mqtt.h"
#include "core_mqtt_agent.h"

#include "mqtt_agent.h"
#include "mqtt_common.h"
#include "mqtt_onboarding.h"
#include "queue_handler.h"

#include "cert_renew_agent.h"

#include "mqtt_aws_credentials.h"

/* Includes helpers for managing MQTT subscriptions. */
#include "mqtt_subscription_manager.h"

/*Include backoff algorithm header for retry logic.*/
#include "backoff_algorithm.h"

/* Transport interface include. */
#include "network_transport.h"

#include "setup_config.h"

#include "key_value_store.h"

#include "mqtt_connection.h"

#if defined(CONFIG_CONNECTION_TEST)
    #define CONNECTION_TEST
#endif

MQTTAgentContext_t globalMqttAgentContext = {0};
AWSConnectSettings_t AWSConnectSettings   = {0};

/* Only Debug to detect stack size */
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    static UBaseType_t uxHighWaterMark;
#endif

static const char* TAG = "MQTT_AGENT";

static void prvMQTTAgentProcessing(void);
static void prvCheckFirmware(void);
static void prvPrintRunningPartition(void);
static void prvNotifyMainTask(void);


void mqttAgentTask(void* parameters)
{
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
#endif

    NetworkContext_t xNetworkContext = {0};
    TransportInterface_t xTransport = {0};

    initHardware();
    prvPrintRunningPartition();

    if (IsOnBoardingEnabled()) {
        StartOnboarding(&xNetworkContext, &xTransport);
    } else {
        ConnectToAWS(&xNetworkContext, &xTransport);
    }

    SubscribeToNextJobTopic();
    prvCheckFirmware();
    prvNotifyMainTask();
    prvMQTTAgentProcessing();

    while (true) {
        vTaskDelay(DELAY_TIME);
    }
}

static void prvMQTTAgentProcessing(void)
{
    MQTTStatus_t xMQTTStatus = MQTTSuccess;

    do {
        ESP_LOGI(TAG, "Starting to process commands");

        xMQTTStatus = MQTTAgent_CommandLoop(&globalMqttAgentContext);
        ESP_LOGE(TAG, "Finishing processing commands %s", MQTT_Status_strerror(xMQTTStatus));

        if (xMQTTStatus == MQTTSuccess) {
            HandleMQTTDisconnect(globalMqttAgentContext.mqttContext.transportInterface.pNetworkContext, &(globalMqttAgentContext.mqttContext));
            if (!ReconnectWithNewCertificate(globalMqttAgentContext.mqttContext.transportInterface.pNetworkContext)) {
                UpdateStatusRenew(CertRenewEventRejectedSignedCertificate);
            } else {
                UpdateStatusRenew(CertRenewEventRevokeOldCertificate);
            }
        } else {
            RestoreMQTTConnection(globalMqttAgentContext.mqttContext.transportInterface.pNetworkContext);
        }
    } while (xMQTTStatus == MQTTSuccess);
}

/* Validates the current firmware and cancels rollback if the firmware is valid. */
static void prvCheckFirmware(void)
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI(TAG, "App is valid, rollback cancelled successfully");
            } else {
                ESP_LOGE(TAG, "Failed to cancel rollback");
            }
        }
    }
}

void prvPrintRunningPartition(void)
{
    /* Get the currently running partition */
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running == NULL) {
        ESP_LOGE(TAG, "Failed to get the running partition.");
        return;
    }

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get the OTA state: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Running Partition Information:");
    ESP_LOGI(TAG, "-----------------------------------------");
    ESP_LOGI(TAG, "Partition Name: %s", running->label);
    ESP_LOGI(TAG, "Type: 0x%x", running->type);
    ESP_LOGI(TAG, "Subtype: 0x%x", running->subtype);
    ESP_LOGI(TAG, "Start Address: 0x%lx", running->address);
    ESP_LOGI(TAG, "Size: %d bytes", running->size);
    ESP_LOGI(TAG, "OTA State: 0x%x", ota_state);

    switch (ota_state) {
        case ESP_OTA_IMG_NEW:
            ESP_LOGI(TAG, "State: New image.");
            break;
        case ESP_OTA_IMG_PENDING_VERIFY:
            ESP_LOGI(TAG, "State: Pending verification.");
            break;
        case ESP_OTA_IMG_VALID:
            ESP_LOGI(TAG, "State: Valid image.");
            break;
        case ESP_OTA_IMG_INVALID:
            ESP_LOGI(TAG, "State: Invalid image.");
            break;
        case ESP_OTA_IMG_ABORTED:
            ESP_LOGI(TAG, "State: Update aborted.");
            break;
        default:
            ESP_LOGI(TAG, "State: Unknown.");
            break;
    }
}

static void prvNotifyMainTask(void)
{
    uint32_t ulValueToSend = 0x01;
    xTaskNotify(xTaskGetHandle("main"), ulValueToSend, eSetValueWithOverwrite);
}