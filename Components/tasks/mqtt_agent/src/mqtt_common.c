#include <string.h>
#include "core_json.h"
#include "core_mqtt_agent.h"
#include "fleet_provisioning.h"
#include "jobs.h"

#include "esp_mac.h"

#include "cert_renew_agent.h"
#include "mqtt_agent.h"
#include "mqtt_common.h"
#include "mqtt_subscription_manager.h"
#include "ota_agent.h"

#define MAX_COMMAND_SEND_BLOCK_TIME_MS         2000U
#define MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION 10000U

extern MQTTAgentContext_t globalMqttAgentContext;
extern AWSConnectSettings_t AWSConnectSettings;
extern SubscriptionElement_t globalSubscriptionList;
extern QueueHandle_t xCertRenewEventQueue;
extern QueueHandle_t xOtaEventQueue;

static const char* TAG = "MQTT_AGENT";

char* topic_filter = NULL;

static JobEventData_t jobBuffers[1] = {0};
char globalJobId[JOB_ID_LENGTH]     = {0};

#define JOBS_NOTIFY_NEXT_TOPIC "$aws/things/%s/jobs/notify-next"

static void prvMQTTPublishCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo);
static void prvMQTTSubscribeCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo);
static void prvMQTTUnSubscribeCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo);
static void prvIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo);
static bool prIsFreeRTOSOtaJob(const char* jobDoc, size_t jobDocLength);
static bool prIsCertRenewalJob(const char* jobDoc, size_t jobDocLength);
static void prvSendOTAJobDocument(JobEventData_t* jobDocument);
static void prvSendRenewJobDocument(JobEventData_t* jobDocument);

/* Publishes an MQTT message to the MQTT agent's message queue for delivery to AWS IoT Core. */
MQTTStatus_t PublishToTopic(const char* pcTopic, uint16_t usTopicLen, const char* pcMsg, uint32_t ulMsgSize, MQTTQoS_t xQoS, const char* TASK)
{
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = {0};
    MQTTAgentCommandContext_t xCommandContext;
    MQTTPublishInfo_t xPublishInfo;

    memset(&(xCommandContext), 0, sizeof(MQTTAgentCommandContext_t));
    memset(&(xPublishInfo), 0, sizeof(MQTTPublishInfo_t));

    /* Set the required publish parameters. */
    xPublishInfo.pTopicName      = pcTopic;
    xPublishInfo.topicNameLength = usTopicLen;
    xPublishInfo.qos             = xQoS;
    xPublishInfo.pPayload        = pcMsg;
    xPublishInfo.payloadLength   = ulMsgSize;

    xCommandInformation.blockTimeMs                 = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandInformation.cmdCompleteCallback         = prvMQTTPublishCompleteCallback;
    xCommandInformation.pCmdCompleteCallbackContext = &xCommandContext;
    xCommandContext.xTaskToNotify                   = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs                           = NULL;
    xCommandContext.xReturnStatus                   = MQTTSendFailed;

    xCommandAdded = MQTTAgent_Publish(&globalMqttAgentContext, &xPublishInfo, &xCommandInformation);

    if (xCommandAdded == MQTTSuccess) {
        xTaskNotifyWait(0,
                        0,
                        NULL,
                        portMAX_DELAY);

        if (xCommandContext.xReturnStatus != MQTTSuccess) {
            ESP_LOGE(TASK, "Failed to send publish packet to broker with error = %s.", MQTT_Status_strerror(xCommandContext.xReturnStatus));
        } else {
            ESP_LOGI(TASK, "Sent publish packet to broker %.*s to broker.",
                     usTopicLen,
                     pcTopic);
            //ESP_LOGI(TAG, "Message: %s", pcMsg);
        }
    }

    return xCommandContext.xReturnStatus;
}

/*
 * Subscribes to one or more MQTT topics by adding the subscription request to the MQTT agent's
 * message queue, enabling message delivery from AWS IoT Core to the client.
 */
