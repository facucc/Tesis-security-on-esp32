#include "mqtt_connection.h"

/* Includes helpers for managing MQTT subscriptions. */
#include "mqtt_subscription_manager.h"

/*Include backoff algorithm header for retry logic.*/
#include "backoff_algorithm.h"

#define TOPIC_FORMAT      "$aws/things/%s/jobs/%s/update%s"
#define TOPIC_FORMAT_SIZE 150

/* The maximum back-off delay (in milliseconds) for retrying connection to server. */
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS 5000U

/* The base back-off delay (in milliseconds) to use for connection retry attempts. */
#define CONNECTION_RETRY_BACKOFF_BASE_MS 500U

/*
 * Dimensions the buffer used to serialise and deserialise MQTT packets.
 * Specified in bytes.  Must be large enough to hold the maximum
 * anticipated MQTT payload.
 */
#define MQTT_AGENT_NETWORK_BUFFER_SIZE 30000

/* The length of the queue used to hold commands for the agent. */
#define MQTT_AGENT_COMMAND_QUEUE_LENGTH 25
#define MILLISECONDS_PER_SECOND         1000U
#define MILLISECONDS_PER_TICK           (MILLISECONDS_PER_SECOND / configTICK_RATE_HZ)
#define MQTT_CONNECT_TIMEOUT            50000U

/*
 * The buffer is used to hold the serialized packets for transmission to
 * and from the transport interface.
 */
static uint8_t pucNetworkBuffer[MQTT_AGENT_NETWORK_BUFFER_SIZE];

/* FreeRTOS blocking queue to be used as MQTT Agent context. */
static MQTTAgentMessageContext_t xCommandQueue;

/*
 * The interface context used to post commands to the agent.
 * For FreeRTOS it's implemented using a FreeRTOS blocking queue.
 */
static MQTTAgentMessageInterface_t xMessageInterface = {0};

/* The global array of subscription elements. */
static SubscriptionElement_t globalSubscriptionList[SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS];

/* TLS Context Semaphore. */
static StaticSemaphore_t xTlsContextSemaphoreBuffer;

#if defined(CONNECTION_TEST)
typedef struct TLSFailedSettings {
    char* certificate;
    char* rootCA;
} TLSFailedSettings_t;

static TLSFailedSettings_t TLSFailedSettings = {0};
#endif

/*
 * Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the
 * chances of overflow for the 32 bit unsigned integer used for holding the
 * timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

extern AWSConnectSettings_t AWSConnectSettings;
extern MQTTAgentContext_t globalMqttAgentContext;

static const char* TAG = "MQTT_AGENT";

static void prvIncomingPublishCallback(MQTTAgentContext_t* pxMqttAgentContext,
                                       uint16_t usPacketId,
                                       MQTTPublishInfo_t* pxPublishInfo);

#if defined(CONNECTION_TEST)
static void prvTestConnections(void);
#endif
static bool isUpdateJobs(const char* pTopicName, size_t topicNameLength);
static uint32_t prvGetTimeMs(void);

void NetworkTransportInit(NetworkContext_t* pNetworkContext, TransportInterface_t* pTransport)
{
    pNetworkContext->pcHostname           = AWSConnectSettings.endpoint;
    pNetworkContext->pxTls                = NULL;
    pNetworkContext->xTlsContextSemaphore = xSemaphoreCreateMutexStatic(&xTlsContextSemaphoreBuffer);
    pNetworkContext->disableSni           = 0;
    pNetworkContext->pAlpnProtos          = NULL;

    /* Initialize credentials for establishing TLS session. */
    pNetworkContext->pcClientCert     = AWSConnectSettings.certificate;
    pNetworkContext->pcClientCertSize = strlen(AWSConnectSettings.certificate) + 1;
    pNetworkContext->pcClientKey      = AWSConnectSettings.privateKey;
    pNetworkContext->pcClientKeySize  = strlen(AWSConnectSettings.privateKey) + 1;

    /* Fill in Transport Interface send and receive function pointers. */
    pTransport->pNetworkContext = pNetworkContext;
    pTransport->send            = espTlsTransportSend;
    pTransport->recv            = espTlsTransportRecv;
}

