#include <string.h>
#include "jobs.h"
#include "core_mqtt_agent.h"
#include "core_json.h"
#include "fleet_provisioning.h"

#include "mqtt_subscription_manager.h"
#include "mqtt_common.h"
#include "mqtt_agent.h"
#include "ota_agent.h"
#include "cert_renew_agent.h"

#define MAX_COMMAND_SEND_BLOCK_TIME_MS (2000)
#define MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION (10000U)
extern MQTTAgentContext_t xGlobalMqttAgentContext;
extern SubscriptionElement_t pxGlobalSubscriptionList;

extern QueueHandle_t xRenewEventQueue;
extern QueueHandle_t xOtaEventQueue; 

static const char * TAG = "MQTT_AGENT";

char * topic_filter = NULL;

static JobEventData_t jobBuffers[1] = { 0 };
char globalJobId[ JOB_ID_LENGTH ] = { 0 };

#define JOBS_NOTIFY_NEXT_TOPIC "$aws/things/%s/jobs/notify-next"

static void prvMQTTPublishCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );
static void prvMQTTSubscribeCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );
static void prvMQTTUnSubscribeCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );
static void prvIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo );
static bool prIsFreeRTOSOtaJob( const char * jobDoc, size_t jobDocLength );
static bool prIsCertRenewalJob(const char * jobDoc, size_t jobDocLength);
static void prvSendOTAJobDocument(JobEventData_t * jobDocument);
static void prvSendRenewJobDocument(JobEventData_t * jobDocument);

BaseType_t PublishToTopic( const char * pcTopic, uint16_t usTopicLen, const char * pcMsg, uint32_t ulMsgSize, MQTTQoS_t  xQoS, const char * TASK)
{
    BaseType_t xMqttStatus = 0;
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = { 0 };
    MQTTAgentCommandContext_t xCommandContext;
    MQTTPublishInfo_t xPublishInfo;

    memset( &( xCommandContext ), 0, sizeof( MQTTAgentCommandContext_t ) );
    memset( &( xPublishInfo ), 0, sizeof( MQTTPublishInfo_t ) );

    /* Set the required publish parameters. */
    xPublishInfo.pTopicName = pcTopic;
    xPublishInfo.topicNameLength = usTopicLen;
    xPublishInfo.qos = xQoS;
    xPublishInfo.pPayload = pcMsg;
    xPublishInfo.payloadLength = ulMsgSize;

    xCommandInformation.blockTimeMs = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandInformation.cmdCompleteCallback = prvMQTTPublishCompleteCallback;
    xCommandInformation.pCmdCompleteCallbackContext = &xCommandContext;
    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs = NULL;
    xCommandContext.xReturnStatus = MQTTSendFailed;

    xCommandAdded = MQTTAgent_Publish( &xGlobalMqttAgentContext, &xPublishInfo, &xCommandInformation );
    //configASSERT( xCommandAdded == MQTTSuccess );

    if (xCommandAdded == MQTTSuccess)
    {    
        xTaskNotifyWait( 0,
                         0,
                         NULL,
                         portMAX_DELAY );

        if( xCommandContext.xReturnStatus != MQTTSuccess )
        {
            ESP_LOGE(TASK, "Failed to send publish packet to broker with error = %s.", MQTT_Status_strerror(xCommandContext.xReturnStatus));
        }
        else
        {
            ESP_LOGI(TASK, "Sent publish packet to broker %.*s to broker.",
                    usTopicLen,
                    pcTopic );
            //ESP_LOGI(TASK, "Publish packet = %s", pcMsg);
        }
    }
    
    return xMqttStatus;
}

