/* CoreMQTT-Agent APIS for running MQTT in a multithreaded environment. */
#include "freertos_agent_message.h"
#include "freertos_command_pool.h"


/* CoreMQTT-Agent include. */
#include "core_mqtt_agent.h"

#include "mqtt_agent.h"

/* Includes helpers for managing MQTT subscriptions. */
#include "mqtt_subscription_manager.h"

/*Include backoff algorithm header for retry logic.*/
#include "backoff_algorithm.h"

/* Transport interface include. */
#include "network_transport.h"

#include "key_value_store.h"

#define AWS_NAMESPACE "aws"
/* AWS Connect Settings */
#define CERTIFICATE_NVS_KEY "Certificate"
#define PRIVATE_KEY_NVS_KEY "PrivateKey"
#define ROOT_CA_NVS_KEY "RootCA"
#define ENDPOINT_NVS_KEY "Endpoint"
#define THING_NAME_NVS_KEY "ThingName"
#define AWS_MQTT_PORT 8883


#define MQTT_KEEP_ALIVE_INTERVAL_SECONDS            ( 60U )
/**
 * @brief Timeout for receiving CONNACK packet in milli seconds.
 */
#define CONNACK_RECV_TIMEOUT_MS                     ( 2000U )

/**
 * @brief The maximum number of retries for connecting to server.
 */
#define CONNECTION_RETRY_MAX_ATTEMPTS            ( 5U )

/**
 * @brief The maximum back-off delay (in milliseconds) for retrying connection to server.
 */
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS    ( 5000U )

/**
 * @brief The base back-off delay (in milliseconds) to use for connection retry attempts.
 */
#define CONNECTION_RETRY_BACKOFF_BASE_MS         ( 500U )

/**
 * @brief Dimensions the buffer used to serialise and deserialise MQTT packets.
 * @note Specified in bytes.  Must be large enough to hold the maximum
 * anticipated MQTT payload.
 */
#define MQTT_AGENT_NETWORK_BUFFER_SIZE          ( 5000 )

/**
 * @brief The length of the queue used to hold commands for the agent.
 */
#define MQTT_AGENT_COMMAND_QUEUE_LENGTH         ( 25 )

/**
 * @brief Number of milliseconds in a second.
 */
#define NUM_MILLISECONDS_IN_SECOND                  ( 1000U )

/**
 * @brief Milliseconds per second.
 */
#define MILLISECONDS_PER_SECOND                     ( 1000U )

/**
 * @brief Milliseconds per FreeRTOS tick.
 */
#define MILLISECONDS_PER_TICK                       ( MILLISECONDS_PER_SECOND / configTICK_RATE_HZ )

/**
 * @brief The buffer is used to hold the serialized packets for transmission to
 * and from the transport interface.
 */
static uint8_t pucNetworkBuffer[ MQTT_AGENT_NETWORK_BUFFER_SIZE ];


/**
 * @brief FreeRTOS blocking queue to be used as MQTT Agent context.
 */
static MQTTAgentMessageContext_t xCommandQueue;

/**
 * @brief The interface context used to post commands to the agent.
 * For FreeRTOS it's implemented using a FreeRTOS blocking queue.
 */
static MQTTAgentMessageInterface_t xMessageInterface = {0};
/**
 * @brief The global array of subscription elements.
 *
 * @note The subscription manager implementation expects that the array of the
 * subscription elements used for storing subscriptions to be initialized to 0.
 * As this is a global array, it will be intialized to 0 by default.
 */
static SubscriptionElement_t pxGlobalSubscriptionList[ SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS ];

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the
 * chances of overflow for the 32 bit unsigned integer used for holding the
 * timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

static const char *TAG = "MQTT_AGENT";

TransportInterface_t transport = { 0 };
MQTTAgentContext_t xGlobalMqttAgentContext = {0};
/**
 * @brief Static buffer for TLS Context Semaphore.
 */
static StaticSemaphore_t xTlsContextSemaphoreBuffer;

static AWSConnectSettings_t AWSConnectSettings = {0};

/**
 * @brief Common callback registered with MQTT agent to receive all publish
 * packets. Packets received using the callback is distributed to subscribed
 * topics using subscription manager.
 *
 * @param[in] pxMqttAgentContext MQTT agent context for the connection.
 * @param[in] usPacketId Packet identifier for the packet.
 * @param[in] pPublishInfo MQTT packet information which stores details of the
 * job document.
 */
static void prvIncomingPublishCallback( MQTTAgentContext_t * pxMqttAgentContext,
                                        uint16_t usPacketId,
                                        MQTTPublishInfo_t * pxPublishInfo );