MQTTStatus_t MQTTAgentInit(TransportInterface_t* pTransport)
{
    MQTTStatus_t xReturn;
    MQTTFixedBuffer_t xFixedBuffer = {.pBuffer = pucNetworkBuffer, .size = MQTT_AGENT_NETWORK_BUFFER_SIZE};
    static uint8_t ucStaticQueueStorageArea[MQTT_AGENT_COMMAND_QUEUE_LENGTH * sizeof(MQTTAgentCommand_t*)];
    static StaticQueue_t xStaticQueueStructure;

    ESP_LOGI(TAG, "Creating command queue.\n");
    xCommandQueue.queue = xQueueCreateStatic(MQTT_AGENT_COMMAND_QUEUE_LENGTH,
                                             sizeof(MQTTAgentCommand_t*),
                                             ucStaticQueueStorageArea,
                                             &xStaticQueueStructure);

    /* Initialize the agent task pool. */
    Agent_InitializePool();

    xMessageInterface.pMsgCtx        = &xCommandQueue;
    xMessageInterface.recv           = Agent_MessageReceive;
    xMessageInterface.send           = Agent_MessageSend;
    xMessageInterface.getCommand     = Agent_GetCommand;
    xMessageInterface.releaseCommand = Agent_ReleaseCommand;

    /* Initialize MQTT Agent. */
    xReturn = MQTTAgent_Init(&globalMqttAgentContext,
                             &xMessageInterface,
                             &xFixedBuffer,
                             pTransport,
                             prvGetTimeMs,
                             prvIncomingPublishCallback,
                             /* Context to pass into the callback. Passing the pointer to subscription array. */
                             (void*)globalSubscriptionList);

    assert(&globalMqttAgentContext != NULL);
    assert(&globalMqttAgentContext.mqttContext.getTime != NULL);

    return xReturn;
}

/* Sends an MQTT Connect packet over the already connected TCP socket. */
MQTTStatus_t MQTTConnect(void)
{
    MQTTConnectInfo_t connectInfo = {0};
    MQTTStatus_t xMQTTStatus      = MQTTSuccess;
    bool sessionPresent           = false;

    assert(&globalMqttAgentContext != NULL);

    connectInfo.pClientIdentifier      = GetThingName();
    connectInfo.clientIdentifierLength = (uint16_t)strlen(GetThingName());

    connectInfo.pUserName        = NULL;
    connectInfo.userNameLength   = 0U;
    connectInfo.pPassword        = NULL;
    connectInfo.passwordLength   = 0U;
    connectInfo.keepAliveSeconds = 60U;
    connectInfo.cleanSession     = true;

    assert(&globalMqttAgentContext.mqttContext.getTime != NULL);

    xMQTTStatus = MQTT_Connect(&(globalMqttAgentContext.mqttContext),
                               &connectInfo,
                               NULL,
                               MQTT_CONNECT_TIMEOUT,
                               &sessionPresent);
    if (xMQTTStatus != MQTTSuccess) {
        ESP_LOGE(TAG, "Connection with MQTT broker failed with status = %s", MQTT_Status_strerror(xMQTTStatus));
        return xMQTTStatus;
    }

    ESP_LOGI(TAG, "MQTT connection successfully established with broker");

    return xMQTTStatus;
}

static uint32_t generateRandomNumber()
{
    return (rand());
}

/* 
 * Attempt to connect to MQTT broker. If connection fails, retry after
 * a timeout. Timeout value will exponentially increase until maximum
 * attempts are reached.
 */
TlsTransportStatus_t ConnectToMQTTBroker(NetworkContext_t* pNetworkContext, int maxAttempts)
{
    BaseType_t xStatus = pdPASS;
    TlsTransportStatus_t xTlsTransportStatus = TLS_TRANSPORT_SUCCESS;
    BackoffAlgorithmContext_t reconnectParams;
    uint16_t nextRetryBackOff = 0U;

    BackoffAlgorithm_InitializeParams(&reconnectParams,
                                      CONNECTION_RETRY_BACKOFF_BASE_MS,
                                      CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                      maxAttempts);

    do {
        xTlsTransportStatus = xTlsConnect(pNetworkContext);

        if (xTlsTransportStatus != TLS_TRANSPORT_SUCCESS) {
            ESP_LOGE(TAG, "Status: %s", TlsTransportStatusToString(xTlsTransportStatus));
            /* Generate a random number and get back-off value (in milliseconds) for the next connection retry. */
            BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff(&reconnectParams, generateRandomNumber(), &nextRetryBackOff);

            if (xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted) {
                ESP_LOGE(TAG, "All retry attempts have exhausted. Operation will not be retried");
                xStatus = pdFAIL;
            } else if (xBackoffAlgStatus == BackoffAlgorithmSuccess) {
                ESP_LOGE(TAG, "Connection to the broker failed. Retrying connection "
                         "after %hu ms backoff.",
                         (unsigned short)nextRetryBackOff);
                vTaskDelay(pdMS_TO_TICKS(nextRetryBackOff));
                xStatus = pdPASS;
            }
        } else {
            ESP_LOGI(TAG, "Status: %s", TlsTransportStatusToString(xTlsTransportStatus));
        }
    } while ((xTlsTransportStatus != TLS_TRANSPORT_SUCCESS) && (xStatus == pdPASS));

    return xTlsTransportStatus;
}

