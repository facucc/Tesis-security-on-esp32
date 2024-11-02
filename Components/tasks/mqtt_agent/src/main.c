#include "esp_ota_ops.h"

/* CoreMQTT-Agent APIS for running MQTT in a multithreaded environment. */
#include "freertos_agent_message.h"
#include "freertos_command_pool.h"

/* CoreMQTT-Agent include. */
#include "core_mqtt.h"
#include "core_mqtt_agent.h"

#include "queue_handler.h"
#include "mqtt_agent.h"
#include "mqtt_common.h"
#include "mqtt_onboarding.h"

#include "cert_renew_agent.h"

/* Includes helpers for managing MQTT subscriptions. */
#include "mqtt_subscription_manager.h"

/*Include backoff algorithm header for retry logic.*/
#include "backoff_algorithm.h"

/* Transport interface include. */
#include "network_transport.h"

#include "setup_config.h"

#include "key_value_store.h"


#define AWS_INSECURE_MQTT_PORT 1883
#define AWS_SECURE_MQTT_PORT 8883

#define TOPIC_FORMAT "$aws/things/%s/jobs/%s/update/%s"
#define TOPIC_FORMAT_SIZE 150
#define ACCEPTED "accepted"
#define REJECTED "rejected"

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
#define MQTT_AGENT_NETWORK_BUFFER_SIZE          ( 30000 )

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


#define MQTT_CONNECT_TIMEOUT 50000U

//#define CONNECT_DEBUG
#define AWS_ROOT_CA 1
#define GOOGLE_ROOT_CA 2
#define ONBOARDING true
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

AWSConnectSettings_t AWSConnectSettings = {0};

#if defined(CONNECT_DEBUG) 
    typedef struct TLSFailedSettings { 
        char * certificate;
        char * rootCA;
    } TLSFailedSettings_t;

    static TLSFailedSettings_t TLSFailedSettings = {0};
#endif

UBaseType_t uxHighWaterMark;  
  
static void prvIncomingPublishCallback( MQTTAgentContext_t * pxMqttAgentContext, uint16_t usPacketId, MQTTPublishInfo_t * pxPublishInfo );
static BaseType_t prvConnectToMQTTBroker( NetworkContext_t * pNetworkContext, int maxAttempts );
static void prvNetworkTransportInit( NetworkContext_t * pNetworkContext, TransportInterface_t * xTransport );
static void prvSetConnectionTLS(NetworkContext_t * pNetworkContext);
static MQTTStatus_t prvMqttAgentInit( TransportInterface_t * xTransport );
static MQTTStatus_t prvMQTTConnect( void ); 
static uint32_t prvGetTimeMs( void );
static void prvMQTTAgentProcessing( void );
static void prvCheckFirmware( void );
static void prvPrintRunningPartition( void );

#if defined(CONNECT_DEBUG) 
static inline void prvSetConnectionWithoutTLS(NetworkContext_t * pNetworkContext)
#endif
static void prvSetRootCA(NetworkContext_t * pNetworkContext, int root_ca);
static void prvLoadAWSSettings( bool OnBoarding );
static void prvLoadFailedTLSSettings( void );
static void prvUpdateAWSSettings(NetworkContext_t * pNetworkContext);
static void prvResetAWSCredentials(NetworkContext_t * pNetworkContext);
static uint32_t generateRandomNumber( void );
static void prvNotifyTask();
static bool isUpdateJobs(const char *pTopicName, size_t topicNameLength);