BaseType_t SubscribeToTopic( MQTTAgentSubscribeArgs_t * pcSubsTopics, void * IncomingPublishCallback, const char * TASK)
{
    BaseType_t xMqttStatus = 0;
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = { 0 };
    MQTTAgentCommandContext_t xCommandContext;

    memset( &( xCommandContext ), 0, sizeof( MQTTAgentCommandContext_t ) );

    xCommandInformation.blockTimeMs = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandInformation.cmdCompleteCallback = prvMQTTSubscribeCompleteCallback;
    xCommandInformation.pCmdCompleteCallbackContext = &xCommandContext;

    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs = pcSubsTopics;
    xCommandContext.xReturnStatus = MQTTSendFailed;
    xCommandContext.pxIncomingPublishCallback = IncomingPublishCallback;

    xCommandAdded = MQTTAgent_Subscribe( &xGlobalMqttAgentContext, pcSubsTopics, &xCommandInformation );
    
    configASSERT( xCommandAdded == MQTTSuccess );

    xTaskNotifyWait( 0,
                     0,
                     NULL,
                     pdMS_TO_TICKS( MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION ) );

    if( xCommandContext.xReturnStatus != MQTTSuccess )
    {
        ESP_LOGE(TASK, "Failed to send subscribe packet to broker with error = %s.", MQTT_Status_strerror(xCommandAdded));
        xMqttStatus = 1;
    }
    else
    {
        ESP_LOGI(TASK, "Subscribe topics to broker"); 
    }

    return xMqttStatus;
}
BaseType_t UnSubscribeToTopic( MQTTAgentSubscribeArgs_t * pcSubsTopics, const char * TASK)
{
    BaseType_t xMqttStatus = 0;
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = { 0 };
    MQTTAgentCommandContext_t xCommandContext;

    memset( &( xCommandContext ), 0, sizeof( MQTTAgentCommandContext_t ) );

    xCommandInformation.blockTimeMs = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandInformation.cmdCompleteCallback = prvMQTTUnSubscribeCompleteCallback;
    xCommandInformation.pCmdCompleteCallbackContext = &xCommandContext;

    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs = pcSubsTopics;
    xCommandContext.xReturnStatus = MQTTSendFailed;
    xCommandContext.pxIncomingPublishCallback = NULL;

    xCommandAdded = MQTTAgent_Unsubscribe( &xGlobalMqttAgentContext, pcSubsTopics, &xCommandInformation );
    
    configASSERT( xCommandAdded == MQTTSuccess );

    xTaskNotifyWait( 0,
                     0,
                     NULL,
                     pdMS_TO_TICKS( MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION ) );

    if( xCommandContext.xReturnStatus != MQTTSuccess )
    {
        ESP_LOGE(TASK, "Failed to send UnSubscribe packet to broker with error = %s.", MQTT_Status_strerror(xCommandAdded));
        xMqttStatus = 1;
    }
    else
    {
        ESP_LOGI(TASK, "UnSubscribe topics to broker");
    }

    return xMqttStatus;
}
static void prvMQTTPublishCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