MQTTStatus_t SubscribeToTopic(MQTTAgentSubscribeArgs_t* pcSubsTopics, void* IncomingPublishCallback, const char* TASK)
{
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = {0};
    MQTTAgentCommandContext_t xCommandContext;

    memset(&(xCommandContext), 0, sizeof(MQTTAgentCommandContext_t));

    xCommandInformation.blockTimeMs                 = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandInformation.cmdCompleteCallback         = prvMQTTSubscribeCompleteCallback;
    xCommandInformation.pCmdCompleteCallbackContext = &xCommandContext;

    xCommandContext.xTaskToNotify             = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs                     = pcSubsTopics;
    xCommandContext.xReturnStatus             = MQTTSendFailed;
    xCommandContext.pxIncomingPublishCallback = IncomingPublishCallback;

    xCommandAdded = MQTTAgent_Subscribe(&globalMqttAgentContext, pcSubsTopics, &xCommandInformation);

    

    /*
    xTaskNotifyWait(0,
                    0,
                    NULL,
                    pdMS_TO_TICKS(MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION));
                
    */
    if (xCommandAdded == MQTTSuccess) {
        xTaskNotifyWait(0,
                        0,
                        NULL,
                        portMAX_DELAY);

        if (xCommandContext.xReturnStatus != MQTTSuccess) {
            ESP_LOGE(TASK, "Failed to send subscribe packet to broker with error = %s.", MQTT_Status_strerror(xCommandContext.xReturnStatus));
        } else {
            ESP_LOGI(TASK, "Subscribe topics to broker");
        }
    }
    else {
        ESP_LOGI(TAG, "Failed to push command to queue");
        configASSERT(xCommandAdded == MQTTSuccess);
    }
    return xCommandContext.xReturnStatus;
}

/* Unsubscribes from a set of MQTT topics and removes any registered callbacks. */
MQTTStatus_t UnSubscribeToTopic(MQTTAgentSubscribeArgs_t* pcSubsTopics, const char* TASK)
{
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = {0};
    MQTTAgentCommandContext_t xCommandContext;

    memset(&(xCommandContext), 0, sizeof(MQTTAgentCommandContext_t));

    xCommandInformation.blockTimeMs                 = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandInformation.cmdCompleteCallback         = prvMQTTUnSubscribeCompleteCallback;
    xCommandInformation.pCmdCompleteCallbackContext = &xCommandContext;

    xCommandContext.xTaskToNotify             = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs                     = pcSubsTopics;
    xCommandContext.xReturnStatus             = MQTTSendFailed;
    xCommandContext.pxIncomingPublishCallback = NULL;

    xCommandAdded = MQTTAgent_Unsubscribe(&globalMqttAgentContext, pcSubsTopics, &xCommandInformation);

    configASSERT(xCommandAdded == MQTTSuccess);

    xTaskNotifyWait(0,
                    0,
                    NULL,
                    pdMS_TO_TICKS(MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION));

    if (xCommandContext.xReturnStatus != MQTTSuccess) {
        ESP_LOGE(TASK, "Failed to send UnSubscribe packet to broker with error = %s.", MQTT_Status_strerror(xCommandContext.xReturnStatus));
    } else {
        ESP_LOGI(TASK, "UnSubscribe topics to broker");
    }

    return xCommandContext.xReturnStatus;
}

static void prvMQTTPublishCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo)
{
    pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

    // ESP_LOGI(pcTaskGetName(xTaskGetCurrentTaskHandle()), "In MQTTPublishCompleteCallback %s", MQTT_Status_strerror(pxReturnInfo->returnCode));

    if (pxCommandContext->xTaskToNotify != NULL) {
        xTaskNotify(pxCommandContext->xTaskToNotify,
                    pxCommandContext->ulNotificationValue,
                    eSetValueWithOverwrite);
    }
}

/* 
 * Function executed when an acknowledgment (ACK) is received for an MQTT subscription packet.
 * Its primary role is to add a handler function to the subscription list to manage incoming messages
 * for the subscribed topics.
 */