void mqttAgentTask( void * parameters)
{
    uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
    NetworkContext_t xNetworkContext = { 0 };
    TransportInterface_t xTransport  = { 0 };    
    TlsTransportStatus_t xTlsTransportStatus = TLS_TRANSPORT_SUCCESS;
    MQTTStatus_t xMQTTStatus;

    initHardware();
    prvPrintRunningPartition();

    if (IsOnBoardingEnabled() == true)
    {
        ESP_LOGI(TAG, "Onboarding....");
        prvLoadAWSSettings(ONBOARDING);
        prvSetConnectionTLS(&xNetworkContext);
        prvSetRootCA(&xNetworkContext, AWS_ROOT_CA); 
        prvNetworkTransportInit(&xNetworkContext, &xTransport);         
        prvMqttAgentInit(&xTransport);

        ESP_LOGI(TAG, "Establishing MQTT session with claim certificate...to %s:%d", AWSConnectSettings.endpoint, AWS_SECURE_MQTT_PORT);
        
        xTlsTransportStatus = prvConnectToMQTTBroker(&xNetworkContext, 5);

        assert(xTlsTransportStatus == TLS_TRANSPORT_SUCCESS);
        if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS)
        {
            assert(prvMQTTConnect() == MQTTSuccess);
        }
        SubscribeOnBoardingTopic();
        CreateCertificateFromCSR();
        RegisterThingAWS();
        StoreNewCredentials();

        ESP_LOGI(TAG, "Disconnecting from AWS");

        xMQTTStatus = MQTT_Disconnect( &( xGlobalMqttAgentContext.mqttContext ) );

        assert(xMQTTStatus == MQTTSuccess);

        xTlsDisconnect( xTransport.pNetworkContext );

        prvUpdateAWSSettings(xTransport.pNetworkContext);

        ESP_LOGI(TAG, "Establishing MQTT session with new certificate...to %s:%d", AWSConnectSettings.endpoint, AWS_SECURE_MQTT_PORT);
        
        xTlsTransportStatus = prvConnectToMQTTBroker(&xNetworkContext, 5);

        assert(xTlsTransportStatus == TLS_TRANSPORT_SUCCESS);

        if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS)
        {
            assert(prvMQTTConnect() == MQTTSuccess);
        }
        UnSubscribeOnBoardingTopic();       
        DisableOnBoarding();

        ESP_LOGI(TAG, "Successful OnBoarding");
        //uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
        //ESP_LOGI( TAG,"HIGH WATER MARK END: %d\n", uxHighWaterMark);
        
    }
    else
    {
        prvLoadAWSSettings(!ONBOARDING);
        prvSetConnectionTLS(&xNetworkContext);  
        prvSetRootCA(&xNetworkContext, AWS_ROOT_CA);   
        prvNetworkTransportInit(&xNetworkContext, &xTransport);
        prvMqttAgentInit(&xTransport);
        
    #if defined(CONNECT_DEBUG)
        /*
        prvSetConnectionWithoutTLS(&xNetworkContext);
        ESP_LOGI(TAG, "Establishing a non-TLS session to %s:%d",
        AWSConnectSettings.endpoint,
        AWS_INSECURE_MQTT_PORT);

        xTlsTransportStatus = prvConnectToMQTTBroker(&xNetworkContext, 1);

        // Attempt connection with TLS without certificate if the previous method failed
        if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS)
        {
            prvMQTTConnect();            
        }
        xTlsDisconnect(&xNetworkContext);
        */  
        ESP_LOGI(TAG, "Establishing a TLS session with invalid certificate to %s:%d",
                AWSConnectSettings.endpoint,
                AWS_SECURE_MQTT_PORT);

        prvSetConnectionTLS(&xNetworkContext);

        xTlsTransportStatus = prvConnectToMQTTBroker(&xNetworkContext, 1);

        // Attempt connection with TLS without certificate if the previous method failed
        if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS)
        {
            prvMQTTConnect();            
        }
        xTlsDisconnect(&xNetworkContext);
        
        vTaskDelay(pdMS_TO_TICKS(500000));
        
        ESP_LOGI(TAG, "Establishing a TLS session with a different root CA to %s:%d",
                AWSConnectSettings.endpoint,
                AWS_SECURE_MQTT_PORT);
        
        prvSetConnectionTLS(&xNetworkContext);
        prvLoadFailedTLSSettings();
        prvSetRootCA(&xNetworkContext, GOOGLE_ROOT_CA); 

        xTlsTransportStatus = prvConnectToMQTTBroker(&xNetworkContext, 1);

        // Attempt connection with TLS without certificate if the previous method failed
        if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS)
        {
            prvMQTTConnect();            
        }
        xTlsDisconnect(&xNetworkContext);
        //free(TLSFailedSettings.certificate);        
        free(TLSFailedSettings.rootCA);
        prvSetRootCA(&xNetworkContext, AWS_ROOT_CA);

    #endif
        //vTaskDelay(pdMS_TO_TICKS(30000));     
    
        ESP_LOGI(TAG, "Establishing a TLS session to %s:%d",
                    AWSConnectSettings.endpoint,
                    AWS_SECURE_MQTT_PORT );

        xTlsTransportStatus = prvConnectToMQTTBroker(&xNetworkContext, 5);

        assert(xTlsTransportStatus == TLS_TRANSPORT_SUCCESS);

        if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS)
        {
            xMQTTStatus = prvMQTTConnect();
            assert(xMQTTStatus == MQTTSuccess);
        }
    
    }
        
    //ESP_LOGI(TAG, "Testing new version");
    SubscribeToNextJobTopic();
    //exit(0); 
    prvCheckFirmware();
    prvNotifyTask(); 
    prvMQTTAgentProcessing();
       
    while (1)
    {        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void prvNotifyTask()
{
    uint32_t ulValueToSend = 0x01;
    xTaskNotify(xTaskGetHandle("main"), ulValueToSend, eSetValueWithOverwrite);
}

static void prvCheckFirmware( void )
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            {
                ESP_LOGI(TAG, "App is valid, rollback cancelled successfully");
            }
            else
            {
                ESP_LOGE(TAG, "Failed to cancel rollback");
            }
        }
        //prvPrintRunningPartition(running, ota_state);
    }
}
// static void prvPrintRunningPartition( const esp_partition_t * partition, esp_ota_img_states_t ota_state)
// {
//     ESP_LOGI(TAG, "Partition Name: %s", partition->label);
//     ESP_LOGI(TAG, "Ota State 0x%x", ota_state);
//     ESP_LOGI(TAG, "Type: 0x%x", partition->type);
//     ESP_LOGI(TAG, "Subtype: 0x%x", partition->subtype);
//     ESP_LOGI(TAG, "Start Address: 0x%lx", partition->address);
// }

