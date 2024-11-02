#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "core_json.h"
#include "jobs.h"

#include "mqtt_agent.h"
#include "mqtt_common.h"
#include "queue_handler.h"
#include "cert_renew_agent.h"
#include "gen_csr.h"
#include "key_value_store.h"


#define MAX_COMMAND_SEND_BLOCK_TIME_MS ( 2000 )

/* CSR TOPICS*/
#define CREATE_CERTIFICATE_FROM_CSR_TOPIC "things/%s/certificate/create-from-csr/json"
#define CREATE_CSR_ACCEPTED_TOPIC "things/%s/certificate/create-from-csr/json/accepted"
#define CREATE_CSR_REJECTED_TOPIC "things/%s/certificate/create-from-csr/json/rejected"

/* CERT REVOKE TOPICS*/
#define CERT_REVOKE_TOPIC "things/%s/certificate/revoke/json"
#define CERT_REVOKE_ACCEPTED_TOPIC "things/%s/certificate/revoke/json/accepted"
#define CERT_REVOKE_REJECTED_TOPIC "things/%s/certificate/revoke/json/rejected"

#define SUCCESS_STATUS_DETAILS "{\"Code\": \"200\", \"Message\": \"Successful certificate renewal\"}"
#define FAILED_RENEWAL_STATUS_DETAILS "{\"Code\": \"400\", \"Error\": \"Failed to renewal certificate\"}"
#define FAILED_REVOKE_STATUS_DETAILS "{\"Code\": \"400\", \"Error\": \"Failed to revoke certificate\"}"

#define NUMBER_OF_SUBSCRIPTIONS 2
#define MAX_MESSAGES 5
#define MAX_MSG_SIZE sizeof( RenewEventMsg_t )

#define CERT_RENEWAL_OP  "CertRotation"
#define CERT_CLIENT "client"

static const char *TAG = "RENEW_AGENT";

static void prvProcessingEvent();

static void prvCreateFromCSRIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo );
static void prvCertRevokeIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo );

static void prvMQTTTerminateCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );

static void prvJobDocumentParser(char * message, size_t messageLength, Operation_t * jobFields);
static JSONStatus_t prPopulateOperationFields(const char * jobDoc, const size_t jobDocLength, Operation_t * jobFields);
static char * setParametersToStruct(const char * src, size_t srcLength);
static void prvSubscribeRevokeTopics();
static void prvRevokeCertificate();
static void prvStoreNewCredentials();

static void prvSubscribeCreateFromCSRTopics();
static void prvCreateCertificateFromCSR();
static void prvUnSubscribeTopics();
static JSONStatus_t prvReceivedCertificateParser(RenewDataEvent_t * certData);
static void prvPrintErrorMessage(const char *message, const size_t messageLength);

bool isAcceptedTopic(char *receivedTopic);
QueueHandle_t xRenewEventQueue;
/* Array containing pointer to the Renewal event structures used to send events to the renew task. */
static RenewEventMsg_t xqueueData[ MAX_MESSAGES * MAX_MSG_SIZE ];

/* The variable used to hold the queue's data structure. */
static StaticQueue_t xStaticQueue;

static RenewDataEvent_t dataBuffers[1] = { 0 };

static char jobId [JOB_ID_LENGTH] = {0};
Operation_t operation = {0};
static char * topic_filters [2] = { NULL, NULL };
char * certificateId = NULL;

RenewState_t currentState = RenewStateInit;

/* Only Debug to detect stack size*/
static UBaseType_t uxHighWaterMark;

extern AWSConnectSettings_t AWSConnectSettings;

const char * pRenewAgentState[ RenewStateMax ] =
{
    "Init",
    "Ready",
    "ProcessingJob",
    "ClientCertRenewal",
    "RenewingClientCert",
    "WaitingSignedCertificate",
    "ProcessingSignedCertificate",
    "RevokingOldCertificate"
};

