#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gen_csr.h"
#include "key_value_store.h"
#include "mqtt_agent.h"
#include "mqtt_onboarding.h"
#include "mqtt_subscription_manager.h"

extern MQTTAgentContext_t globalMqttAgentContext;
extern SubscriptionElement_t globalSubscriptionList;
extern AWSConnectSettings_t AWSConnectSettings;

CertificateOnBoarding_t CertificateOnBoarding = {0};
static char* certificateOwnershipToken        = NULL;
static const char* TAG                        = "MQTT_AGENT_ONBOARDING";

/* Only Debug to detect stack size*/
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    static UBaseType_t uxHighWaterMark;
#endif

static void prvIncomingProvisioningPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo);
static void prvPublishToTopic(const char* pcTopic, uint16_t usTopicLen, const char* pcMsg, uint32_t ulMsgSize, MQTTQoS_t xQoS);
static bool parseCSRResponse(const char* pResponse, size_t length);
static bool parseRegisterThingResponse(const char* pResponse, size_t length);


void StartOnboarding(NetworkContext_t* pNetworkContext, TransportInterface_t* pTransport)
{
    ESP_LOGI(TAG, "Onboarding....");

#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
#endif
    LoadAWSSettings(ONBOARDING);
    SetConnectionTLS(pNetworkContext);
    SetRootCA(pNetworkContext, AWS_ROOT_CA);
    NetworkTransportInit(pNetworkContext, pTransport);
    MQTTAgentInit(pTransport);

    ESP_LOGI(TAG, "Establishing MQTT session with claim certificate...to %s:%d", AWSConnectSettings.endpoint, AWS_SECURE_MQTT_PORT);

    EstablishMQTTSession(pNetworkContext, CONNECTION_RETRY_MAX_ATTEMPTS);

    SubscribeOnBoardingTopic();
    CreateCertificateFromCSR();
    RegisterThingAWS();
    StoreNewCredentials();

    HandleMQTTDisconnect(pNetworkContext, &(globalMqttAgentContext.mqttContext));
    UpdateAWSSettings(pNetworkContext);

    ESP_LOGI(TAG, "Establishing MQTT session with new certificate...to %s:%d", AWSConnectSettings.endpoint, AWS_SECURE_MQTT_PORT);

    EstablishMQTTSession(pNetworkContext, CONNECTION_RETRY_MAX_ATTEMPTS);
    UnSubscribeOnBoardingTopic();
    DisableOnBoarding();

    ESP_LOGI(TAG, "Successful Onboarding");
    
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "HIGH WATER MARK END: %d\n", uxHighWaterMark);
#endif
}

/*
 *  Subscribes to AWS IoT onboarding topics to monitor the success or failure of
 *  certificate creation and Thing registration operations. The topics include:
 *    - $aws/certificates/create-from-csr/json/accepted
 *    - $aws/certificates/create-from-csr/json/rejected
 *    - $aws/provisioning-templates/<templateName>/provision/json/accepted 
 *    - $aws/provisioning-templates/<templateName>/provision/json/rejected
 */
MQTTStatus_t SubscribeOnBoardingTopic()
{
    MQTTStatus_t xStatus;
    uint16_t xPacketId;

    MQTTSubscribeInfo_t subscriptionList[NUMBER_OF_SUBSCRIPTIONS_ONBOARDING] = {0};

    const char* topic_filters[NUMBER_OF_SUBSCRIPTIONS_ONBOARDING] = {
        FP_JSON_CREATE_CERT_ACCEPTED_TOPIC,
        FP_JSON_CREATE_CERT_REJECTED_TOPIC,
        FP_JSON_REGISTER_ACCEPTED_TOPIC(PROVISIONING_TEMPLATE_NAME),
        FP_JSON_REGISTER_REJECTED_TOPIC(PROVISIONING_TEMPLATE_NAME)
    };

    ESP_LOGI(TAG, "Subscribing to the onboarding topics\n");

    for (int i = 0; i < NUMBER_OF_SUBSCRIPTIONS_ONBOARDING; i++) {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[i]);
        subscriptionList[i].qos               = MQTTQoS0;
        subscriptionList[i].pTopicFilter      = topic_filters[i];
        subscriptionList[i].topicFilterLength = strlen(topic_filters[i]);

        bool xSubscriptionAdded = SubscriptionManager_AddSubscription((SubscriptionElement_t*)globalMqttAgentContext.pIncomingCallbackContext,
                                                                      topic_filters[i],
                                                                      strlen(topic_filters[i]),
                                                                      &prvIncomingProvisioningPublishCallback,
                                                                      NULL);

        if (xSubscriptionAdded == false) {
            ESP_LOGE(TAG, "Failed to register an incoming publish callback for topic %.*s.", subscriptionList[i].topicFilterLength, subscriptionList[i].pTopicFilter);
        } else {
            ESP_LOGI(TAG, "Successful subscription");
        }
    }

    xPacketId = MQTT_GetPacketId(&(globalMqttAgentContext.mqttContext));

    xStatus = MQTT_Subscribe(&(globalMqttAgentContext.mqttContext),
                             subscriptionList,
                             NUMBER_OF_SUBSCRIPTIONS_ONBOARDING,
                             xPacketId);

    assert(xStatus == MQTTSuccess);

    if (xStatus == MQTTSuccess) {
        xStatus = WaitForPacketAck(&(globalMqttAgentContext.mqttContext),
                                   xPacketId,
                                   MQTT_PROCESS_LOOP_TIMEOUT_MS);
        assert(xStatus == MQTTSuccess);
    }

    return xStatus;
}