void prvPrintRunningPartition(void)
{
    // Get the currently running partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL)
    {
        ESP_LOGE(TAG, "Failed to get the running partition.");
        return;
    }

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

    if (err != ESP_OK)
    {
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

    switch (ota_state)
    {
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

static void prvMQTTAgentProcessing( void )
{
    MQTTStatus_t xMQTTStatus = MQTTSuccess;
    
    do
    {
        /* MQTTAgent_CommandLoop() is effectively the agent implementation.  It
         * will manage the MQTT protocol until such time that an error occurs,
         * which could be a disconnect.  If an error occurs the MQTT error code
         * is returned and the queue left uncleared so there can be an attempt to
         * clean up and reconnect however the application writer prefers. */

        ESP_LOGI(TAG, "Starting to process commands");        
        xMQTTStatus = MQTTAgent_CommandLoop( &xGlobalMqttAgentContext );

        ESP_LOGE(TAG, "Finishing processing commands %s", MQTT_Status_strerror(xMQTTStatus));

        if( xMQTTStatus == MQTTSuccess )
        {
            xMQTTStatus = MQTT_Disconnect( &( xGlobalMqttAgentContext.mqttContext ) );
            assert(xMQTTStatus == MQTTSuccess);
            xTlsDisconnect( xGlobalMqttAgentContext.mqttContext.transportInterface.pNetworkContext );
            
            prvUpdateAWSSettings(xGlobalMqttAgentContext.mqttContext.transportInterface.pNetworkContext);
            
            ESP_LOGI(TAG, "Establishing MQTT session with new certificate...to %s:%d", AWSConnectSettings.endpoint, AWS_SECURE_MQTT_PORT);

            if (prvConnectToMQTTBroker(xGlobalMqttAgentContext.mqttContext.transportInterface.pNetworkContext, 1) != TLS_TRANSPORT_SUCCESS)
            {
                prvResetAWSCredentials(xGlobalMqttAgentContext.mqttContext.transportInterface.pNetworkContext);
                prvConnectToMQTTBroker(xGlobalMqttAgentContext.mqttContext.transportInterface.pNetworkContext, 5);
                xMQTTStatus = prvMQTTConnect();
                assert(xMQTTStatus == MQTTSuccess);

                UpdateStatusRenew(RenewEventRejectedSignedCertificate);                    
            }
            else
            {
                xMQTTStatus = prvMQTTConnect();
                assert(xMQTTStatus == MQTTSuccess);
                ESP_LOGI(TAG, "Sending event to renew agent");            
                UpdateStatusRenew(RenewEventRevokeOldCertificate); 
            }          
       
        }
        else
        {
            TlsTransportStatus_t xTlsTransportStatus = prvConnectToMQTTBroker(xGlobalMqttAgentContext.mqttContext.transportInterface.pNetworkContext, 5);

            assert(xTlsTransportStatus == TLS_TRANSPORT_SUCCESS);

            if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS)
            {
                xMQTTStatus = prvMQTTConnect();
                assert(xMQTTStatus == MQTTSuccess);
            }
        }
    } while( xMQTTStatus == MQTTSuccess );
}
static void prvLoadAWSSettings( bool OnBoarding )
{    
    size_t itemLength = 0;

    ESP_LOGI(TAG, "Opening AWS Namespace");
    
    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);
    
    AWSConnectSettings.rootCA      = load_value_from_nvs(xHandle, ROOT_CA_NVS_KEY, &itemLength);
    AWSConnectSettings.endpoint    = load_value_from_nvs(xHandle, ENDPOINT_NVS_KEY, &itemLength);
    
    if (OnBoarding)
    {
        AWSConnectSettings.thingName   = GetMacAddress();
        AWSConnectSettings.certificate = load_value_from_nvs(xHandle, CLAIM_CERTIFICATE_NVS_KEY, &itemLength);
        AWSConnectSettings.privateKey  = load_value_from_nvs(xHandle, CLAIM_PRIVATE_KEY_NVS_KEY, &itemLength);
    }
    else
    {
        AWSConnectSettings.thingName   = load_value_from_nvs(xHandle, THING_NAME_NVS_KEY, &itemLength);
        AWSConnectSettings.certificate = load_value_from_nvs(xHandle, CERTIFICATE_NVS_KEY, &itemLength);
        AWSConnectSettings.privateKey  = load_value_from_nvs(xHandle, PRIVATE_KEY_NVS_KEY, &itemLength); 
    }  
    nvs_close(xHandle);
}
#if defined(CONNECT_DEBUG) 
static void prvLoadFailedTLSSettings( void )
{    
    size_t itemLength = 0;
    ESP_LOGI(TAG, "Opening google Namespace");

    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(GOOGLE_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);
    
    TLSFailedSettings.rootCA      = load_value_from_nvs(xHandle, ROOT_CA_NVS_KEY, &itemLength);
    //TLSFailedSettings.certificate = load_value_from_nvs(xHandle, CERTIFICATE_NVS_KEY, &itemLength);

    nvs_close(xHandle);
}
#endif
static void prvResetAWSCredentials(NetworkContext_t * pNetworkContext)
{
    size_t itemLength = 0;
    nvs_handle xHandle;
    ESP_LOGI(TAG, "Opening AWS Namespace");
    
    free(AWSConnectSettings.certificate);
    free(AWSConnectSettings.privateKey);

    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);

    AWSConnectSettings.certificate    = load_value_from_nvs(xHandle, CERTIFICATE_NVS_KEY, &itemLength);
    pNetworkContext->pcClientCert     = AWSConnectSettings.certificate;
    pNetworkContext->pcClientCertSize = strlen(AWSConnectSettings.certificate) + 1;

    AWSConnectSettings.privateKey    = load_value_from_nvs(xHandle, PRIVATE_KEY_NVS_KEY, &itemLength);
    pNetworkContext->pcClientKey     = AWSConnectSettings.privateKey;
    pNetworkContext->pcClientKeySize = strlen(AWSConnectSettings.privateKey) + 1;

    nvs_close(xHandle);
}
static void prvUpdateAWSSettings(NetworkContext_t * pNetworkContext)
{
    if (AWSConnectSettings.certificate != NULL)
    {
        free(AWSConnectSettings.certificate);
        AWSConnectSettings.certificate = NULL;
    }

    if (AWSConnectSettings.privateKey != NULL)
    {
        free(AWSConnectSettings.privateKey);
        AWSConnectSettings.privateKey = NULL;
    }

    ESP_LOGI(TAG, "Updating aws settings");

    AWSConnectSettings.certificate = strdup(AWSConnectSettings.newCertificate);
    pNetworkContext->pcClientCert = AWSConnectSettings.certificate;
    pNetworkContext->pcClientCertSize = strlen(AWSConnectSettings.certificate) + 1;

    AWSConnectSettings.privateKey = strdup(AWSConnectSettings.newPrivateKey);
    pNetworkContext->pcClientKey = AWSConnectSettings.privateKey;
    pNetworkContext->pcClientKeySize = strlen(AWSConnectSettings.privateKey) + 1;

    ESP_LOGI(TAG, "New Certificate:\n%s", pNetworkContext->pcClientCert);

    free(AWSConnectSettings.newCertificate);
    AWSConnectSettings.newCertificate = NULL;

    free(AWSConnectSettings.newPrivateKey);
    AWSConnectSettings.newPrivateKey = NULL;
}
static BaseType_t prvConnectToMQTTBroker( NetworkContext_t * pNetworkContext, int maxAttempts )
{
    BaseType_t xStatus = pdPASS;
    TlsTransportStatus_t xTlsTransportStatus = TLS_TRANSPORT_SUCCESS;
    BackoffAlgorithmContext_t reconnectParams;
    uint16_t nextRetryBackOff = 0U;

    BackoffAlgorithm_InitializeParams( &reconnectParams,
                                       CONNECTION_RETRY_BACKOFF_BASE_MS,
                                       CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                       maxAttempts );

    /* Attempt to connect to MQTT broker. If connection fails, retry after
     * a timeout. Timeout value will exponentially increase until maximum
     * attempts are reached.
     */
    do
    {    
        xTlsTransportStatus = xTlsConnect ( pNetworkContext );        

        if( xTlsTransportStatus != TLS_TRANSPORT_SUCCESS )
        {
            ESP_LOGE(TAG, "Status: %s", TlsTransportStatusToString(xTlsTransportStatus));
            /* Generate a random number and get back-off value (in milliseconds) for the next connection retry. */
            BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &reconnectParams, generateRandomNumber(), &nextRetryBackOff );

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
        else
        {
            ESP_LOGI(TAG, "Status: %s", TlsTransportStatusToString(xTlsTransportStatus));
        }
    } while( ( xTlsTransportStatus != TLS_TRANSPORT_SUCCESS ) && ( xStatus == pdPASS ) );

    return xTlsTransportStatus;
}
static uint32_t generateRandomNumber()
{
    return( rand() );
}
static void prvSetConnectionTLS(NetworkContext_t * pNetworkContext)
{
    pNetworkContext->is_plain_tcp = false; 
    pNetworkContext->xPort = AWS_SECURE_MQTT_PORT;
}
#if defined(CONNECT_DEBUG) 
static inline void prvSetConnectionWithoutTLS(NetworkContext_t * pNetworkContext)
{
    pNetworkContext->is_plain_tcp = true; 
    pNetworkContext->xPort = AWS_INSECURE_MQTT_PORT;
}
#endif
static void prvSetRootCA(NetworkContext_t * pNetworkContext, int root_ca)
{
    switch (root_ca)
    {
    case AWS_ROOT_CA:
        pNetworkContext->pcServerRootCA = AWSConnectSettings.rootCA;
        pNetworkContext->pcServerRootCASize = strlen(AWSConnectSettings.rootCA) + 1;
        break;
    #if defined(CONNECT_DEBUG) 
    case GOOGLE_ROOT_CA:
        pNetworkContext->pcServerRootCA = TLSFailedSettings.rootCA;
        pNetworkContext->pcServerRootCASize = strlen(TLSFailedSettings.rootCA) + 1;
        break;    
    #endif
    default:
        break;
    }
}
static void prvNetworkTransportInit( NetworkContext_t * pNetworkContext, TransportInterface_t * xTransport)
{
    pNetworkContext->pcHostname = AWSConnectSettings.endpoint;
    pNetworkContext->pxTls = NULL;  
    pNetworkContext->xTlsContextSemaphore = xSemaphoreCreateMutexStatic(&xTlsContextSemaphoreBuffer);
    pNetworkContext->disableSni = 0;
    pNetworkContext->pAlpnProtos = NULL;

    /* Initialize credentials for establishing TLS session. */
    pNetworkContext->pcClientCert = AWSConnectSettings.certificate;
    pNetworkContext->pcClientCertSize = strlen(AWSConnectSettings.certificate) + 1;
    pNetworkContext->pcClientKey = AWSConnectSettings.privateKey;
    pNetworkContext->pcClientKeySize = strlen(AWSConnectSettings.privateKey) + 1;

    /* Fill in Transport Interface send and receive function pointers. */
    xTransport->pNetworkContext = pNetworkContext;
    xTransport->send = espTlsTransportSend;
    xTransport->recv = espTlsTransportRecv;

    
    //ESP_LOGI(TAG, "Load Certificate: %s", pNetworkContext->pcClientCert);
    //ESP_LOGI(TAG, "Load Private Key: %s", pNetworkContext->pcClientKey);
    //ESP_LOGI(TAG, "Load ROOTCA: %s. Size %d\n", pNetworkContext->pcServerRootCA, strlen(AWSConnectSettings.rootCA));
    
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
                              (void *) pxGlobalSubscriptionList );

    assert(&xGlobalMqttAgentContext != NULL);
    assert(&xGlobalMqttAgentContext.mqttContext.getTime != NULL);

    return xReturn;
}