//    ESP_LOGI(pcTaskGetName(xTaskGetCurrentTaskHandle()), "In MQTTPublishCompleteCallback %s", MQTT_Status_strerror(pxReturnInfo->returnCode));

    if( pxCommandContext->xTaskToNotify != NULL )
    {
        xTaskNotify( pxCommandContext->xTaskToNotify,
                     pxCommandContext->ulNotificationValue,
                     eSetValueWithOverwrite );
    }
}
static void prvMQTTSubscribeCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    MQTTAgentCommandContext_t * pxApplicationDefinedContext = ( MQTTAgentCommandContext_t * ) pxCommandContext;
    MQTTAgentSubscribeArgs_t * pxSubscribeArgs = ( MQTTAgentSubscribeArgs_t * ) pxCommandContext->pArgs;

    pxApplicationDefinedContext->xReturnStatus = pxReturnInfo->returnCode;
    ESP_LOGI(TAG,"In the MQTT Subscribe Complete Callback\n");

    if( pxReturnInfo->returnCode == MQTTSuccess )
    {        
        /* Add subscription so that incoming publishes are routed to the application callback. */
        for (size_t i = 0; i < pxSubscribeArgs->numSubscriptions; i++)
        {
            bool xSubscriptionAdded = SubscriptionManager_AddSubscription( ( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                                                pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter,
                                                pxSubscribeArgs->pSubscribeInfo[i].topicFilterLength,
                                                pxApplicationDefinedContext->pxIncomingPublishCallback,
                                                NULL );

            if( xSubscriptionAdded == false )
            {
                ESP_LOGI(TAG, "Failed to register an incoming publish callback for topic %.*s.", pxSubscribeArgs->pSubscribeInfo[i].topicFilterLength, pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter );
            }
            else 
                ESP_LOGI(TAG,"Successful subscription %s\n", pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter);

                        ESP_LOGE(TAG, "Topic added: %s",
                     ((SubscriptionElement_t *)xGlobalMqttAgentContext.pIncomingCallbackContext)[i].pcSubscriptionFilterString);
        }

        
    }

    xTaskNotify( pxCommandContext->xTaskToNotify,
                 ( uint32_t ) ( pxReturnInfo->returnCode ),
                 eSetValueWithOverwrite );
}
static void prvMQTTUnSubscribeCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    MQTTAgentCommandContext_t * pxApplicationDefinedContext = ( MQTTAgentCommandContext_t * ) pxCommandContext;
    MQTTAgentSubscribeArgs_t * pxSubscribeArgs = ( MQTTAgentSubscribeArgs_t * ) pxCommandContext->pArgs;

    pxApplicationDefinedContext->xReturnStatus = pxReturnInfo->returnCode;
    
    ESP_LOGI(TAG,"In the unsubscribe complete callback");

    if( pxReturnInfo->returnCode == MQTTSuccess )
    {
        for (size_t i = 0; i < pxSubscribeArgs->numSubscriptions; i++)
        {
            SubscriptionManager_RemoveSubscription( ( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                                                    pxSubscribeArgs->pSubscribeInfo[i].pTopicFilter,
                                                    pxSubscribeArgs->pSubscribeInfo[i].topicFilterLength);
        }        
    }
    xTaskNotify( pxCommandContext->xTaskToNotify,
                 ( uint32_t ) ( pxReturnInfo->returnCode ),
                 eSetValueWithOverwrite );
}
MQTTStatus_t TerminateMQTTAgent(void * IncomingPublishCallback, const char * TASK)
{
    MQTTStatus_t xCommandAdded;
    MQTTAgentCommandInfo_t xCommandInformation = { 0 };

    xCommandInformation.cmdCompleteCallback = IncomingPublishCallback;
    xCommandInformation.blockTimeMs = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    
    xCommandAdded = MQTTAgent_Terminate( &xGlobalMqttAgentContext, &xCommandInformation );

    configASSERT( xCommandAdded == MQTTSuccess );

    return xCommandAdded;     

}
MQTTStatus_t SubscribeToNextJobTopic()
{
    MQTTStatus_t xStatus;
    uint16_t xPacketId;
    bool xSubscriptionAdded = false;
    MQTTSubscribeInfo_t subscriptionList = { 0 };
    
    topic_filter = (char *) calloc(TOPIC_FILTER_LENGTH, sizeof(char));

    sprintf(topic_filter, JOBS_NOTIFY_NEXT_TOPIC, GetThingName());

    subscriptionList.qos = MQTTQoS0;
    subscriptionList.pTopicFilter = topic_filter;
    subscriptionList.topicFilterLength = strlen( topic_filter);

    ESP_LOGI(TAG, "Subscribing to the topic %s", topic_filter);

    xPacketId = MQTT_GetPacketId( &( xGlobalMqttAgentContext.mqttContext ) );
    xStatus = MQTT_Subscribe( &( xGlobalMqttAgentContext.mqttContext ), &subscriptionList, 1, xPacketId ) ;
    
    assert(xStatus == MQTTSuccess );
    
    xSubscriptionAdded = SubscriptionManager_AddSubscription( ( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                                            subscriptionList.pTopicFilter,
                                            subscriptionList.topicFilterLength,
                                            &prvIncomingPublishCallback,
                                            NULL );

    if( xSubscriptionAdded == false )
    {
        ESP_LOGI(TAG, "Failed to register an incoming publish callback for topic %.*s.", subscriptionList.topicFilterLength, subscriptionList.pTopicFilter );
    }
    else 
        ESP_LOGI(TAG,"Successful subscription\n");


    if( xStatus == MQTTSuccess )
    {
        xStatus = WaitForPacketAck( &( xGlobalMqttAgentContext.mqttContext ),
                                       xPacketId,
                                       1000 );
        assert(xStatus == MQTTSuccess);
    }    

    return xStatus;
}
static void prvIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo )
{
    char * jobDoc;
    const char * jobId;
    size_t jobIdLength = 0U;    
    JobEventData_t jobDocument = {0};    

    //ESP_LOGI("MQTT_AGENT", "Handling Incoming MQTT message");

    (void ) pvIncomingPublishCallbackContext;
    
    jobIdLength = Jobs_GetJobId((const char *) pxPublishInfo->pPayload, pxPublishInfo->payloadLength, &jobId);
    
    if ( jobIdLength > 0 )
    {
        size_t jobDocLength = Jobs_GetJobDocument( (const char *) pxPublishInfo->pPayload, pxPublishInfo->payloadLength, (const char **) &jobDoc );

        if( jobDocLength != 0U )
        {
            strncpy(jobDocument.jobData, jobDoc, jobDocLength);
            strncpy(jobDocument.jobId, jobId, jobIdLength);

            jobDocument.jobDataLength = jobDocLength;

            ESP_LOGI(TAG, "JobDocument: %s\n", jobDocument.jobData);

            if (prIsFreeRTOSOtaJob(jobDoc, jobDocLength))
            {
                prvSendOTAJobDocument(&jobDocument);
            }
            else if(prIsCertRenewalJob(jobDoc, jobDocLength))
            {
                prvSendRenewJobDocument(&jobDocument);
            }
            else
            {
                ESP_LOGE(TAG, "JobDocument failed");
            }
        }
        else
        {
            ESP_LOGE(TAG, "JobDocument received failed");
        }
    }
}
static void prvSendOTAJobDocument(JobEventData_t * jobDocument)
{
    OtaEventMsg_t nextEvent = { 0 };  
    nextEvent.eventId = OtaEventReceivedJobDocument;

    memcpy(jobBuffers, jobDocument, sizeof(JobEventData_t));

    jobBuffers->jobDataLength = jobDocument->jobDataLength;

    nextEvent.jobEvent = &jobBuffers[0];

    SendEvent_FreeRTOS(xOtaEventQueue, (void *) &nextEvent, TAG );
}
static void prvSendRenewJobDocument(JobEventData_t * jobDocument)
{
    RenewEventMsg_t nextEvent = { 0 };
    nextEvent.eventId = RenewEventReceivedJobDocument;

    memcpy(jobBuffers, jobDocument, sizeof(JobEventData_t));

    jobBuffers->jobDataLength = jobDocument->jobDataLength;

    nextEvent.jobEvent = &jobBuffers[0];

    SendEvent_FreeRTOS(xRenewEventQueue, (void *)&nextEvent, TAG);
}
static bool prIsFreeRTOSOtaJob( const char * jobDoc, size_t jobDocLength )
{
    JSONStatus_t jsonResult = JSONNotFound;
    const char * afrOtaDocHeader;
    size_t afrOtaDocHeaderLength = 0U;

    /* FreeRTOS OTA updates have a top level "afr_ota" job document key.
    * Check for this to ensure the document is an FreeRTOS OTA update */
    jsonResult = JSON_SearchConst( jobDoc,
                                      jobDocLength,
                                      "afr_ota",
                                      7U,
                                      &afrOtaDocHeader,
                                      &afrOtaDocHeaderLength,
                                      NULL );

    return( JSONSuccess == jsonResult );
}
void SendUpdateForJob(JobCurrentStatus_t pcJobStatus, const char * pcJobStatusMsg )
{
    JobsStatus_t xStatus = JobsSuccess;
    JobsUpdateRequest_t jobUpdateRequest = {0};    
    char pUpdateJobTopic[JOBS_API_MAX_LENGTH(strlen(GetThingName()))];
    size_t ulTopicLength = 0U;

    xStatus = Jobs_Update( pUpdateJobTopic,
                           sizeof( pUpdateJobTopic ),
                           GetThingName(),
                           strlen(GetThingName()),
                           GetJobId(),
                           strlen(GetJobId()),
                           &ulTopicLength );

    if( xStatus == JobsSuccess )
    {    
        jobUpdateRequest.status = pcJobStatus;

        if (pcJobStatusMsg != NULL)
        {
            jobUpdateRequest.statusDetails = pcJobStatusMsg;
            jobUpdateRequest.statusDetailsLength = strlen( pcJobStatusMsg );
        }
        char messageBuffer[UPDATE_REQUEST_SIZE]  = {0};
        size_t messageLength = Jobs_UpdateMsg( jobUpdateRequest, messageBuffer, JOB_UPDATE_SIZE );

        if (messageLength > 0 )
        {
            PublishToTopic(pUpdateJobTopic,
                           strlen(pUpdateJobTopic),
                           messageBuffer,
                           strlen(messageBuffer),
                           MQTTQoS0,
                           TAG);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to generate job Update Request");
        }
        
    }
    else
    {
        ESP_LOGE(TAG, "Failed to generate Publish topic string for sending job update: %s", GetJobId());
    }
}
static bool prIsCertRenewalJob(const char * jobDoc, size_t jobDocLength)
{
    JSONStatus_t jsonResult = JSONNotFound;
    const char * jsonValue = NULL;
    size_t jsonValueLength = 0U;

    jsonResult = JSON_SearchConst( jobDoc,
                                   jobDocLength,
                                   "operation",
                                   9U,
                                   &jsonValue,
                                   &jsonValueLength,
                                   NULL );

    return( JSONSuccess == jsonResult );
}

