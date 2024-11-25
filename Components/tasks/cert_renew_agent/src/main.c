/* AWS IoT SDK Headers */
#include "core_json.h"
#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "jobs.h"

/*
 * Custom Project Headers
 * Application-specific headers for certificate renewal and MQTT operations
 */
#include "cert_renew_agent.h"
#include "gen_csr.h"
#include "key_value_store.h"
#include "mqtt_agent.h"
#include "mqtt_common.h"
#include "queue_handler.h"

#define MAX_COMMAND_SEND_BLOCK_TIME_MS 2000
#define MAX_NUM_OF_DATA_BUFFERS        1U

/* csr topics*/
#define CREATE_CERTIFICATE_FROM_CSR_TOPIC "things/%s/certificate/create-from-csr/json"
#define CREATE_CSR_ACCEPTED_TOPIC         "things/%s/certificate/create-from-csr/json/accepted"
#define CREATE_CSR_REJECTED_TOPIC         "things/%s/certificate/create-from-csr/json/rejected"

/* Certificate revoke topics*/
#define CERT_REVOKE_TOPIC          "things/%s/certificate/revoke/json"
#define CERT_REVOKE_ACCEPTED_TOPIC "things/%s/certificate/revoke/json/accepted"
#define CERT_REVOKE_REJECTED_TOPIC "things/%s/certificate/revoke/json/rejected"

#define SUCCESS_RENEWAL_STATUS_DETAILS "{\"Code\": \"200\", \"Message\": \"Successful certificate renewal\"}"
#define FAILED_RENEWAL_STATUS_DETAILS  "{\"Code\": \"400\", \"Error\": \"Failed to renewal certificate\"}"
#define FAILED_REVOKE_STATUS_DETAILS   "{\"Code\": \"400\", \"Error\": \"Failed to revoke certificate\"}"

#define NUMBER_OF_SUBSCRIPTIONS 2
#define MAX_MESSAGES            5
#define MAX_MSG_SIZE            sizeof(CertRenewEventMsg_t)

#define CERT_RENEWAL_OP "CertRotation"
#define CERT_CLIENT     "client"

/* Data buffers for Cert Renewal events */
static CertRenewDataEvent_t dataBuffers[MAX_NUM_OF_DATA_BUFFERS] = {0};
QueueHandle_t xCertRenewEventQueue;

/* Array containing pointer to the Renewal event structures used to send events to the renew task. */
static CertRenewEventMsg_t xqueueData[MAX_MESSAGES * MAX_MSG_SIZE];

/* The variable used to hold the queue's data structure. */
static StaticQueue_t xStaticQueue;

static char jobId[JOB_ID_LENGTH] = {0};
Operation_t operation            = {0};
char* certificateId              = NULL;

/*
 * Topic Filters
 * Buffer used for subscription topics related to Cert Renew api responses
 */
static char* topic_filters[2] = {NULL, NULL};

const char* pRenewAgentState[CertRenewStateMax] = {
    "Init",
    "Ready",
    "ProcessingJob",
    "ClientCertRenewal",
    "RenewingClientCert",
    "WaitingSignedCertificate",
    "ProcessingSignedCertificate",
    "RevokingOldCertificate"
};

const char* pRenewAgentEvent[CertRenewEventMax] = {
    "Start",
    "CertRenewEventReady",
    "ReceivedJobDocument",
    "ClientCertificateRenewal",
    "ReceivedSignedCertificate",
    "RejectedCertificateSigningRequest",
    "WaitSignedCertificate",
    "RejectedSignedCertificate",
    "RevokeOldCertificate",
    "AcceptedOldCertificateRevoke",
    "RejectedOldCertificateRevoke"
};

CertRenewState_t currentState = CertRenewStateInit;

/* Only Debug to detect stack size*/
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    static UBaseType_t uxHighWaterMark;
#endif

static const char* TAG = "CERT_RENEWAL_AGENT";

extern AWSConnectSettings_t AWSConnectSettings;