/* HELPER FUNCTIONS */

void ConnectToAWS(NetworkContext_t* pNetworkContext, TransportInterface_t* pTransport)
{
    LoadAWSSettings(!ONBOARDING);
    SetConnectionTLS(pNetworkContext);
    SetRootCA(pNetworkContext, AWS_ROOT_CA);
    NetworkTransportInit(pNetworkContext, pTransport);
    MQTTAgentInit(pTransport);

#if defined(CONNECTION_TEST)
    prvTestConnections(pNetworkContext);
#endif
    ESP_LOGI(TAG, "Establishing a TLS session to %s:%d", AWSConnectSettings.endpoint, AWS_SECURE_MQTT_PORT);
    EstablishMQTTSession(pNetworkContext, CONNECTION_RETRY_MAX_ATTEMPTS);
}

void EstablishMQTTSession(NetworkContext_t* pNetworkContext, uint16_t connectionRetryMaxAttemps)
{
    TlsTransportStatus_t xTlsTransportStatus = ConnectToMQTTBroker(pNetworkContext, connectionRetryMaxAttemps);

    assert(xTlsTransportStatus == TLS_TRANSPORT_SUCCESS);

    if (xTlsTransportStatus == TLS_TRANSPORT_SUCCESS) {
        assert(MQTTConnect() == MQTTSuccess);
    }
}

void HandleMQTTDisconnect(NetworkContext_t* pNetworkContext, MQTTContext_t * pContext)
{
    MQTTStatus_t xMQTTStatus;
    TlsTransportStatus_t xTLSStatus;

    ESP_LOGI(TAG, "Disconnecting from AWS");

    xMQTTStatus = MQTT_Disconnect(pContext);
    assert(xMQTTStatus == MQTTSuccess);

    xTLSStatus = xTlsDisconnect(pNetworkContext);
    assert(xTLSStatus == TLS_TRANSPORT_SUCCESS);    
}

bool ReconnectWithNewCertificate(NetworkContext_t* pNetworkContext)
{
    ESP_LOGI(TAG, "Establishing MQTT session with new certificate...to %s:%d", AWSConnectSettings.endpoint, AWS_SECURE_MQTT_PORT);

    UpdateAWSSettings(pNetworkContext);

    if (ConnectToMQTTBroker(pNetworkContext, 1) != TLS_TRANSPORT_SUCCESS) {
        ResetAWSCredentials(pNetworkContext);
        
        if (ConnectToMQTTBroker(pNetworkContext, CONNECTION_RETRY_MAX_ATTEMPTS) != TLS_TRANSPORT_SUCCESS) {
            ESP_LOGE(TAG, "Failed to connect after resetting credentials.");
            exit(1);
            return false;
        } 
    }
    if (MQTTConnect() != MQTTSuccess) {
        ESP_LOGE(TAG, "Failed to establish MQTT connection.");
        exit(1);
        return false;
    }  

    ESP_LOGI(TAG, "Successfully reconnected to MQTT broker with new certificate.");
    return true;
}

void RestoreMQTTConnection(NetworkContext_t* pNetworkContext)
{
    ESP_LOGE(TAG, "Network error detected, attempting to reconnect...");

    if (ConnectToMQTTBroker(pNetworkContext, CONNECTION_RETRY_MAX_ATTEMPTS) == TLS_TRANSPORT_SUCCESS) {
        MQTTStatus_t xMQTTStatus = MQTTConnect();
        assert(xMQTTStatus == MQTTSuccess);
        ESP_LOGI(TAG, "Reconnected to the MQTT broker successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to reconnect to the MQTT broker after maximum attempts.");
    }
}

void SetConnectionTLS(NetworkContext_t* pNetworkContext)
{
    pNetworkContext->is_plain_tcp = false;
    pNetworkContext->xPort        = AWS_SECURE_MQTT_PORT;
}

#if defined(CONNECTION_TEST)
void SetConnectionWithoutTLS(NetworkContext_t* pNetworkContext)
{
    pNetworkContext->is_plain_tcp = true;
    pNetworkContext->xPort        = AWS_INSECURE_MQTT_PORT;
}
#endif

void SetRootCA(NetworkContext_t* pNetworkContext, int root_ca)
{
    switch (root_ca) {
        case AWS_ROOT_CA:
            pNetworkContext->pcServerRootCA     = AWSConnectSettings.rootCA;
            pNetworkContext->pcServerRootCASize = strlen(AWSConnectSettings.rootCA) + 1;
            break;
#if defined(CONNECTION_TEST)
        case GOOGLE_ROOT_CA:
            pNetworkContext->pcServerRootCA     = TLSFailedSettings.rootCA;
            pNetworkContext->pcServerRootCASize = strlen(TLSFailedSettings.rootCA) + 1;
            break;
#endif
        default:
            break;
    }
}