static void prvMQTTSubscribeCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo)
{
    MQTTAgentCommandContext_t* pxApplicationDefinedContext = (MQTTAgentCommandContext_t*)pxCommandContext;
    MQTTAgentSubscribeArgs_t* pxSubscribeArgs              = (MQTTAgentSubscribeArgs_t*)pxCommandContext->pArgs;

    pxApplicationDefinedContext->xReturnStatus = pxReturnInfo->returnCode;
    ESP_LOGI(TAG, "In the MQTT Subscribe Complete Callback\n");

    if (pxReturnInfo->returnCode == MQTTSuccess) {
        /* Add subscription so that incoming publishes are routed to the application callback. */
        for (size_t i = 0; i < pxSubscribeArgs->numSubscriptions; i++) {
            bool xSubscriptionAdded = SubscriptionManager_AddSubscription((SubscriptionElement_t*)globalMqttAgentContext.pIncomingCallbackContext,
                                                                          pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter,
                                                                          pxSubscribeArgs->pSubscribeInfo[i].topicFilterLength,
                                                                          pxApplicationDefinedContext->pxIncomingPublishCallback,
                                                                          NULL);

            if (xSubscriptionAdded == false) {
                ESP_LOGI(TAG, "Failed to register an incoming publish callback for topic %.*s.", pxSubscribeArgs->pSubscribeInfo[i].topicFilterLength, pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter);
            } else {
                ESP_LOGI(TAG, "Successful subscription %s\n", pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter);
            }

            ESP_LOGI(TAG, "Topic added: %s",
                     ((SubscriptionElement_t*)globalMqttAgentContext.pIncomingCallbackContext)[i].pcSubscriptionFilterString);
        }
    }

    xTaskNotify(pxCommandContext->xTaskToNotify,
                (uint32_t)(pxReturnInfo->returnCode),
                eSetValueWithOverwrite);
}

/*
 * Function executed when an acknowledgment (ACK) is received for an MQTT unsubscribe packet.
 * Its primary role is to remove the handler functions for the unsubscribed topics from the subscription list.
 */
static void prvMQTTUnSubscribeCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo)
{
    MQTTAgentCommandContext_t* pxApplicationDefinedContext = (MQTTAgentCommandContext_t*)pxCommandContext;
    MQTTAgentSubscribeArgs_t* pxSubscribeArgs              = (MQTTAgentSubscribeArgs_t*)pxCommandContext->pArgs;

    pxApplicationDefinedContext->xReturnStatus = pxReturnInfo->returnCode;

    ESP_LOGI(TAG, "In the unsubscribe complete callback");

    if (pxReturnInfo->returnCode == MQTTSuccess) {
        for (size_t i = 0; i < pxSubscribeArgs->numSubscriptions; i++) {
            SubscriptionManager_RemoveSubscription((SubscriptionElement_t*)globalMqttAgentContext.pIncomingCallbackContext,
                                                   pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter,
                                                   pxSubscribeArgs->pSubscribeInfo[i].topicFilterLength);
        }
    }
    xTaskNotify(pxCommandContext->xTaskToNotify,
                (uint32_t)(pxReturnInfo->returnCode),
                eSetValueWithOverwrite);
}

/*
 * Used by the certificate renewal agent to terminate the MQTT agent's execution
 * in order to validate the new certificate.
 */
MQTTStatus_t TerminateMQTTAgent(void* IncomingPublishCallback, const char* TASK)
{
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = {0};

    xCommandInformation.cmdCompleteCallback = IncomingPublishCallback;
    xCommandInformation.blockTimeMs         = MAX_COMMAND_SEND_BLOCK_TIME_MS;

    xCommandAdded = MQTTAgent_Terminate(&globalMqttAgentContext, &xCommandInformation);

    configASSERT(xCommandAdded == MQTTSuccess);

    return xCommandAdded;
}

/*
 * Subscribes to the AWS IoT Jobs topic to receive notifications about the next job for the device.
 * The topic is $aws/things/<ThingName>/jobs/notify-next
 */