static BaseType_t prvConnectToMQTTBroker( NetworkContext_t * pNetworkContext );
static void prvNetworkTransportInit( NetworkContext_t * pNetworkContext, TransportInterface_t * xTransport );
/**
 * @brief Initializes an MQTT Agent including transport interface and
 * network buffer.
 *
 * @return `MQTTSuccess` if the initialization succeeds, else `MQTTBadParameter`
 */
static MQTTStatus_t prvMqttAgentInit( TransportInterface_t * xTransport );
static MQTTStatus_t prvMQTTConnect( void ); 
static uint32_t prvGetTimeMs( void );
static void prvMQTTAgentProcessing( void );
static void prvLoadAWSSettings( void );
static uint32_t generateRandomNumber( void );

void mqttAgenteTask( void * parameters)
{
    NetworkContext_t xNetworkContext = { 0 };
    TransportInterface_t xTransport  = { 0 };

    prvLoadAWSSettings();    
    prvNetworkTransportInit(&xNetworkContext, &xTransport);
    prvMqttAgentInit(&xTransport);

    prvConnectToMQTTBroker(&xNetworkContext);
    prvMQTTConnect();
    prvMQTTAgentProcessing();

    while (1)
    {
        ESP_LOGI(TAG, "Hello world" );
        vTaskDelay(5000/ portTICK_PERIOD_MS);
    }
}
static void prvMQTTAgentProcessing( )
{
    MQTTStatus_t xMQTTStatus = MQTTSuccess;

    do
    {
        /* MQTTAgent_CommandLoop() is effectively the agent implementation.  It
         * will manage the MQTT protocol until such time that an error occurs,
         * which could be a disconnect.  If an error occurs the MQTT error code
         * is returned and the queue left uncleared so there can be an attempt to
         * clean up and reconnect however the application writer prefers. */
        ESP_LOGI(TAG,"Por procesar mensajes\n");
        xMQTTStatus = MQTTAgent_CommandLoop( &xGlobalMqttAgentContext );
        /* Success is returned for application intiated disconnect or termination.
         * The socket will also be disconnected by the caller. */
        if( xMQTTStatus != MQTTSuccess )
        {
           
        }
    } while( xMQTTStatus != MQTTSuccess );
}
static void prvLoadAWSSettings()
{    
    size_t itemLength = 0;
    ESP_LOGI(TAG, "Opening AWS Namespace");

    nvs_handle handle;
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK);
    
    /* Agregar check de punteros NULL*/
    AWSConnectSettings.certificate = nvs_load_value_if_exist(handle, CERTIFICATE_NVS_KEY, &itemLength);
    AWSConnectSettings.privateKey  = nvs_load_value_if_exist(handle, PRIVATE_KEY_NVS_KEY, &itemLength);
    AWSConnectSettings.rootCA      = nvs_load_value_if_exist(handle, ROOT_CA_NVS_KEY, &itemLength);
    AWSConnectSettings.endpoint    = nvs_load_value_if_exist(handle, ENDPOINT_NVS_KEY, &itemLength);
    AWSConnectSettings.thingName   = nvs_load_value_if_exist(handle, THING_NAME_NVS_KEY, &itemLength);

    nvs_close(handle);
}