/* This function is executed when an incomming publish message */
static void prvIncomingPublishCallback(MQTTAgentContext_t* pxMqttAgentContext,
                                       uint16_t usPacketId,
                                       MQTTPublishInfo_t* pxPublishInfo)
{
    bool xPublishHandled   = false;
    char topicRecived[150] = {0};

    strncpy(topicRecived, pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength);

    /* Fan out the incoming publishes to the callbacks registered using subscription manager. */

    ESP_LOGI(TAG, "In the incoming publish callback by the topic: %s", topicRecived);

    assert((SubscriptionElement_t*)pxMqttAgentContext->pIncomingCallbackContext != NULL);

    xPublishHandled = SubscriptionManager_HandleIncomingPublishes((SubscriptionElement_t*)pxMqttAgentContext->pIncomingCallbackContext,
                                                                  pxPublishInfo);

    /* If there are no callbacks to handle the incoming publishes, handle it as an unsolicited publish. */
    if (xPublishHandled != true) {
        char* pcLocation   = (char*) & (pxPublishInfo->pTopicName[pxPublishInfo->topicNameLength]);
        char cOriginalChar = *pcLocation;
        *pcLocation        = 0x00;
        *pcLocation        = cOriginalChar;

        if (!isUpdateJobs(pxPublishInfo->pTopicName, pxPublishInfo->topicNameLength)) {
            ESP_LOGW(TAG, "WARN:  Received an unsolicited publish from topic %s\n", pxPublishInfo->pTopicName);
        }
    }
}

#if defined(CONNECTION_TEST)
static void prvTestConnections(void)
{
    ESP_LOGI(TAG, "Establishing a non-TLS session to %s:%d",
                AWSConnectSettings.endpoint,
                AWS_INSECURE_MQTT_PORT);

    SetConnectionWithoutTLS(pNetworkContext);
    EstablishMQTTSession(pNetworkContext, 1);
    xTlsDisconnect(pNetworkContext);

    ESP_LOGI(TAG, "Establishing a TLS session with invalid certificate to %s:%d",
                AWSConnectSettings.endpoint,
                AWS_SECURE_MQTT_PORT);

    SetConnectionTLS(pNetworkContext);
    EstablishMQTTSession(pNetworkContext, 1);
    xTlsDisconnect(pNetworkContext);

    ESP_LOGI(TAG, "Establishing a TLS session with a different root CA to %s:%d",
                AWSConnectSettings.endpoint,
                AWS_SECURE_MQTT_PORT);

    SetConnectionTLS(pNetworkContext);
    LoadFailedTLSSettings();
    SetRootCA(pNetworkContext, GOOGLE_ROOT_CA);
    EstablishMQTTSession(pNetworkContext, 1);
    xTlsDisconnect(pNetworkContext);
    free(TLSFailedSettings.rootCA);
    SetRootCA(pNetworkContext, AWS_ROOT_CA);
}
#endif

/* Checks if the given topic corresponds to an AWS IoT job update topic. */
static bool isUpdateJobs(const char* pTopicName, size_t topicNameLength)
{
    char topicAccepted[TOPIC_FORMAT_SIZE] = {0};
    char topicRejected[TOPIC_FORMAT_SIZE] = {0};

    snprintf(topicAccepted, sizeof(topicAccepted), TOPIC_FORMAT, GetThingName(), GetJobId(), JOBS_API_SUCCESS);
    snprintf(topicRejected, sizeof(topicRejected), TOPIC_FORMAT, GetThingName(), GetJobId(), JOBS_API_FAILURE);

    /*
    ESP_LOGI(TAG, "Topico accepted %s", topicAccepted);
    ESP_LOGI(TAG, "Topico rejected %s", topicRejected);
    */
    if (!strncmp(pTopicName, topicAccepted, topicNameLength)) {
        ESP_LOGI(TAG, "The topic corresponds to a job update %s\n", topicAccepted);
        return true;
    } else if (!strncmp(pTopicName, topicRejected, topicNameLength)) {
        ESP_LOGI(TAG, "The topic corresponds to a job update %s\n", topicRejected);
        return true;
    }
    return false;
}

static uint32_t prvGetTimeMs(void)
{
    TickType_t xTickCount = 0;
    uint32_t ulTimeMs     = 0UL;

    /* Get the current tick count. */
    xTickCount = xTaskGetTickCount();

    /* Convert the ticks to milliseconds. */
    ulTimeMs = (uint32_t)xTickCount * MILLISECONDS_PER_TICK;

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTimeMs = (uint32_t)(ulTimeMs - ulGlobalEntryTimeMs);

    return ulTimeMs;
}