static void prvProcessingEvent();
static void prvCreateFromCSRIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo);
static void prvCertRevokeIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo);
static void prvMQTTTerminateCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo);
static void prvJobDocumentParser(char* message, size_t messageLength, Operation_t* jobFields);
static void prvSubscribeRevokeTopics();
static void prvRevokeCertificate();
static void prvStoreNewCredentials();
static void prvSubscribeCreateFromCSRTopics();
static void prvCreateCertificateFromCSR();
static void prvUnSubscribeTopics();
static JSONStatus_t prvReceivedCertificateParser(CertRenewDataEvent_t* certData);
static void prvPrintErrorMessage(const char* message, const size_t messageLength);
static bool isAcceptedTopic(char* receivedTopic);

void renewAgentTask(void* parameters)
{
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
#endif
    CertRenewEventMsg_t nextEvent = {0};
    xCertRenewEventQueue          = InitEvent_FreeRTOS(MAX_MESSAGES, MAX_MSG_SIZE, (uint8_t*)xqueueData, &xStaticQueue, TAG);

    nextEvent.eventId = CertRenewEventReady;

    SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);

    while (true) {
        prvProcessingEvent();
    }
}

/*
 * Implements the state machine for the certificate renewal agent.
 * Processes incoming events from the event queue and transitions
 * the renewal agent through its various states, performing the
 * corresponding actions for each event.
*/
static void prvProcessingEvent()
{
    const char* statusDetails;
    CertRenewEventMsg_t recvEvent = {0};
    CertRenewEventMsg_t nextEvent = {0};

    Operation_t jobFields = {0};

    ReceiveEvent_FreeRTOS(xCertRenewEventQueue, (void*)&recvEvent, portMAX_DELAY, TAG);
    ESP_LOGI(TAG, "Current State: %s | Received Event: %s", pRenewAgentState[currentState], pRenewAgentEvent[recvEvent.eventId]);

    switch (recvEvent.eventId) {
        case CertRenewEventReady:
            currentState = CertRenewStateReady;
            break;
        case CertRenewEventReceivedJobDocument:
            currentState = CertRenewStateProcessingJob;
            strncpy(jobId, recvEvent.jobEvent->jobId, JOB_ID_LENGTH);
            ESP_LOGI(TAG, "Job Id %s", jobId);
            ESP_LOGI(TAG, "Job document %s", recvEvent.jobEvent->jobData);
            prvJobDocumentParser((char*)recvEvent.jobEvent->jobData, recvEvent.jobEvent->jobDataLength, &jobFields);

            assert(jobFields.operation != NULL);
            assert(jobFields.certName != NULL);

            if (!strncmp(jobFields.operation, CERT_RENEWAL_OP, jobFields.operationLength)) {

                if (!strncmp(jobFields.certName, CERT_CLIENT, jobFields.certNameLength)) {
                    SetJobId(jobId);
                    SendUpdateForJob(InProgress, NULL);

                    nextEvent.eventId = CertRenewEventClientCertificateRenewal;
                    SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);
                } else {
                    SendUpdateForJob(Rejected, NULL);
                    ESP_LOGE(TAG, "Error: not certName found");
                    nextEvent.eventId = CertRenewStateReady;
                }
            } else {
                SendUpdateForJob(Rejected, NULL);
                ESP_LOGE(TAG, "Error: not operation found");
                nextEvent.eventId = CertRenewStateReady;
            }
            free(jobFields.operation);
            free(jobFields.certName);

            break;
        case CertRenewEventClientCertificateRenewal:
            prvSubscribeCreateFromCSRTopics();
            prvCreateCertificateFromCSR();

            currentState = CertRenewStateRenewingClientCert;

            nextEvent.eventId = CertRenewEventWaitSignedCertificate;
            SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);

            break;

        case CertRenewEventWaitSignedCertificate:
            currentState = CertRenewStateWaitingSignedCertificate;

            break;

        case CertRenewEventRejectedCertificateSigningRequest:
            ESP_LOGE(TAG, "Certificate Signing request invalid");
            prvPrintErrorMessage((char*)recvEvent.dataEvent->data, recvEvent.dataEvent->dataLength);

            statusDetails = strndup(FAILED_RENEWAL_STATUS_DETAILS, strlen(FAILED_RENEWAL_STATUS_DETAILS));

            if (statusDetails == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for status details");
                return;
            }
            SendUpdateForJob(Failed, statusDetails);
            free((void*)statusDetails);

            currentState = CertRenewStateReady;
            break;

        case CertRenewEventReceivedSignedCertificate:

            currentState = CertRenewStateProcessingSignedCertificate;

            if (prvReceivedCertificateParser(recvEvent.dataEvent) == JSONSuccess) {
                TerminateMQTTAgent(&prvMQTTTerminateCompleteCallback, TAG);
            } else {
                statusDetails = strndup(FAILED_RENEWAL_STATUS_DETAILS, strlen(FAILED_RENEWAL_STATUS_DETAILS));

                if (statusDetails == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for status details");
                    return;
                }
                SendUpdateForJob(Failed, statusDetails);
                free((void*)statusDetails);

                free(certificateId);
                nextEvent.eventId = CertRenewStateReady;
                SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);
            }
            break;
        case CertRenewEventRejectedSignedCertificate:
            statusDetails = strndup(FAILED_RENEWAL_STATUS_DETAILS, strlen(FAILED_RENEWAL_STATUS_DETAILS));

            if (statusDetails == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for status details");
                return;
            }
            SendUpdateForJob(Failed, statusDetails);
            free((void*)statusDetails);
            free(certificateId);

            nextEvent.eventId = CertRenewStateReady;
            ESP_LOGI(TAG, "Failed to renewal certificate");
            SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);
            break;
        case CertRenewEventRevokeOldCertificate:
            prvUnSubscribeTopics();
            prvSubscribeRevokeTopics();
            prvRevokeCertificate();
            currentState = CertRenewStateRevokingOldCertificate;
            break;
        case CertRenewEventAcceptedOldCertificateRevoke:
            prvUnSubscribeTopics();
            free(certificateId);
            ESP_LOGI(TAG, "Successful certificate renewal");
            prvStoreNewCredentials();