static BaseType_t prvConnectToMQTTBroker( NetworkContext_t * pNetworkContext )
{
    BaseType_t xStatus = pdPASS;
    TlsTransportStatus_t xTlsTransportStatus = TLS_TRANSPORT_SUCCESS;
    BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
    BackoffAlgorithmContext_t reconnectParams;
    uint16_t nextRetryBackOff = 0U;

/*
    ESP_LOGI(TAG, "certificate %s\n", AWSConnectSettings.certificate);
    ESP_LOGI(TAG,"privateKey %s\n", AWSConnectSettings.privateKey);
    ESP_LOGI(TAG,"rootCA %s\n", AWSConnectSettings.rootCA);
    ESP_LOGI(TAG,"endpoint %s\n", AWSConnectSettings.endpoint);
    ESP_LOGI(TAG,"thingName %s\n", AWSConnectSettings.thingName);
*/
    // free(AWSConnectSettings.certificate);
    // free(AWSConnectSettings.privateKey);
    // free(AWSConnectSettings.rootCA);
    // free(AWSConnectSettings.endpoint);
    // free(AWSConnectSettings.thingName);

    BackoffAlgorithm_InitializeParams( &reconnectParams,
                                       CONNECTION_RETRY_BACKOFF_BASE_MS,
                                       CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                       CONNECTION_RETRY_MAX_ATTEMPTS );

    /* Attempt to connect to MQTT broker. If connection fails, retry after
     * a timeout. Timeout value will exponentially increase until maximum
     * attempts are reached.
     */
    do
    {
        /* Establish a TLS session with the MQTT broker. This example connects
         * to the MQTT broker as specified in AWS_IOT_ENDPOINT and AWS_MQTT_PORT
         * at the demo config header. */
        ESP_LOGI(TAG, "Establishing a TLS session to %s:%d",
                   AWSConnectSettings.endpoint,
                   AWS_MQTT_PORT );

        /* Attempt to create a mutually authenticated TLS connection. */
        xTlsTransportStatus = xTlsConnect ( pNetworkContext );

        if( xTlsTransportStatus != TLS_TRANSPORT_SUCCESS )
        {
            /* Generate a random number and get back-off value (in milliseconds) for the next connection retry. */
            xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &reconnectParams, generateRandomNumber(), &nextRetryBackOff );

            if( xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted )
            {
                ESP_LOGE(TAG, "All retry attempts have exhausted. Operation will not be retried");
                xStatus = pdFAIL;
            }
            else if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
            {
                ESP_LOGE(TAG, "Connection to the broker failed. Retrying connection "
                           "after %hu ms backoff.",
                           ( unsigned short ) nextRetryBackOff );
                vTaskDelay( pdMS_TO_TICKS(nextRetryBackOff));
                xStatus = pdPASS;
            }
        }
    } while( ( xTlsTransportStatus != TLS_TRANSPORT_SUCCESS ) && ( xStatus == pdPASS ) );


    return xStatus;
}
static uint32_t generateRandomNumber()
{
    return( rand() );
}
static void prvNetworkTransportInit( NetworkContext_t * pNetworkContext, TransportInterface_t * xTransport)
{
    pNetworkContext->pcHostname = AWSConnectSettings.endpoint;
    pNetworkContext->xPort = AWS_MQTT_PORT;
    pNetworkContext->pxTls = NULL;  
    pNetworkContext->xTlsContextSemaphore = xSemaphoreCreateMutexStatic(&xTlsContextSemaphoreBuffer);
    pNetworkContext->disableSni = 0;
    pNetworkContext->pAlpnProtos = NULL;   

/* Initialize credentials for establishing TLS session. */
    pNetworkContext->pcServerRootCA = AWSConnectSettings.rootCA;
    pNetworkContext->pcServerRootCASize = strlen(AWSConnectSettings.rootCA) + 1;
    pNetworkContext->pcClientCert = AWSConnectSettings.certificate;
    pNetworkContext->pcClientCertSize = strlen(AWSConnectSettings.certificate) + 1;
    pNetworkContext->pcClientKey = AWSConnectSettings.privateKey;
    pNetworkContext->pcClientKeySize = strlen(AWSConnectSettings.privateKey) + 1;

    /* Fill in Transport Interface send and receive function pointers. */
    xTransport->pNetworkContext = pNetworkContext;
    xTransport->send = espTlsTransportSend;
    xTransport->recv = espTlsTransportRecv;

}

static MQTTStatus_t prvMqttAgentInit( TransportInterface_t * xTransport )
{    
    MQTTStatus_t xReturn;
    MQTTFixedBuffer_t xFixedBuffer = { .pBuffer = pucNetworkBuffer, .size = MQTT_AGENT_NETWORK_BUFFER_SIZE };
    static uint8_t ucStaticQueueStorageArea[ MQTT_AGENT_COMMAND_QUEUE_LENGTH * sizeof( MQTTAgentCommand_t * ) ];
    static StaticQueue_t xStaticQueueStructure;

    ESP_LOGI(TAG, "Creating command queue.\n" );
    xCommandQueue.queue = xQueueCreateStatic( MQTT_AGENT_COMMAND_QUEUE_LENGTH,
                                              sizeof( MQTTAgentCommand_t * ),
                                              ucStaticQueueStorageArea,
                                              &xStaticQueueStructure );

    /* Initialize the agent task pool. */
    Agent_InitializePool();

    xMessageInterface.pMsgCtx = &xCommandQueue;
    xMessageInterface.recv = Agent_MessageReceive;
    xMessageInterface.send = Agent_MessageSend;
    xMessageInterface.getCommand = Agent_GetCommand;
    xMessageInterface.releaseCommand = Agent_ReleaseCommand;

    /* Initialize MQTT Agent. */
    xReturn = MQTTAgent_Init( &xGlobalMqttAgentContext,
                              &xMessageInterface,
                              &xFixedBuffer,
                              xTransport,
                              prvGetTimeMs,
                              prvIncomingPublishCallback,
                              /* Context to pass into the callback. Passing the pointer to subscription array. */
                              pxGlobalSubscriptionList );

    assert(&xGlobalMqttAgentContext != NULL);
    assert(&xGlobalMqttAgentContext.mqttContext.getTime != NULL);

    return xReturn;
}