MQTTStatus_t WaitForPacketAck( MQTTContext_t * pMqttContext,
                              uint16_t usPacketIdentifier,
                              uint32_t ulTimeout )
{
    uint32_t ulMqttProcessLoopEntryTime;
    uint32_t ulMqttProcessLoopTimeoutTime;
    uint32_t ulCurrentTime;

    MQTTStatus_t xMqttStatus = MQTTSuccess;

    ulCurrentTime = pMqttContext->getTime();
    ulMqttProcessLoopEntryTime = ulCurrentTime;
    ulMqttProcessLoopTimeoutTime = ulCurrentTime + ulTimeout;

    /* Call MQTT_ProcessLoop multiple times until the expected packet ACK
     * is received, a timeout happens, or MQTT_ProcessLoop fails. */
    while( ( ulCurrentTime < ulMqttProcessLoopTimeoutTime ) &&
           ( xMqttStatus == MQTTSuccess || xMqttStatus == MQTTNeedMoreBytes ) )
    {
        xMqttStatus = MQTT_ProcessLoop( pMqttContext );
        ulCurrentTime = pMqttContext->getTime();
    }

    if( ( xMqttStatus != MQTTSuccess ) && ( xMqttStatus != MQTTNeedMoreBytes ) )
    {
        ESP_LOGE(TAG, "MQTT_ProcessLoop failed to receive ACK packet: Expected ACK Packet ID=%02X, LoopDuration=%"PRIu32", Status=%s",
                    usPacketIdentifier,
                    ( ulCurrentTime - ulMqttProcessLoopEntryTime ),
                    MQTT_Status_strerror( xMqttStatus ));
    }

    return xMqttStatus;
}
/* Helper functions*/
void ReplaceEscapedNewlines(char *str)
{
    char *read = str;
    char *write = str;

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

void SetJobId(const char *jobId)
{
    strncpy(globalJobId, jobId, JOB_ID_LENGTH);
}

const char * GetJobId()
{
    return globalJobId;
}