/*
 * Unsubscribes from MQTT topics related to the onboarding process. Cleans up
 * any registered callbacks and removes subscriptions from the Subscription Manager.
*/
void UnSubscribeOnBoardingTopic()
{
    MQTTStatus_t xStatus;
    MQTTSubscribeInfo_t unsubscribeList[NUMBER_OF_SUBSCRIPTIONS_ONBOARDING] = {0};

    const char* topic_filters[NUMBER_OF_SUBSCRIPTIONS_ONBOARDING] = {
        FP_JSON_CREATE_CERT_ACCEPTED_TOPIC,
        FP_JSON_CREATE_CERT_REJECTED_TOPIC,
        FP_JSON_REGISTER_ACCEPTED_TOPIC(PROVISIONING_TEMPLATE_NAME),
        FP_JSON_REGISTER_REJECTED_TOPIC(PROVISIONING_TEMPLATE_NAME)
    };

    ESP_LOGI(TAG, "unsubscribing to the onboarding topics\n");

    for (int i = 0; i < NUMBER_OF_SUBSCRIPTIONS_ONBOARDING; i++) {
        unsubscribeList[i].pTopicFilter      = topic_filters[i];
        unsubscribeList[i].topicFilterLength = strlen(topic_filters[i]);

        SubscriptionManager_RemoveSubscription((SubscriptionElement_t*)globalMqttAgentContext.pIncomingCallbackContext,
                                               topic_filters[i],
                                               strlen(topic_filters[i]));

        uint16_t xPacketId = MQTT_GetPacketId(&(globalMqttAgentContext.mqttContext));

        xStatus = MQTT_Unsubscribe(&(globalMqttAgentContext.mqttContext),
                                   &unsubscribeList[i],
                                   1,
                                   xPacketId);

        assert(xStatus == MQTTSuccess);

        if (xStatus == MQTTSuccess) {
            xStatus = WaitForPacketAck(&(globalMqttAgentContext.mqttContext),
                                       xPacketId,
                                       MQTT_PROCESS_LOOP_TIMEOUT_MS);
            assert(xStatus == MQTTSuccess);
        }
    }
}

/*
 * Generates a CSR and sends it to AWS IoT Core via
 * the Fleet Provisioning API ($aws/certificates/create-from-csr/json)
 * to create a new certificate.
 */
void CreateCertificateFromCSR()
{
    char* req_msg = (char*)calloc(CSR_BUFFER_SIZE, sizeof(char));
    assert(req_msg != NULL);

    char* escaped_csr = (char*)calloc(CSR_BUFFER_SIZE, sizeof(char));
    assert(escaped_csr != NULL);

    char* key_pem = (char*)calloc(PRIVATE_KEY_BUFFER_SIZE, sizeof(char));
    assert(key_pem != NULL);

    char* csr_pem = (char*)calloc(CSR_BUFFER_SIZE, sizeof(char));
    assert(csr_pem != NULL);

    if (GenerateCSR(key_pem, csr_pem) == true) {
        ESP_LOGI(TAG, "Certificate Signing Request: \n%s", csr_pem);
        AWSConnectSettings.newPrivateKey = key_pem;
    } else {
        ESP_LOGE(TAG, "Failed generate csr");
        exit(1);
    }

    EscapeNewlines(csr_pem, escaped_csr);

    snprintf(req_msg,
             CSR_BUFFER_SIZE,
             CERTIFICATE_SIGNING_REQUEST_BODY,
             escaped_csr);

    prvPublishToTopic(FP_JSON_CREATE_CERT_PUBLISH_TOPIC,
                      FP_JSON_CREATE_CERT_PUBLISH_LENGTH,
                      req_msg,
                      strlen(req_msg),
                      MQTTQoS0);

    ProcessLoopWithTimeout(&(globalMqttAgentContext.mqttContext));

    free(req_msg);
    free(escaped_csr);
}

