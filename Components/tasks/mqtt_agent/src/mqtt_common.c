
#include "mqtt_common.h"
#include "mqtt_agent.h"
#include <string.h>
#include "core_mqtt_agent.h"
#include "mqtt_subscription_manager.h"
/**
 * @brief The maximum amount of time in milliseconds to wait for the commands
 * to be posted to the MQTT agent should the MQTT agent's command queue be full.
 * Tasks wait in the Blocked state, so don't use any CPU time.
 */
#define MAX_COMMAND_SEND_BLOCK_TIME_MS         ( 2000 )

/**
 * @brief This demo uses task notifications to signal tasks from MQTT callback
 * functions.  mqttexampleMS_TO_WAIT_FOR_NOTIFICATION defines the time, in ticks,
 * to wait for such a callback.
 */
#define MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION      ( 5000U )

/**
 * @brief The MQTT agent manages the MQTT contexts.  This set the handle to the
 * context used by this task.
 */
extern MQTTAgentContext_t xGlobalMqttAgentContext;
/**
 * @brief The global array of subscription elements.
 *
 * @note The subscription manager implementation expects that the array of the
 * subscription elements used for storing subscriptions to be initialized to 0.
 * As this is a global array, it will be intialized to 0 by default.
 */

extern SubscriptionElement_t pxGlobalSubscriptionList;

static const char * TAG = "MQTT_AGENT";

static void prvMQTTPublishCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );
static void prvMQTTSubscribeCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );

BaseType_t PublishToTopic( const char * pcTopic, uint16_t usTopicLen, const char * pcMsg, uint32_t ulMsgSize, MQTTQoS_t  xQoS, const char * TASK)
{
    BaseType_t xOtaMqttStatus = 0;
    MQTTStatus_t xCommandStatus;
    MQTTAgentCommandInfo_t xCommandParams = { 0 };
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

    xCommandParams.blockTimeMs = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandParams.cmdCompleteCallback = prvMQTTPublishCompleteCallback;
    xCommandParams.pCmdCompleteCallbackContext = &xCommandContext;
    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs = NULL;
    xCommandContext.xReturnStatus = MQTTSendFailed;

    xCommandStatus = MQTTAgent_Publish( &xGlobalMqttAgentContext, &xPublishInfo, &xCommandParams );
    configASSERT( xCommandStatus == MQTTSuccess );

    xTaskNotifyWait( 0,
                     0,
                     NULL,
                     pdMS_TO_TICKS( MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION ) );

    if( xCommandContext.xReturnStatus != MQTTSuccess )
    {
        ESP_LOGE(TASK, "Failed to send PUBLISH packet to broker with error = %u.", xCommandContext.xReturnStatus );
        xOtaMqttStatus = 1;
    }
    else
    {
        ESP_LOGI(TASK, "Sent PUBLISH packet to broker %.*s to broker.\n\n",
                   usTopicLen,
                   pcTopic );
    }
    ESP_LOGI(TASK, "Paquete request %s\n", pcMsg);
    return xOtaMqttStatus;
}