MQTTStatus_t SubscribeToNextJobTopic()
{
    MQTTStatus_t xStatus;
    uint16_t xPacketId;
    bool xSubscriptionAdded              = false;
    MQTTSubscribeInfo_t subscriptionList = {0};

    topic_filter = (char*)calloc(TOPIC_FILTER_LENGTH, sizeof(char));

    sprintf(topic_filter, JOBS_NOTIFY_NEXT_TOPIC, GetThingName());

    subscriptionList.qos               = MQTTQoS0;
    subscriptionList.pTopicFilter      = topic_filter;
    subscriptionList.topicFilterLength = strlen(topic_filter);

    ESP_LOGI(TAG, "Subscribing to the topic %s", topic_filter);

    xPacketId = MQTT_GetPacketId(&(globalMqttAgentContext.mqttContext));
    xStatus   = MQTT_Subscribe(&(globalMqttAgentContext.mqttContext), &subscriptionList, 1, xPacketId);

    assert(xStatus == MQTTSuccess);

    xSubscriptionAdded = SubscriptionManager_AddSubscription((SubscriptionElement_t*)globalMqttAgentContext.pIncomingCallbackContext,
                                                             subscriptionList.pTopicFilter,
                                                             subscriptionList.topicFilterLength,
                                                             &prvIncomingPublishCallback,
                                                             NULL);

    if (xSubscriptionAdded == false) {
        ESP_LOGI(TAG, "Failed to register an incoming publish callback for topic %.*s.", subscriptionList.topicFilterLength, subscriptionList.pTopicFilter);
    } else {
        ESP_LOGI(TAG, "Successful subscription\n");
    }

    if (xStatus == MQTTSuccess) {
        xStatus = WaitForPacketAck(&(globalMqttAgentContext.mqttContext),
                                   xPacketId,
                                   1000);
        assert(xStatus == MQTTSuccess);
    }

    return xStatus;
}

/*
 * Handles incoming MQTT publish messages ($aws/things/<ThingName>/jobs/notify-next).
 * Extracts the Job ID and Job Document from the payload and publishes the document
 * to either the OTA agent or the certificate renewal agent based on the job type.
 */
static void prvIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo)
{
    char* jobDoc;
    const char* jobId;
    size_t jobIdLength         = 0U;
    JobEventData_t jobDocument = {0};

    (void)pvIncomingPublishCallbackContext;

    jobIdLength = Jobs_GetJobId((const char*)pxPublishInfo->pPayload, pxPublishInfo->payloadLength, &jobId);

    if (jobIdLength > 0) {
        size_t jobDocLength = Jobs_GetJobDocument((const char*)pxPublishInfo->pPayload, pxPublishInfo->payloadLength, (const char**)&jobDoc);

        if (jobDocLength != 0U) {
            strncpy(jobDocument.jobData, jobDoc, jobDocLength);
            strncpy(jobDocument.jobId, jobId, jobIdLength);

            jobDocument.jobDataLength = jobDocLength;

            ESP_LOGI(TAG, "JobDocument: %s\n", jobDocument.jobData);

            if (prIsFreeRTOSOtaJob(jobDoc, jobDocLength)) {
                prvSendOTAJobDocument(&jobDocument);
            } else if (prIsCertRenewalJob(jobDoc, jobDocLength)) {
                prvSendRenewJobDocument(&jobDocument);
            } else {
                ESP_LOGE(TAG, "JobDocument failed");
            }
        } else {
            ESP_LOGE(TAG, "JobDocument received failed");
        }
    }
}

/* Sends an OTA job document to the OTA task via a queue. */
static void prvSendOTAJobDocument(JobEventData_t* jobDocument)
{
    OtaEventMsg_t nextEvent = {0};
    nextEvent.eventId       = OtaEventReceivedJobDocument;

    memcpy(jobBuffers, jobDocument, sizeof(JobEventData_t));

    jobBuffers->jobDataLength = jobDocument->jobDataLength;

    nextEvent.jobEvent = &jobBuffers[0];

    SendEvent_FreeRTOS(xOtaEventQueue, (void*)&nextEvent, TAG);
}