#if defined(CONFIG_ENABLE_STACK_WATERMARK)
            uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "HIGH WATER MARK - CERT%d\n", uxHighWaterMark);

            uxHighWaterMark = uxTaskGetStackHighWaterMark( xTaskGetHandle("mqtt_agent") );
            ESP_LOGI( TAG,"HIGH WATER MARK | mqtt_agent %d", uxHighWaterMark);
#endif

            statusDetails = strndup(SUCCESS_RENEWAL_STATUS_DETAILS, strlen(SUCCESS_RENEWAL_STATUS_DETAILS));

            if (statusDetails == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for status details");
                return;
            }
            SendUpdateForJob(Succeeded, statusDetails);

            free((void*)statusDetails);

            nextEvent.eventId = CertRenewEventReady;
            SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);
            break;
        case CertRenewEventRejectedOldCertificateRevoke:
            prvUnSubscribeTopics();
            free(certificateId);
            prvPrintErrorMessage((char*)recvEvent.dataEvent->data, recvEvent.dataEvent->dataLength);

            statusDetails = strndup(FAILED_REVOKE_STATUS_DETAILS, strlen(FAILED_REVOKE_STATUS_DETAILS));

            if (statusDetails == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for status details");
                return;
            }
            SendUpdateForJob(Failed, statusDetails);

            free((void*)statusDetails);

            nextEvent.eventId = CertRenewEventReady;
            SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, TAG);
            break;

        default:
            break;
    }
}

static void prvStoreNewCredentials()
{
    LoadValueToNVS(CERTIFICATE_NVS_KEY, AWSConnectSettings.certificate);
    LoadValueToNVS(PRIVATE_KEY_NVS_KEY, AWSConnectSettings.privateKey);
}