static MQTTStatus_t prvMQTTConnect( void )
{
    MQTTConnectInfo_t connectInfo = { 0 };
    MQTTStatus_t xMQTTStatus = MQTTSuccess;
    bool sessionPresent = false;

    assert( &xGlobalMqttAgentContext != NULL );
    
    connectInfo.pClientIdentifier = GetThingName();
    connectInfo.clientIdentifierLength = ( uint16_t ) strlen( GetThingName() );
    
    connectInfo.pUserName = NULL;
    connectInfo.userNameLength = 0U;
    connectInfo.pPassword = NULL;
    connectInfo.passwordLength = 0U;
    connectInfo.keepAliveSeconds = 60U;
    connectInfo.cleanSession = true;

    assert(&xGlobalMqttAgentContext.mqttContext.getTime != NULL);
    
    xMQTTStatus = MQTT_Connect( &( xGlobalMqttAgentContext.mqttContext ),
                               &connectInfo,
                               NULL,
                               MQTT_CONNECT_TIMEOUT,
                               &sessionPresent );
    if (xMQTTStatus != MQTTSuccess)
    {
        ESP_LOGE(TAG, "Connection with MQTT broker failed with status = %s", MQTT_Status_strerror(xMQTTStatus));
        return xMQTTStatus;
    }

    ESP_LOGI(TAG, "MQTT connection successfully established with broker");

    return xMQTTStatus;
}