/*
 * Register the device with AWS IoT Core using the 
 * Fleet Provisioning API ($aws/provisioning-templates/templateName/provision/json).
 * Sends a JSON payload containing the certificate ownership token and
 * the device's MAC address.
 */
void RegisterThingAWS()
{
    char buffer[REGISTER_THING_BUFFER_SIZE] = {0};
    char* mac_address = GetMacAddress();

    snprintf(buffer,
             sizeof(buffer),
             REGISTER_THING_BODY,
             certificateOwnershipToken,
             mac_address);

    prvPublishToTopic(FP_JSON_REGISTER_PUBLISH_TOPIC(PROVISIONING_TEMPLATE_NAME),
                      FP_JSON_REGISTER_PUBLISH_LENGTH(strlen(PROVISIONING_TEMPLATE_NAME)),
                      buffer,
                      strlen(buffer),
                      MQTTQoS0);

    free(mac_address);

    ProcessLoopWithTimeout(&(globalMqttAgentContext.mqttContext));
}

/* Publishes a message to a specified MQTT topic.*/
static void prvPublishToTopic(const char* pcTopic, uint16_t usTopicLen, const char* pcMsg, uint32_t ulMsgSize, MQTTQoS_t xQoS)
{
    MQTTStatus_t xStatus;
    MQTTPublishInfo_t xPublishInfo;
    uint16_t packetId;
    memset(&(xPublishInfo), 0, sizeof(MQTTPublishInfo_t));
    xPublishInfo.qos             = xQoS;
    xPublishInfo.pTopicName      = pcTopic;
    xPublishInfo.topicNameLength = usTopicLen;
    xPublishInfo.pPayload        = pcMsg;
    xPublishInfo.payloadLength   = ulMsgSize;

    ESP_LOGI(TAG, "Publish packet = %s", pcMsg);

    packetId = MQTT_GetPacketId(&(globalMqttAgentContext.mqttContext));
    xStatus  = MQTT_Publish(&(globalMqttAgentContext.mqttContext), &xPublishInfo, packetId);

    if (xStatus != MQTTSuccess) {
        ESP_LOGE(TAG, "Failed to publish the message %d", MQTT_Status_strerror(xStatus));
        assert(xStatus == MQTTSuccess);
    }   
}

/*
 * Callback function executed when a message is received on Fleet Provisioning-related topics.
 * Parses the incoming payload to extract the certificate or Thing Name or detect any provisioning failure.
 */
static void prvIncomingProvisioningPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo)
{
    FleetProvisioningStatus_t xStatus;
    FleetProvisioningTopic_t api;
    (void)pvIncomingPublishCallbackContext;

    xStatus = FleetProvisioning_MatchTopic(pxPublishInfo->pTopicName,
                                           pxPublishInfo->topicNameLength, &api);

    if (xStatus != FleetProvisioningSuccess) {
        ESP_LOGE(TAG, "Unexpected publish message received. Topic: %.*s.",
                 (int)pxPublishInfo->topicNameLength,
                 (const char*)pxPublishInfo->pTopicName);
    } else {
        char* in_packet = CreateStringCopy((const char*)pxPublishInfo->pPayload, pxPublishInfo->payloadLength);

        if (api == FleetProvJsonCreateCertFromCsrAccepted) {
            ESP_LOGI(TAG, "Received accepted response from Fleet Provisioning CreateCertificateFromCsr API.");

            parseCSRResponse(in_packet, pxPublishInfo->payloadLength);

            ESP_LOGI(TAG, "\ncertificateOwnershipToken: %s\n certificate: %s\n",
                     certificateOwnershipToken,
                     AWSConnectSettings.newCertificate);

        } else if (api == FleetProvJsonCreateCertFromCsrRejected) {
            ESP_LOGE(TAG, "Received rejected response from Fleet Provisioning CreateCertificateFromCsr API.");
            ESP_LOGE(TAG, "Errors: %s", in_packet);
            assert(api != FleetProvJsonCreateCertFromCsrRejected);

        }

        else if (api == FleetProvJsonRegisterThingAccepted) {
            ESP_LOGI(TAG, "Received accepted response from Fleet Provisioning RegisterThing API.");
            parseRegisterThingResponse(in_packet, pxPublishInfo->payloadLength);
            ESP_LOGI(TAG, "ThingName %s", AWSConnectSettings.thingName);
        } else if (api == FleetProvJsonRegisterThingRejected) {
            ESP_LOGE(TAG, "Received rejected response from Fleet Provisioning RegisterThing API.");
            ESP_LOGE(TAG, "Errors: %s", in_packet);
            assert(api != FleetProvJsonRegisterThingRejected);
        } else {
            ESP_LOGE(TAG, "Received message on unexpected Fleet Provisioning topic. Topic: %.*s.",
                     (int)pxPublishInfo->topicNameLength,
                     (const char*)pxPublishInfo->pTopicName);
        }
    }
}