/*
 * Subscribes to the CSR API response topics to receive either the signed certificate
 * or the rejection of the CSR. The topics include:
 *   - things/<ThingName>/certificates/create-from-csr/json/accepted
 *   - things/<ThingName>/certificates/create-from-csr/json/rejected
*/
static void prvSubscribeCreateFromCSRTopics()
{
    MQTTAgentSubscribeArgs_t xSubscribeArgs                       = {0};
    MQTTSubscribeInfo_t subscriptionList[NUMBER_OF_SUBSCRIPTIONS] = {0};

    topic_filters[0] = (char*)calloc(TOPIC_FILTER_LENGTH, sizeof(char));
    topic_filters[1] = (char*)calloc(TOPIC_FILTER_LENGTH, sizeof(char));

    if (topic_filters[0] == NULL && topic_filters[1] == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for CreateFromCSR subscription topics");
        assert(topic_filters[0] != NULL);
        assert(topic_filters[1] != NULL);
    }

    snprintf(topic_filters[0], TOPIC_FILTER_LENGTH, CREATE_CSR_ACCEPTED_TOPIC, GetThingName());
    snprintf(topic_filters[1], TOPIC_FILTER_LENGTH, CREATE_CSR_REJECTED_TOPIC, GetThingName());

    for (int i = 0; i < NUMBER_OF_SUBSCRIPTIONS; i++) {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[i]);
        subscriptionList[i].qos               = MQTTQoS0;
        subscriptionList[i].pTopicFilter      = topic_filters[i];
        subscriptionList[i].topicFilterLength = strlen(topic_filters[i]);
    }
    xSubscribeArgs.numSubscriptions = NUMBER_OF_SUBSCRIPTIONS;
    xSubscribeArgs.pSubscribeInfo   = subscriptionList;

    assert(SubscribeToTopic(&xSubscribeArgs, &prvCreateFromCSRIncomingPublishCallback, TAG) == MQTTSuccess);
}

static void prvUnSubscribeTopics()
{
    MQTTAgentSubscribeArgs_t xUnSubscribeArgs                    = {0};
    MQTTSubscribeInfo_t unSubscribeList[NUMBER_OF_SUBSCRIPTIONS] = {0};

    ESP_LOGI(TAG, "unsubscribing to the topics");

    for (int i = 0; i < NUMBER_OF_SUBSCRIPTIONS; i++) {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[i]);
        unSubscribeList[i].qos               = MQTTQoS0;
        unSubscribeList[i].pTopicFilter      = topic_filters[i];
        unSubscribeList[i].topicFilterLength = strlen(topic_filters[i]);
    }
    xUnSubscribeArgs.numSubscriptions = NUMBER_OF_SUBSCRIPTIONS;
    xUnSubscribeArgs.pSubscribeInfo   = unSubscribeList;

    UnSubscribeToTopic(&xUnSubscribeArgs, TAG);
    free(topic_filters[0]);
    free(topic_filters[1]);
}

/*
 * Creates a Certificate Signing Request (CSR) and sends it to AWS for signing
 * by publishing a message to the MQTT topic things/<ThingName>/certificate/create-from-csr/json.
 * The message contains the certificateSigningRequest.
*/
static void prvCreateCertificateFromCSR()
{
    char topic_filter[TOPIC_FILTER_LENGTH] = {0};

    char* req_msg = (char*)calloc(CSR_BUFFER_SIZE, sizeof(char));
    assert(req_msg != NULL);

    char* escaped_csr = (char*)calloc(CSR_BUFFER_SIZE, sizeof(char));
    assert(escaped_csr != NULL);

    char* key_pem = (char*)calloc(PRIVATE_KEY_BUFFER_SIZE, sizeof(char));
    assert(key_pem != NULL);

    char* csr_pem = (char*)calloc(CSR_BUFFER_SIZE, sizeof(char));
    assert(csr_pem != NULL);

    if (GenerateCSR(key_pem, csr_pem) == true) {
        ESP_LOGI(TAG, "Certificate Signing Request %s", csr_pem);
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

    snprintf(topic_filter,
             sizeof(topic_filter),
             CREATE_CERTIFICATE_FROM_CSR_TOPIC,
             GetThingName());

    PublishToTopic(topic_filter,
                   strlen(topic_filter),
                   req_msg,
                   strlen(req_msg),
                   MQTTQoS0,
                   TAG);

    free(csr_pem);
    free(escaped_csr);
    free(req_msg);
}

/*
 * Subscribes to the certificate revocation API topics to handle responses
 * for revocation requests. 
 * The topics include:
 *   - things/<ThingName>/certificates/revoke/json/accepted
 *   - things/<ThingName>/certificates/revoke/json/rejected
*/
static void prvSubscribeRevokeTopics()
{
    MQTTAgentSubscribeArgs_t xSubscribeArgs                       = {0};
    MQTTSubscribeInfo_t subscriptionList[NUMBER_OF_SUBSCRIPTIONS] = {0};

    topic_filters[0] = (char*)calloc(TOPIC_FILTER_LENGTH, sizeof(char));
    topic_filters[1] = (char*)calloc(TOPIC_FILTER_LENGTH, sizeof(char));

    if (topic_filters[0] == NULL && topic_filters[1] == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for revoke subscription topics");
        assert(topic_filters[0] != NULL);
        assert(topic_filters[1] != NULL);
    }

    snprintf(topic_filters[0], TOPIC_FILTER_LENGTH, CERT_REVOKE_ACCEPTED_TOPIC, GetThingName());
    snprintf(topic_filters[1], TOPIC_FILTER_LENGTH, CERT_REVOKE_REJECTED_TOPIC, GetThingName());

    for (int i = 0; i < NUMBER_OF_SUBSCRIPTIONS; i++) {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[i]);
        subscriptionList[i].qos               = MQTTQoS0;
        subscriptionList[i].pTopicFilter      = topic_filters[i];
        subscriptionList[i].topicFilterLength = strlen(topic_filters[i]);
    }
    xSubscribeArgs.numSubscriptions = NUMBER_OF_SUBSCRIPTIONS;
    xSubscribeArgs.pSubscribeInfo   = subscriptionList;

    assert(SubscribeToTopic(&xSubscribeArgs, &prvCertRevokeIncomingPublishCallback, TAG) == MQTTSuccess);
}