/*-----------------------------------------------------------*/
/* This function is executed when an incomming publish message */
static void prvIncomingPublishCallback( MQTTAgentContext_t * pxMqttAgentContext,
                                        uint16_t usPacketId,
                                        MQTTPublishInfo_t * pxPublishInfo )
{
    bool xPublishHandled = false;    
    char topicRecived[150] = {0};

    strncpy(topicRecived, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength);

    /* Fan out the incoming publishes to the callbacks registered using
     * subscription manager. */

    ESP_LOGI(TAG,"In the incoming publish callback by the topic: %s", topicRecived);

    assert(( SubscriptionElement_t * ) pxMqttAgentContext->pIncomingCallbackContext != NULL);

    xPublishHandled = SubscriptionManager_HandleIncomingPublishes( ( SubscriptionElement_t * ) pxMqttAgentContext->pIncomingCallbackContext,
                                                                   pxPublishInfo );

    /* If there are no callbacks to handle the incoming publishes,
     * handle it as an unsolicited publish. */
    if( xPublishHandled != true )
    {
        char *pcLocation = ( char * ) &( pxPublishInfo->pTopicName[ pxPublishInfo->topicNameLength ] );
        char cOriginalChar = *pcLocation;
        *pcLocation = 0x00;
        *pcLocation = cOriginalChar;

        if (!isUpdateJobs(pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength))
        {
            ESP_LOGW(TAG, "WARN:  Received an unsolicited publish from topic %s\n", pxPublishInfo->pTopicName );
        }

    }
}

bool isUpdateJobs(const char *pTopicName, size_t topicNameLength)
{    
    char topicAccepted[TOPIC_FORMAT_SIZE] = {0};
    char topicRejected[TOPIC_FORMAT_SIZE] = {0};

    snprintf(topicAccepted, sizeof(topicAccepted), TOPIC_FORMAT, GetThingName(), GetJobId(), ACCEPTED);
    snprintf(topicRejected, sizeof(topicRejected), TOPIC_FORMAT, GetThingName(), GetJobId(), REJECTED);

    ESP_LOGI(TAG, "Topico accepted %s", topicAccepted);
    ESP_LOGI(TAG, "Topico rejected %s", topicRejected);

    if (!strncmp(pTopicName, topicAccepted, topicNameLength))
    {
        ESP_LOGI(TAG, "The topic corresponds to a job update %s\n", topicAccepted);
        return true;
    }  
    else if (!strncmp(pTopicName, topicRejected, topicNameLength))
    {
        ESP_LOGI(TAG, "The topic corresponds to a job update %s\n", topicRejected);
        return true;
    } 
    return false;
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
char * GetThingName()
{
  return AWSConnectSettings.thingName; 
}