/* Sends a certificate renewal job document to the certificate renewal task via a queue. */
static void prvSendRenewJobDocument(JobEventData_t* jobDocument)
{
    CertRenewEventMsg_t nextEvent = {0};
    nextEvent.eventId         = CertRenewEventReceivedJobDocument;

    memcpy(jobBuffers, jobDocument, sizeof(JobEventData_t));

    jobBuffers->jobDataLength = jobDocument->jobDataLength;

    nextEvent.jobEvent = &jobBuffers[0];

    SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);
}

/* FreeRTOS OTA updates have a top level "afr_ota" job document key.
 * Check for this to ensure the document is an FreeRTOS OTA update 
 */
static bool prIsFreeRTOSOtaJob(const char* jobDoc, size_t jobDocLength)
{
    JSONStatus_t jsonResult = JSONNotFound;
    const char* afrOtaDocHeader;
    size_t afrOtaDocHeaderLength = 0U;


    jsonResult = JSON_SearchConst(jobDoc,
                                  jobDocLength,
                                  "afr_ota",
                                  7U,
                                  &afrOtaDocHeader,
                                  &afrOtaDocHeaderLength,
                                  NULL);

    return (JSONSuccess == jsonResult);
}

/* Updates the execution status of a job on AWS IoT Jobs. */
void SendUpdateForJob(JobCurrentStatus_t pcJobStatus, const char* pcJobStatusMsg)
{
    JobsStatus_t xStatus                 = JobsSuccess;
    JobsUpdateRequest_t jobUpdateRequest = {0};
    char pUpdateJobTopic[JOBS_API_MAX_LENGTH(strlen(GetThingName()))];
    size_t ulTopicLength = 0U;

    xStatus = Jobs_Update(pUpdateJobTopic,
                          sizeof(pUpdateJobTopic),
                          GetThingName(),
                          strlen(GetThingName()),
                          GetJobId(),
                          strlen(GetJobId()),
                          &ulTopicLength);

    if (xStatus == JobsSuccess) {
        jobUpdateRequest.status = pcJobStatus;

        if (pcJobStatusMsg != NULL) {
            jobUpdateRequest.statusDetails       = pcJobStatusMsg;
            jobUpdateRequest.statusDetailsLength = strlen(pcJobStatusMsg);
        }
        char messageBuffer[UPDATE_REQUEST_SIZE] = {0};
        size_t messageLength                    = Jobs_UpdateMsg(jobUpdateRequest, messageBuffer, JOB_UPDATE_SIZE);

        if (messageLength > 0) {
            PublishToTopic(pUpdateJobTopic,
                           strlen(pUpdateJobTopic),
                           messageBuffer,
                           strlen(messageBuffer),
                           MQTTQoS0,
                           TAG);
        } else {
            ESP_LOGE(TAG, "Failed to generate job Update Request");
        }

    } else {
        ESP_LOGE(TAG, "Failed to generate Publish topic string for sending job update: %s", GetJobId());
    }
}

/* Determines if a job document corresponds to a certificate renewal job by checking for the "operation" key. */
static bool prIsCertRenewalJob(const char* jobDoc, size_t jobDocLength)
{
    JSONStatus_t jsonResult = JSONNotFound;
    const char* jsonValue   = NULL;
    size_t jsonValueLength  = 0U;

    jsonResult = JSON_SearchConst(jobDoc,
                                  jobDocLength,
                                  "operation",
                                  9U,
                                  &jsonValue,
                                  &jsonValueLength,
                                  NULL);

    return (JSONSuccess == jsonResult);
}