/*
 * The agent revokes the old certificate by publishing a message to the MQTT topic
 * things/<ThingName>/certificates/revoke/json. The message includes the ID of the
 *  new certificate to distinguish it from the old one.
*/
static void prvRevokeCertificate()
{
    char buffer[OLD_REVOKE_MSG_SIZE];
    char topic_filter[TOPIC_FILTER_LENGTH] = {0};

    snprintf(buffer,
             sizeof(buffer),
             "{"
             "\"certificateId\": \"%s\""
             "}",
             certificateId);

    snprintf(topic_filter,
             sizeof(topic_filter),
             CERT_REVOKE_TOPIC,
             GetThingName());

    PublishToTopic(topic_filter,
                   strlen(topic_filter),
                   buffer,
                   strlen(buffer),
                   MQTTQoS0,
                   TAG);
}

/*
 * Parses the received JSON payload containing the signed certificate
 * and extracts the certificate ID and PEM certificate.
 */
static JSONStatus_t prvReceivedCertificateParser(CertRenewDataEvent_t* certData)
{
    JSONStatus_t jsonResult = JSONNotFound;
    const char* jsonValue   = NULL;
    size_t jsonValueLength  = 0U;
    char certificatePem[2048];

    jsonResult = JSON_Validate((char*)certData->data, certData->dataLength);

    if (jsonResult == JSONSuccess) {
        jsonResult = JSON_SearchConst((char*)certData->data,
                                      certData->dataLength,
                                      "certificateId",
                                      13U,
                                      &jsonValue,
                                      &jsonValueLength,
                                      NULL);

        assert(jsonResult == JSONSuccess);

        certificateId = CreateStringCopy(jsonValue, jsonValueLength);

        jsonResult = JSON_SearchConst((char*)certData->data,
                                      certData->dataLength,
                                      "certificatePem",
                                      14U,
                                      &jsonValue,
                                      &jsonValueLength,
                                      NULL);

        assert(jsonResult == JSONSuccess);

        strncpy(certificatePem, jsonValue, jsonValueLength);
        certificatePem[jsonValueLength] = '\0';

        ReplaceEscapedNewlines(certificatePem);

        AWSConnectSettings.newCertificate = CreateStringCopy(certificatePem, jsonValueLength + 1);
    }
    return jsonResult;
}

/*
 * Parses the JSON job document to extract the operation type and certificate name
 * and stores them in the provided Operation_t structure.
*/
static void prvJobDocumentParser(char* message, size_t messageLength, Operation_t* jobFields)
{
    JSONStatus_t jsonResult = JSONSuccess;
    const char* jsonValue   = NULL;
    size_t jsonValueLength  = 0U;

    jsonResult = JSON_Validate((char*)message, messageLength);

    if (jsonResult != JSONSuccess) {
        ESP_LOGE(TAG, "JSON validation failed.");
        assert(jsonResult == JSONSuccess);
        return;
    }

    jsonResult = JSON_SearchConst(message,
                                  messageLength,
                                  "operation",
                                  9U,
                                  &jsonValue,
                                  &jsonValueLength,
                                  NULL);

    if (jsonResult == JSONSuccess) {
        jobFields->operation       = CreateStringCopy(jsonValue, jsonValueLength);
        jobFields->operationLength = jsonValueLength;
    } else {
        ESP_LOGE(TAG, "Operation field not found.");
        assert(jsonResult == JSONSuccess);
        return;
    }

    jsonResult = JSON_SearchConst(message,
                                  messageLength,
                                  "certName",
                                  8U,
                                  &jsonValue,
                                  &jsonValueLength,
                                  NULL);

    if (jsonResult == JSONSuccess) {
        jobFields->certName       = CreateStringCopy(jsonValue, jsonValueLength);
        jobFields->certNameLength = jsonValueLength;
    } else {
        ESP_LOGE(TAG, "CertName field not found.");
        assert(jsonResult == JSONSuccess);
        return;
    }

    assert(jsonResult == JSONSuccess);
}