const char * pRenewAgentEvent[ RenewEventMax ] =
{
    "Start",
    "RenewEventReady",
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

/* Only validation test */
const char *invalid_csr = 
"-----BEGIN CERTIFICATE REQUEST-----\n"
"INVALIDCSR1234\n"
"MIICkDCCAXgCAQAwSzELMAkGA1UEBhMCQVIxDDAKBgNVBAgMA0NCQTELMAoGA1UE\n"
"INVALIDDATAHEREBwwDQ0JBMRIwEAYDVQQKDAlTZWd1cmlkYWQxDDAKBgNVBAsMA1\n"
"VOQzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMAUTC56+09rAsKrTNh\n"
"URtPprXu0dPID9rBJ/AVnYfm1ONt0rF4kGdR4TwjQ/vIF/ns51o/PkZbUx1vnNd9\n"
"/hFGFpUN/tA8VCstD1ZrQzV2wD2QEcmac5tcAhsU2b9MayP2DrXKIaAHI42Vn22N0\n"
"80tPdKXYoBNnNK4PaW1wwFOcbJhUmFZ+lhQYhFYxVq/YX8rftlJ7jyCQNZNvQPG9T\n"
"UtyxwHJfhuTJCui5l8sejBOds5mNC1d2WUkORfE3P+wXrdyl+w7K00nHz3SuPMTco\n"
"INVALIDLINESk0wsc5rE4MaAQgbNAuTd/74SD9X06iy1XTwLl/3NjnkhO/grGAoRIC\n"
"bg3qX6kue2kCAwEAAaAAMA0GCSqGSIb3DQEBCwUAA4IBAQA3GduVnEEnSbv1MQgNm\n"
"INVALIDCHARACTERSkdOrM8Op6QII0sG3/UXS68SRGG6AdEfiJBMXk2wPmK2b/BL8G\n"
"VYkD05nHgJMLj+ljcSIowLJbvmLy1b3VEE\n"
"-----END CERTIFICATE REQUEST-----";



void renewAgentTask( void * parameters)
{
    uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
    RenewEventMsg_t nextEvent = { 0 };
    xRenewEventQueue = InitEvent_FreeRTOS(MAX_MESSAGES, MAX_MSG_SIZE, (uint8_t *) xqueueData, &xStaticQueue, TAG);

    nextEvent.eventId = RenewEventReady;   

    SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent, TAG);

    while ( true )
    {
        prvProcessingEvent();
    }   
}