MQTTStatus_t WaitForPacketAck(MQTTContext_t* pMqttContext,
                              uint16_t usPacketIdentifier,
                              uint32_t ulTimeout)
{
    uint32_t ulMqttProcessLoopEntryTime;
    uint32_t ulMqttProcessLoopTimeoutTime;
    uint32_t ulCurrentTime;

    MQTTStatus_t xMqttStatus = MQTTSuccess;

    ulCurrentTime                = pMqttContext->getTime();
    ulMqttProcessLoopEntryTime   = ulCurrentTime;
    ulMqttProcessLoopTimeoutTime = ulCurrentTime + ulTimeout;

    /* Call MQTT_ProcessLoop multiple times until the expected packet ACK
     * is received, a timeout happens, or MQTT_ProcessLoop fails. */
    while ((ulCurrentTime < ulMqttProcessLoopTimeoutTime) &&
           (xMqttStatus == MQTTSuccess || xMqttStatus == MQTTNeedMoreBytes)) {
        xMqttStatus   = MQTT_ProcessLoop(pMqttContext);
        ulCurrentTime = pMqttContext->getTime();
    }

    if ((xMqttStatus != MQTTSuccess) && (xMqttStatus != MQTTNeedMoreBytes)) {
        ESP_LOGE(TAG, "MQTT_ProcessLoop failed to receive ACK packet: Expected ACK Packet ID=%02X, LoopDuration=%" PRIu32 ", Status=%s",
                 usPacketIdentifier,
                 (ulCurrentTime - ulMqttProcessLoopEntryTime),
                 MQTT_Status_strerror(xMqttStatus));
    }

    return xMqttStatus;
}

bool ProcessLoopWithTimeout(MQTTContext_t* pMqttContext)
{
    uint32_t ulMqttProcessLoopTimeoutTime;
    uint32_t ulCurrentTime;

    MQTTStatus_t xMqttStatus = MQTTSuccess;
    bool returnStatus        = false;

    ulCurrentTime                = pMqttContext->getTime();
    ulMqttProcessLoopTimeoutTime = ulCurrentTime + (MQTT_PROCESS_LOOP_TIMEOUT_MS * 5);

    /* Call MQTT_ProcessLoop multiple times until the timeout expires or* #MQTT_ProcessLoop fails. */
    while ((ulCurrentTime < ulMqttProcessLoopTimeoutTime) &&
           (xMqttStatus == MQTTSuccess || xMqttStatus == MQTTNeedMoreBytes)) {
        xMqttStatus   = MQTT_ProcessLoop(pMqttContext);
        ulCurrentTime = pMqttContext->getTime();
    }

    if ((xMqttStatus != MQTTSuccess) && (xMqttStatus != MQTTNeedMoreBytes)) {
        ESP_LOGE(TAG, "MQTT_ProcessLoop returned with status = %s.", MQTT_Status_strerror(xMqttStatus));
    } else {
        returnStatus = true;
    }

    return returnStatus;
}

/*
 * Retrieves the device's MAC address from the ESP32 efuse and formats it
 * as a string in the format xx:xx:xx:xx:xx:xx.
 */

char* GetMacAddress()
{
    uint8_t buffer[6] = {0};
    char* mac_address = (char*)calloc(THING_NAME_LENGTH, sizeof(char));

    assert(mac_address != NULL);

    if (esp_efuse_mac_get_default(buffer) == ESP_OK) {
        sprintf(mac_address, "%02x:%02x:%02x:%02x:%02x:%02x", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
        return mac_address;
    } else {
        ESP_LOGE(TAG, "Failed to get mac address");
        free(mac_address);
        return NULL;
    }
}

/* Helper functions*/
void ReplaceEscapedNewlines(char* str)
{
    char* read  = str;
    char* write = str;

    while (*read) {
        if (*read == '\\' && *(read + 1) == 'n') {
            *write++ = '\n';
            read += 2;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

void EscapeNewlines(const char* input, char* output)
{
    while (*input) {
        if (*input == '\n') {
            *output++ = '\\';
            *output++ = 'n';
        } else {
            *output++ = *input;
        }
        input++;
    }
    *output = '\0';
}

char* CreateStringCopy(const char* src, size_t srcLength)
{
    char* target = calloc(srcLength + 1, sizeof(char));

    if (target == NULL) {
        ESP_LOGE(TAG, "Failed to reserve memory\n");
        return NULL;
    } else {
        strncpy(target, src, srcLength);
        target[srcLength] = '\0';
        return target;
    }
}

void SetJobId(const char* jobId)
{
    strncpy(globalJobId, jobId, JOB_ID_LENGTH);
}

const char* GetJobId()
{
    return globalJobId;
}

char* GetThingName()
{
    return AWSConnectSettings.thingName;
}