/*
 * Parses the response from the certificate creation API to extract the certificate
 * ownership token and the certificate in PEM format.
 */
static bool parseCSRResponse(const char* pResponse, size_t length)
{
    const char* jsonValue   = NULL;
    size_t jsonValueLength  = 0U;
    JSONStatus_t jsonResult = JSONNotFound;

    jsonResult = JSON_Validate((char*)pResponse, length);

    if (jsonResult == JSONSuccess) {
        jsonResult = JSON_SearchConst((char*)pResponse,
                                      length,
                                      "certificateOwnershipToken",
                                      25U,
                                      &jsonValue,
                                      &jsonValueLength,
                                      NULL);
        assert(jsonResult == JSONSuccess);

        certificateOwnershipToken = CreateStringCopy(jsonValue, jsonValueLength);

        jsonResult = JSON_SearchConst((char*)pResponse,
                                      length,
                                      "certificatePem",
                                      14U,
                                      &jsonValue,
                                      &jsonValueLength,
                                      NULL);

        assert(jsonResult == JSONSuccess);

        char certificatePem[2048];
        strncpy(certificatePem, jsonValue, jsonValueLength);
        certificatePem[jsonValueLength] = '\0';

        ReplaceEscapedNewlines(certificatePem);

        AWSConnectSettings.newCertificate = CreateStringCopy(certificatePem, jsonValueLength + 1);
    }

    assert(jsonResult == JSONSuccess);
    return (JSONSuccess == jsonResult);
}

/* Parses the response from the Thing registration API to extract the Thing Name. */
static bool parseRegisterThingResponse(const char* pResponse, size_t length)
{

    const char* jsonValue   = NULL;
    size_t jsonValueLength  = 0U;
    JSONStatus_t jsonResult = JSONNotFound;

    jsonResult = JSON_Validate((char*)pResponse, length);

    if (jsonResult == JSONSuccess) {
        jsonResult = JSON_SearchConst((char*)pResponse,
                                      length,
                                      "thingName",
                                      9U,
                                      &jsonValue,
                                      &jsonValueLength,
                                      NULL);
        assert(jsonResult == JSONSuccess);

        AWSConnectSettings.thingName = CreateStringCopy(jsonValue, jsonValueLength);
    }

    return (JSONSuccess == jsonResult);
}

/*
 * Stores the newly generated certificate, private key, and Thing Name
 * into non-volatile storage (NVS).
 */
void StoreNewCredentials()
{
    LoadValueToNVS(CERTIFICATE_NVS_KEY, AWSConnectSettings.newCertificate);
    LoadValueToNVS(PRIVATE_KEY_NVS_KEY, AWSConnectSettings.newPrivateKey);
    LoadValueToNVS(THING_NAME_NVS_KEY, AWSConnectSettings.thingName);

    free(certificateOwnershipToken);
}

/* Checks whether onboarding is enabled by reading a flag from NVS. */
int8_t IsOnBoardingEnabled()
{
    esp_err_t err;

    int8_t isOnboardingEnabled = 0;

    ESP_LOGI(TAG, "Opening AWS Namespace");

    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);

    err = nvs_get_i8(xHandle, ONBOARDING_ENABLED, &isOnboardingEnabled);

    assert(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);

    nvs_close(xHandle);
    return isOnboardingEnabled;
}

/* Disables onboarding by setting the ONBOARDING_ENABLED namespace flag to 0 in NVS. */
void DisableOnBoarding()
{
    esp_err_t err;

    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READWRITE, &xHandle) != ESP_OK);

    err = nvs_set_i8(xHandle, ONBOARDING_ENABLED, (int8_t)DISABLE_ONBOARDING);

    assert(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);

    nvs_close(xHandle);
}