static void prvPrintErrorMessage(const char* message, const size_t messageLength)
{
    JSONStatus_t jsonResult = JSON_Validate((char*)message, messageLength);

    if (jsonResult != JSONSuccess) {
        ESP_LOGI(TAG, "JSON validation failed");
        return;
    }
    
    char* error_msg = CreateStringCopy(message, messageLength);

    if (error_msg != NULL) {
        ESP_LOGE(TAG, "%s", error_msg);
        free(error_msg);
    }
}

/*
 * Callback executed when a response message from the CSR creation API
 * is received via MQTT. Processes the incoming message to determine
 * whether the CSR was accepted or rejected.
*/ 
static void prvCreateFromCSRIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo)
{
    CertRenewEventMsg_t nextEvent = {0};
    char topic_filter[80]         = {0};

    (void)pvIncomingPublishCallbackContext;

    CertRenewDataEvent_t* dataBuf = &dataBuffers[0];

    memcpy(topic_filter, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength);

    memcpy(dataBuf->data, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
    nextEvent.dataEvent = dataBuf;
    dataBuf->dataLength = pxPublishInfo->payloadLength;

    if (isAcceptedTopic(topic_filter)) {
        nextEvent.eventId = CertRenewEventReceivedSignedCertificate;
        ESP_LOGI("MQTT_AGENT", "Certificate received: %s\n", (char*)pxPublishInfo->pPayload);
    } else {
        nextEvent.eventId = CertRenewEventRejectedCertificateSigningRequest;
    }
    SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, "MQTT_AGENT");
}

static void prvMQTTTerminateCompleteCallback(MQTTAgentCommandContext_t* pxCommandContext, MQTTAgentReturnInfo_t* pxReturnInfo)
{
    ESP_LOGI("MQTT_AGENT", "Terminating mqtt agent loop");
}

/*
 * Callback executed when a response message from the certificate revocation API
 * is received via MQTT. Processes the incoming message to determine whether the
 * certificate revocation was accepted or rejected.
*/ 
static void prvCertRevokeIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo)
{
    CertRenewEventMsg_t nextEvent          = {0};
    char topic_filter[TOPIC_FILTER_LENGTH] = {0};

    (void)pvIncomingPublishCallbackContext;

    memcpy(topic_filter, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength);

    CertRenewDataEvent_t* dataBuf = &dataBuffers[0];

    memcpy(dataBuf->data, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
    nextEvent.dataEvent = dataBuf;
    dataBuf->dataLength = pxPublishInfo->payloadLength;

    if (isAcceptedTopic(topic_filter)) {
        nextEvent.eventId = CertRenewEventAcceptedOldCertificateRevoke;
    } else {
        nextEvent.eventId = CertRenewEventRejectedOldCertificateRevoke;
    }
    SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, "MQTT_AGENT");
}

/* Checks if a given topic corresponds to a acepted topic. */
static bool isAcceptedTopic(char* receivedTopic)
{
    if (strstr(receivedTopic, JOBS_API_SUCCESS) != NULL) {
        return true;
    }

    return false;
}

/*
 * Called by the MQTT agent to send an event to the certificate renewal agent.
 * The event can indicate either a certificate rejection or an old certificate revocation.
*/
void UpdateStatusRenew(BaseType_t status)
{
    CertRenewEventMsg_t nextEvent = {0};
    nextEvent.eventId             = status;
    SendEvent_FreeRTOS(xCertRenewEventQueue, (void*)&nextEvent, "MQTT_AGENT");
}