BaseType_t SubscribeToTopic( const char * pcTopicFilter, uint16_t usTopicFilterLength, MQTTQoS_t  xQoS, void * IncomingPublishCallback, const char * TASK)
{
    BaseType_t xOtaMqttStatus = 0;
    MQTTStatus_t xCommandStatus;
    MQTTAgentCommandInfo_t xCommandParams = { 0 };
    MQTTAgentCommandContext_t xCommandContext;
    MQTTSubscribeInfo_t xSubscription;
    MQTTAgentSubscribeArgs_t xSubscribeArgs = { 0 };

    memset( &( xCommandContext ), 0, sizeof( MQTTAgentCommandContext_t ) );
    memset( &( xSubscription ), 0, sizeof( MQTTSubscribeInfo_t ) );

    assert( pcTopicFilter != NULL );
    assert( usTopicFilterLength > 0 );

    xSubscription.qos = xQoS;
    xSubscription.pTopicFilter = pcTopicFilter;
    xSubscription.topicFilterLength = usTopicFilterLength;
    xSubscribeArgs.numSubscriptions = 1;
    xSubscribeArgs.pSubscribeInfo = &xSubscription;


    xCommandParams.blockTimeMs = MAX_COMMAND_SEND_BLOCK_TIME_MS;
    xCommandParams.cmdCompleteCallback = prvMQTTSubscribeCompleteCallback;
    xCommandParams.pCmdCompleteCallbackContext = &xCommandContext;

    xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();
    xCommandContext.pArgs = &xSubscribeArgs;
    xCommandContext.xReturnStatus = MQTTSendFailed;
    xCommandContext.pxIncomingPublishCallback = IncomingPublishCallback;


    xCommandStatus = MQTTAgent_Subscribe( &xGlobalMqttAgentContext, &xSubscribeArgs, &xCommandParams );
    configASSERT( xCommandStatus == MQTTSuccess );

    xTaskNotifyWait( 0,
                     0,
                     NULL,
                     pdMS_TO_TICKS( MQTT_AGENT_MS_TO_WAIT_FOR_NOTIFICATION ) );

    if( xCommandContext.xReturnStatus != MQTTSuccess )
    {
        ESP_LOGE(TASK, "Failed to send SUBSCRIBE packet to broker with error = %u.", xCommandContext.xReturnStatus);
        xOtaMqttStatus = 1;
    }
    else
    {
        ESP_LOGI(TASK, "SUBSCRIBED to topic %.*s to broker.\n\n",
                   usTopicFilterLength,
                   pcTopicFilter );
    }

    return xOtaMqttStatus;
}
static void prvMQTTPublishCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    /* Store the result in the application defined context so the task that
     * initiated the publish can check the operation's status. */
    pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

    if( pxCommandContext->xTaskToNotify != NULL )
    {
        /* Send the context's ulNotificationValue as the notification value so
         * the receiving task can check the value it set in the context matches
         * the value it receives in the notification. */
        xTaskNotify( pxCommandContext->xTaskToNotify,
                     pxCommandContext->ulNotificationValue,
                     eSetValueWithOverwrite );
    }
}
static void prvMQTTSubscribeCompleteCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    bool xSubscriptionAdded = false;
    MQTTAgentCommandContext_t * pxApplicationDefinedContext = ( MQTTAgentCommandContext_t * ) pxCommandContext;
    MQTTAgentSubscribeArgs_t * pxSubscribeArgs = ( MQTTAgentSubscribeArgs_t * ) pxCommandContext->pArgs;
    /* Store the result in the application defined context so the task that
     * initiated the subscribe can check the operation's status.  Also send the
     * status as the notification value.  These things are just done for
     * demonstration purposes. */
    pxApplicationDefinedContext->xReturnStatus = pxReturnInfo->returnCode;
    ESP_LOGI(TAG,"En la funcion prvMQTTSubscribeCompleteCallback\n");
    /* Check if the subscribe operation is a success. Only one topic is
     * subscribed by this demo. */
    if( pxReturnInfo->returnCode == MQTTSuccess )
    {
        
        /* Add subscription so that incoming publishes are routed to the application
         * callback. */
        xSubscriptionAdded = SubscriptionManager_AddSubscription( ( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                                              pxSubscribeArgs->pSubscribeInfo->pTopicFilter,
                                              pxSubscribeArgs->pSubscribeInfo->topicFilterLength,
                                              pxApplicationDefinedContext->pxIncomingPublishCallback,
                                              NULL );

        if( xSubscriptionAdded == false )
        {
            ESP_LOGI(TAG, "Failed to register an incoming publish callback for topic %.*s.", pxSubscribeArgs->pSubscribeInfo->topicFilterLength, pxSubscribeArgs->pSubscribeInfo->pTopicFilter );
        }
        else 
            ESP_LOGI(TAG,"Successful subscription\n");
        
    }

    xTaskNotify( pxCommandContext->xTaskToNotify,
                 ( uint32_t ) ( pxReturnInfo->returnCode ),
                 eSetValueWithOverwrite );
}