static void prvProcessingEvent()
{
    const char * statusDetails;
    RenewEventMsg_t recvEvent = { 0 };
    RenewEventMsg_t nextEvent = { 0 };

    Operation_t jobFields = { 0 };

    ReceiveEvent_FreeRTOS(xRenewEventQueue, (void *) &recvEvent, portMAX_DELAY, TAG);
    ESP_LOGI(TAG,"Current State: %s | Received Event: %s", pRenewAgentState[currentState], pRenewAgentEvent[recvEvent.eventId]);
    
    switch (recvEvent.eventId)
    {
        case RenewEventReady:
            currentState = RenewStateReady;
            break;
        case RenewEventReceivedJobDocument:
            currentState = RenewStateProcessingJob;
            strncpy(jobId, recvEvent.jobEvent->jobId, JOB_ID_LENGTH);
            ESP_LOGI(TAG, "Job Id %s", jobId);
            ESP_LOGI(TAG, "Job document %s", recvEvent.jobEvent->jobData);        
            prvJobDocumentParser((char * )recvEvent.jobEvent->jobData, recvEvent.jobEvent->jobDataLength, &jobFields);

            assert(jobFields.operation != NULL);
            assert(jobFields.certName != NULL);
            
            if (!strncmp(jobFields.operation, CERT_RENEWAL_OP, jobFields.operationLength)) {
               
                if (!strncmp(jobFields.certName, CERT_CLIENT, jobFields.certNameLength))
                {
                    SetJobId(jobId);
                    SendUpdateForJob(InProgress, NULL);
                    
                    nextEvent.eventId = RenewEventClientCertificateRenewal;
                    SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent,TAG); 
                }
                else
                {
                    SendUpdateForJob(Rejected, NULL);
                    ESP_LOGE(TAG, "Error: not certName found");
                    nextEvent.eventId = RenewStateReady;
                }
            }
            else
            {
                SendUpdateForJob(Rejected, NULL);
                ESP_LOGE(TAG, "Error: not operation found");
                nextEvent.eventId = RenewStateReady;
            }
            free(jobFields.operation);
            free(jobFields.certName); 
            
            break;
        case RenewEventClientCertificateRenewal:
            prvSubscribeCreateFromCSRTopics();           
            prvCreateCertificateFromCSR();
            currentState = RenewStateRenewingClientCert;  

            nextEvent.eventId = RenewEventWaitSignedCertificate;
            SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent,TAG); 
            
            break;
            
        case RenewEventWaitSignedCertificate:           
            currentState = RenewStateWaitingSignedCertificate;   

            break;

        case RenewEventRejectedCertificateSigningRequest:
            ESP_LOGE(TAG, "Certificate Signing request invalid");
            prvPrintErrorMessage((char *)recvEvent.dataEvent->data, recvEvent.dataEvent->dataLength);

            statusDetails = strndup(FAILED_RENEWAL_STATUS_DETAILS, strlen(FAILED_RENEWAL_STATUS_DETAILS));

            if (statusDetails == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for status details");
                return;
            }
            SendUpdateForJob(Failed, statusDetails);
            free((void *)statusDetails);               
             
            currentState = RenewStateReady;
            break; 

        case RenewEventReceivedSignedCertificate:

            currentState = RenewStateProcessingSignedCertificate;

            if (prvReceivedCertificateParser(recvEvent.dataEvent) == JSONSuccess)
            {
                TerminateMQTTAgent(&prvMQTTTerminateCompleteCallback, TAG);         
            }
            else
            {
                statusDetails = strndup(FAILED_RENEWAL_STATUS_DETAILS, strlen(FAILED_RENEWAL_STATUS_DETAILS));

                if (statusDetails == NULL)
                {
                    ESP_LOGE(TAG, "Failed to allocate memory for status details");
                    return;
                }
                SendUpdateForJob(Failed, statusDetails);
                free((void *)statusDetails);          

                free(certificateId);
                nextEvent.eventId = RenewStateReady;
                SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent,TAG);                
            }
            break;                       
        case RenewEventRejectedSignedCertificate:
                statusDetails = strndup(FAILED_RENEWAL_STATUS_DETAILS, strlen(FAILED_RENEWAL_STATUS_DETAILS));

                if (statusDetails == NULL)
                {
                    ESP_LOGE(TAG, "Failed to allocate memory for status details");
                    return;
                }
                SendUpdateForJob(Failed, statusDetails);
                free((void *)statusDetails);
                free(certificateId);

                nextEvent.eventId = RenewStateReady;
                ESP_LOGI(TAG, "Failed to renewal certificate");
                SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent,TAG);    
            break;
        case RenewEventRevokeOldCertificate:
            prvUnSubscribeTopics();
            prvSubscribeRevokeTopics();
            prvRevokeCertificate();
            currentState = RenewStateRevokingOldCertificate;
            break;  
        case RenewEventAcceptedOldCertificateRevoke:
            prvUnSubscribeTopics();
            free(certificateId);
            ESP_LOGI(TAG, "Successful certificate renewal");
            prvStoreNewCredentials();
            uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
            ESP_LOGI( TAG,"HIGH WATER MARK %d\n", uxHighWaterMark);

            statusDetails = strndup(SUCCESS_STATUS_DETAILS, strlen(SUCCESS_STATUS_DETAILS));

            if (statusDetails == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for status details");
                return;
            }
            SendUpdateForJob(Succeeded, statusDetails);

            free((void *)statusDetails);
            
            nextEvent.eventId = RenewEventReady;
            SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent,TAG); 
            break;
        case RenewEventRejectedOldCertificateRevoke:
            prvUnSubscribeTopics();
            free(certificateId);
            prvPrintErrorMessage((char *)recvEvent.dataEvent->data, recvEvent.dataEvent->dataLength);

            statusDetails = strndup(FAILED_REVOKE_STATUS_DETAILS, strlen(FAILED_REVOKE_STATUS_DETAILS));

            if (statusDetails == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for status details");
                return;
            }
            SendUpdateForJob(Failed, statusDetails);

            free((void *)statusDetails);

            nextEvent.eventId = RenewEventReady;
            SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent,TAG); 
            break;

        default:
            break;
    }   
}
static void prvStoreNewCredentials()
{
    load_value_to_nvs(CERTIFICATE_NVS_KEY, AWSConnectSettings.certificate);
    load_value_to_nvs(PRIVATE_KEY_NVS_KEY, AWSConnectSettings.privateKey);    
}
static void prvSubscribeCreateFromCSRTopics()
{
    MQTTAgentSubscribeArgs_t xSubscribeArgs = { 0 };
    MQTTSubscribeInfo_t subscriptionList[NUMBER_OF_SUBSCRIPTIONS] = { 0 };
    topic_filters[0] = (char *) calloc(TOPIC_FILTER_LENGTH, sizeof(char));
    topic_filters[1] = (char *) calloc(TOPIC_FILTER_LENGTH, sizeof(char));  

    snprintf(topic_filters[0], TOPIC_FILTER_LENGTH, CREATE_CSR_ACCEPTED_TOPIC, GetThingName());
    snprintf(topic_filters[1], TOPIC_FILTER_LENGTH, CREATE_CSR_REJECTED_TOPIC, GetThingName());
 
    for( int i = 0; i < NUMBER_OF_SUBSCRIPTIONS; i++ )
    {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[ i ]);
        subscriptionList[ i ].qos = MQTTQoS0;
        subscriptionList[ i ].pTopicFilter = topic_filters[ i ];
        subscriptionList[ i ].topicFilterLength = strlen(topic_filters[ i ]);
    }
    xSubscribeArgs.numSubscriptions = NUMBER_OF_SUBSCRIPTIONS;
    xSubscribeArgs.pSubscribeInfo = subscriptionList;

    SubscribeToTopic(&xSubscribeArgs, &prvCreateFromCSRIncomingPublishCallback, TAG);
}
static void prvUnSubscribeTopics()
{
    MQTTAgentSubscribeArgs_t xUnSubscribeArgs = { 0 };    
    MQTTSubscribeInfo_t unSubscribeList[NUMBER_OF_SUBSCRIPTIONS] = { 0 };    
    
    ESP_LOGI(TAG, "unsubscribing to the topics");

    for( int i = 0; i < NUMBER_OF_SUBSCRIPTIONS; i++ )
    {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[ i ]);
        unSubscribeList[ i ].qos = MQTTQoS0;
        unSubscribeList[ i ].pTopicFilter = topic_filters[ i ];
        unSubscribeList[ i ].topicFilterLength = strlen(topic_filters[ i ]);
    }
    xUnSubscribeArgs.numSubscriptions = NUMBER_OF_SUBSCRIPTIONS;
    xUnSubscribeArgs.pSubscribeInfo = unSubscribeList;

    UnSubscribeToTopic(&xUnSubscribeArgs, TAG);
    free(topic_filters[0]);
    free(topic_filters[1]);
}
static void prvCreateCertificateFromCSR()
{
    char topic_filter[100] = {0};
    
    char *req_msg = (char *)calloc(CSR_BUFFER_SIZE, sizeof(char));
    char *escaped_csr = (char *)calloc(CSR_BUFFER_SIZE, sizeof(char));

    char *key_pem = (char *)calloc(PRIVATE_KEY_BUFFER_SIZE, sizeof(char));
    char *csr_pem = (char *)calloc(CSR_BUFFER_SIZE, sizeof(char));

    if (GenerateCSR(key_pem, csr_pem) == true )
    {
        ESP_LOGI(TAG, "Certificate Signing Request %s", csr_pem);
        //ESP_LOGI(TAG, "Private key %s", key_pem);
        AWSConnectSettings.newPrivateKey = key_pem;
    }
    else
    {
        ESP_LOGE(TAG, "Failed generate csr");
        exit(1);
    }

    EscapeNewlines(csr_pem, escaped_csr);


    snprintf(req_msg, 
             CSR_BUFFER_SIZE,
             CERTIFICATE_SIGNING_REQUEST_BODY,
             escaped_csr
            );

    /*
    snprintf(req_msg, 
             CSR_BUFFER_SIZE,
             CERTIFICATE_SIGNING_REQUEST_BODY,
             invalid_csr
            );
    */  
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

static void prvSubscribeRevokeTopics()
{
    MQTTAgentSubscribeArgs_t xSubscribeArgs = { 0 };
    MQTTSubscribeInfo_t subscriptionList[NUMBER_OF_SUBSCRIPTIONS] = { 0 };

    topic_filters[0] = (char *) calloc(TOPIC_FILTER_LENGTH, sizeof(char));
    topic_filters[1] = (char *) calloc(TOPIC_FILTER_LENGTH, sizeof(char));  

    snprintf(topic_filters[0], TOPIC_FILTER_LENGTH, CERT_REVOKE_ACCEPTED_TOPIC, GetThingName());
    snprintf(topic_filters[1], TOPIC_FILTER_LENGTH, CERT_REVOKE_REJECTED_TOPIC, GetThingName());
 
    for( int i = 0; i < NUMBER_OF_SUBSCRIPTIONS; i++ )
    {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[ i ]);
        subscriptionList[ i ].qos = MQTTQoS0;
        subscriptionList[ i ].pTopicFilter = topic_filters[ i ];
        subscriptionList[ i ].topicFilterLength = strlen(topic_filters[ i ]);
    }
    xSubscribeArgs.numSubscriptions = NUMBER_OF_SUBSCRIPTIONS;
    xSubscribeArgs.pSubscribeInfo = subscriptionList;

    SubscribeToTopic(&xSubscribeArgs, &prvCertRevokeIncomingPublishCallback, TAG);
}
static void prvRevokeCertificate()
{
    char buffer[256];
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

static JSONStatus_t prvReceivedCertificateParser(RenewDataEvent_t * certData)
{
    JSONStatus_t jsonResult = JSONNotFound;
    const char * jsonValue = NULL;
    size_t jsonValueLength = 0U;
    char certificatePem[2048]; 

    jsonResult = JSON_Validate( (char *) certData->data, certData->dataLength );

    if( jsonResult == JSONSuccess )
    {
        jsonResult = JSON_SearchConst((char *)certData->data,
                                    certData->dataLength,
                                    "certificateId",
                                    13U,
                                    &jsonValue,
                                    &jsonValueLength,
                                    NULL);

        assert(jsonResult == JSONSuccess);

        certificateId = setParametersToStruct(jsonValue, jsonValueLength);

        jsonResult = JSON_SearchConst((char *)certData->data,
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

        AWSConnectSettings.newCertificate = setParametersToStruct(certificatePem, jsonValueLength + 1);
    }
    return jsonResult;
}
static void prvJobDocumentParser(char * message, size_t messageLength, Operation_t * jobFields)
{
    JSONStatus_t jsonResult = JSONSuccess;

    jsonResult = prPopulateOperationFields(message, messageLength, jobFields);

    assert(jsonResult == JSONSuccess);
}
static char * setParametersToStruct(const char * src, size_t srcLength)
{
    char * target = calloc(srcLength + 1, sizeof(char));

    if (target == NULL)
    {
        ESP_LOGE(TAG, "Failed to reserve memory\n");
        return NULL;
    }
    else
    {
        strncpy(target, src, srcLength);
        target[srcLength] = '\0'; 
        return target;
    }

}
static JSONStatus_t prPopulateOperationFields(const char * jobDoc, const size_t jobDocLength, Operation_t * jobFields) 
{
    JSONStatus_t jsonResult = JSONNotFound;
    const char * jsonValue = NULL;
    size_t jsonValueLength = 0U;

    jsonResult = JSON_Validate( (char *) jobDoc, jobDocLength );

    if (jsonResult != JSONSuccess)
    {
        ESP_LOGE(TAG, "JSON validation failed.");
        return jsonResult;
    }
    jsonResult = JSON_SearchConst( jobDoc,
                                jobDocLength,
                                "operation",
                                9U,
                                &jsonValue,
                                &jsonValueLength,
                                NULL );
    
    if (jsonResult == JSONSuccess)
    {
        jobFields->operation = setParametersToStruct(jsonValue, jsonValueLength);
        jobFields->operationLength = jsonValueLength;        
    }
    else
    {
        ESP_LOGE(TAG, "Operation field not found.");
        return jsonResult; 
    }

    jsonResult = JSON_SearchConst( jobDoc,
                                jobDocLength,
                                "certName",
                                8U,
                                &jsonValue,
                                &jsonValueLength,
                                NULL );

    if (jsonResult == JSONSuccess)
    {                           
        jobFields->certName = setParametersToStruct(jsonValue, jsonValueLength);
        jobFields->certNameLength = jsonValueLength;
    }
    else
    {
        ESP_LOGE(TAG, "CertName field not found.");
        return jsonResult;
    }

    return jsonResult;
    
}

static void prvPrintErrorMessage(const char *message, const size_t messageLength)
{
    JSONStatus_t jsonResult = JSON_Validate((char *)message, messageLength);

    if (jsonResult != JSONSuccess)
    {
        ESP_LOGI(TAG, "JSON validation failed");
        return;
    }

    char * error_msg = setParametersToStruct(message, messageLength);

    ESP_LOGE(TAG, "%s", error_msg);

    free(error_msg);

}

static void prvCreateFromCSRIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo )
{
    RenewEventMsg_t nextEvent = { 0 };
    char topic_filter[80] = { 0 };

    (void ) pvIncomingPublishCallbackContext;
    
    RenewDataEvent_t * dataBuf = &dataBuffers[0];

    memcpy(topic_filter, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength);   
    
    memcpy(dataBuf->data, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
    nextEvent.dataEvent = dataBuf;
    dataBuf->dataLength = pxPublishInfo->payloadLength;  

    if (isAcceptedTopic(topic_filter))
    {
        nextEvent.eventId = RenewEventReceivedSignedCertificate;
        ESP_LOGI("MQTT_AGENT", "Certificate received: %s\n", (char *) pxPublishInfo->pPayload);
    }
    else
    {
        nextEvent.eventId = RenewEventRejectedCertificateSigningRequest;
    }       
    SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent, "MQTT_AGENT" );
}
static void prvMQTTTerminateCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    ESP_LOGI("MQTT_AGENT", "Terminating mqtt agent loop");

}

static void prvCertRevokeIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo )
{
    RenewEventMsg_t nextEvent = { 0 };
    char topic_filter[TOPIC_FILTER_LENGTH] = { 0 };

    (void ) pvIncomingPublishCallbackContext;

    memcpy(topic_filter, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength);

    RenewDataEvent_t * dataBuf = &dataBuffers[0];

    memcpy(dataBuf->data, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
    nextEvent.dataEvent = dataBuf;
    dataBuf->dataLength = pxPublishInfo->payloadLength;  

    if (isAcceptedTopic(topic_filter))
    {
        nextEvent.eventId = RenewEventAcceptedOldCertificateRevoke;
    }
    else
    {
        nextEvent.eventId = RenewEventRejectedOldCertificateRevoke;
    }    
    SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent, "MQTT_AGENT" );
}
bool isAcceptedTopic(char *receivedTopic)
{
    if (strstr(receivedTopic, JOBS_API_SUCCESS) != NULL)
        return true;

    return false;
}

void UpdateStatusRenew(BaseType_t status)
{
    RenewEventMsg_t nextEvent = { 0 };
    nextEvent.eventId = status;
    SendEvent_FreeRTOS(xRenewEventQueue, (void *) &nextEvent, "MQTT_AGENT" );
}