static MQTTStatus_t prvMQTTConnect( void )
{
    MQTTConnectInfo_t connectInfo = { 0 };
    MQTTStatus_t mqttStatus = MQTTSuccess;
    bool sessionPresent = false;

    assert( &xGlobalMqttAgentContext != NULL );

    connectInfo.pClientIdentifier = AWSConnectSettings.thingName;
    connectInfo.clientIdentifierLength = ( uint16_t ) strlen( AWSConnectSettings.thingName );
    connectInfo.pUserName = NULL;
    connectInfo.userNameLength = 0U;
    connectInfo.pPassword = NULL;
    connectInfo.passwordLength = 0U;
    connectInfo.keepAliveSeconds = 60U;
    connectInfo.cleanSession = true;

    assert(&xGlobalMqttAgentContext.mqttContext.getTime != NULL);
    
    mqttStatus = MQTT_Connect( &( xGlobalMqttAgentContext.mqttContext ),
                               &connectInfo,
                               NULL,
                               5000U,
                               &sessionPresent );

    return mqttStatus;
}

/*-----------------------------------------------------------*/
/* This function is executed when an incomming publish message */
static void prvIncomingPublishCallback( MQTTAgentContext_t * pxMqttAgentContext,
                                        uint16_t usPacketId,
                                        MQTTPublishInfo_t * pxPublishInfo )
{
    bool xPublishHandled = false;
    char cOriginalChar, * pcLocation;
    char * topic = NULL;
    //size_t topicLength = 0U;
    uint8_t * message = NULL;
    size_t messageLength = 0U;
    ( void ) usPacketId;

    // assert( pxPublishInfo != NULL );
    topic = ( char * ) pxPublishInfo->pTopicName;
    //topicLength = pxPublishInfo->topicNameLength;
    message = ( uint8_t * ) pxPublishInfo->pPayload;
    messageLength = pxPublishInfo->payloadLength;
/*
    LogInfo("TopicName: %s\n", topic);
    LogInfo("Message: %s\n", message);
    LogInfo("MessageLength: %ld\n", messageLength);
*/
    /* Fan out the incoming publishes to the callbacks registered using
     * subscription manager. */

   ESP_LOGI(TAG,"En la funcion incomming publish\n");

    xPublishHandled = SubscriptionManager_HandleIncomingPublishes( ( SubscriptionElement_t * ) pxMqttAgentContext->pIncomingCallbackContext,
                                                                   pxPublishInfo );

    /* If there are no callbacks to handle the incoming publishes,
     * handle it as an unsolicited publish. */
    if( xPublishHandled != true )
    {
        /* Ensure the topic string is terminated for printing.  This will over-
         * write the message ID, which is restored afterwards. */
        pcLocation = ( char * ) &( pxPublishInfo->pTopicName[ pxPublishInfo->topicNameLength ] );
        cOriginalChar = *pcLocation;
        *pcLocation = 0x00;
        ESP_LOGW(TAG, "WARN:  Received an unsolicited publish from topic %s\n", pxPublishInfo->pTopicName );
        *pcLocation = cOriginalChar;
    }
}
void SubscribeCommandCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    bool xSubscriptionAdded = false;
    MQTTAgentCommandContext_t * pxApplicationDefinedContext = ( MQTTAgentCommandContext_t * ) pxCommandContext;
    MQTTAgentSubscribeArgs_t * pxSubscribeArgs = ( MQTTAgentSubscribeArgs_t * ) pxCommandContext->pArgs;
    /* Store the result in the application defined context so the task that
     * initiated the subscribe can check the operation's status.  Also send the
     * status as the notification value.  These things are just done for
     * demonstration purposes. */
    pxApplicationDefinedContext->xReturnStatus = pxReturnInfo->returnCode;

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


/*-----------------------------------------------------------*/

static uint32_t prvGetTimeMs( void )
{
    TickType_t xTickCount = 0;
    uint32_t ulTimeMs = 0UL;

    /* Get the current tick count. */
    xTickCount = xTaskGetTickCount();

    /* Convert the ticks to milliseconds. */
    ulTimeMs = ( uint32_t ) xTickCount * MILLISECONDS_PER_TICK;

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTimeMs = ( uint32_t ) ( ulTimeMs - ulGlobalEntryTimeMs );

    return ulTimeMs;
}
