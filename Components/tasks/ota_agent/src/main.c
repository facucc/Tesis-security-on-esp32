/* Standard C Library Headers */
#include <string.h>

/* esp-idf Headers*/
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* AWS IoT SDK Headers */
#include "MQTTFileDownloader.h"
#include "core_json.h"
#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "job_parser.h"
#include "jobs.h"
#include "ota_job_processor.h"

/*
 * Custom Project Headers
 * Application-specific headers for OTA and MQTT operations
 */
#include "mqtt_agent.h"
#include "mqtt_common.h"
#include "ota_agent.h"

/*
 * Macro Definitions
 * Defines constants, sizes, and other parameters for the application
 */

#define MAX_MESSAGES 5
#define MAX_MSG_SIZE sizeof(OtaEventMsg_t)

/* Maximum size of the file which can be downloaded */
#define CONFIG_MAX_FILE_SIZE    1843200U
#define NUM_OF_BLOCKS_REQUESTED 1U

#define MAX_NUM_OF_OTA_DATA_BUFFERS 1U
#define UPDATE_JOB_MSG_LENGTH       48U

#define NUMBER_OF_SUBSCRIPTIONS    2
#define STREAM_DATA_ACCEPTED_TOPIC "$aws/things/%s/streams/%s/data/json"
#define STREAM_DATA_REJECTED_TOPIC "$aws/things/%s/streams/%s/rejected/json"

#define SUCCESS_OTA_STATUS_DETAILS "{\"Code\": \"200\", \"Message\": \"Successful ota update\"}"
#define FAILED_OTA_STATUS_DETAILS  "{\"Code\": \"400\", \"Error\": \"Failed to ota update\"}"

/*
 * Used as a mutex to synchronize access to the data buffer
 */
static SemaphoreHandle_t bufferSemaphore;

static esp_ota_context_t ota_ctx;

/* Data buffers for OTA events */
static OtaDataEvent_t dataBuffers[MAX_NUM_OF_OTA_DATA_BUFFERS] = {0};

QueueHandle_t xOtaEventQueue;
/*
 * Array containing pointer to the OTA event structures used
 * to send events to the OTA task.
*/
static OtaEventMsg_t xqueueData[MAX_MESSAGES * MAX_MSG_SIZE];

/* The variable used to hold the queue's data structure. */
static StaticQueue_t xStaticQueue;

static char jobId[JOB_ID_LENGTH] = {0};

/*
 * Topic Filters
 * Buffer used for subscription topics related to OTA api responses
 */
static char* topic_filters[2] = {NULL, NULL};

/*
 * MQTT File Downloader Context
 * Structure used to interact with the AWS IoT Streams API
 */
MqttFileDownloaderContext_t mqttFileDownloaderContext = {0};

/* String representations of OTA agent states */
const char* pOtaAgentStateStrings[OtaStateAll + 1] = {
    "Init",
    "Ready",
    "ProcessingJob",
    "RequestingFileBlock",
    "ProcessingFileBlock",
    "DownloadFinalized",
    "All"
};

/* String representations of OTA agent events. */
const char* pOtaEventStrings[OtaEventMax + 1] = {
    "Start",
    "Ready",
    "ReceivedJobDocument",
    "RequestFileBlock",
    "ReceivedFileBlock",
    "FinishDownload"
};

static OtaState_t otaAgentState = OtaStateInit;

/* OTA state tracking variables */

static uint32_t totalBytesReceived   = 0;
static uint32_t numOfBlocksRemaining = 0;
static uint32_t currentBlockOffset   = 0;
static uint32_t lastBlock            = 0;
static uint16_t currentFileId        = 0;

/* Only Debug to detect stack size*/
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    static UBaseType_t uxHighWaterMark;
#endif

/* Logging tag for OTA agent */
static const char* TAG = "OTA_AGENT";

static void prvProcessOTAEvents(void);
static bool prvJobDocumentParser(char* message, size_t messageLength, AfrOtaJobDocumentFields_t* jobFields);
static void prvRequestDataBlock(void);
static bool prvInitMqttDownloader(AfrOtaJobDocumentFields_t* jobFields);
static void prvProcessReceivedDataBlock(OtaEventMsg_t recvEvent);
static void prvHandleMqttStreamsBlockArrived(int32_t blockId, uint8_t* data, size_t dataLength);
static void prvStreamDataIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo);
static bool prvFinishFirmwareUpdate(void);
static bool prvSubscribeStreamDataTopics(const char* streamName);
static bool isRejectedTopic(char* receivedTopic);
static void prvSendJobSuccessUpdate(void);
static void prvSendJobFailedUpdate(void);
static OtaDataEvent_t* prvGetOtaDataEventBuffer(void);
static void prvFreeOtaDataEventBuffer(OtaDataEvent_t* const pxBuffer);
static void prvPrint_partitions(void);

void otaAgentTask(void* parameters)
{
    (void)parameters;
    OtaEventMsg_t nextEvent = {0};
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
#endif

    bufferSemaphore = xSemaphoreCreateMutex();

    if (bufferSemaphore != NULL) {
        memset(dataBuffers, 0x00, sizeof(dataBuffers));
    }
    xOtaEventQueue = InitEvent_FreeRTOS(MAX_MESSAGES, MAX_MSG_SIZE, (uint8_t*)xqueueData, &xStaticQueue, TAG);

    nextEvent.eventId = OtaEventReady;
    SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);

    while (true) {
        prvProcessOTAEvents();
    }
}

/*
 * Implements the state machine to handle the OTA update process.
 * This function processes events received from the OTA event queue
 * and transitions the OTA agent through various states based on the event type.
*/
static void prvProcessOTAEvents(void)
{
    OtaEventMsg_t recvEvent = {0};
    OtaEvent_t recvEventId  = 0;
    OtaEventMsg_t nextEvent = {0};

    ReceiveEvent_FreeRTOS(xOtaEventQueue, (void*)&recvEvent, portMAX_DELAY, TAG);
    recvEventId = recvEvent.eventId;
    ESP_LOGI(TAG, "Current State: %s | Received Event: %s \n", pOtaAgentStateStrings[otaAgentState], pOtaEventStrings[recvEventId]);

    switch (recvEventId) {
        case OtaEventReady:
            otaAgentState = OtaStateReady;
            break;
        case OtaEventReceivedJobDocument:
            ESP_LOGI(TAG, "Job Document event Received ");
            ESP_LOGI(TAG, "-------------------------------------");

            otaAgentState = OtaStateProcessingJob;

            strncpy(jobId, recvEvent.jobEvent->jobId, JOB_ID_LENGTH);
            ESP_LOGI(TAG, "Job Id %s", jobId);
            ESP_LOGI(TAG, "Job document %s", recvEvent.jobEvent->jobData);

            AfrOtaJobDocumentFields_t jobFields = {0};

            if (prvJobDocumentParser(recvEvent.jobEvent->jobData, recvEvent.jobEvent->jobDataLength, &jobFields)) {
                char* filePath = (char*)calloc(jobFields.filepathLen + 1, sizeof(char));
                strncpy(filePath, jobFields.filepath, jobFields.filepathLen);

                if (prvInitMqttDownloader(&jobFields)) {
                    ESP_LOGI(TAG, "Received OTA Job.");

                    if (SetOTAUpdateContext(filePath, &ota_ctx)) {
                        SetJobId(jobId);
                        SendUpdateForJob(InProgress, NULL);

                        char* streamName = (char*)calloc(jobFields.imageRefLen + 1, sizeof(char));

                        if (streamName != NULL) {
                            strncpy(streamName, jobFields.imageRef, jobFields.imageRefLen);
                            prvSubscribeStreamDataTopics(streamName);
                            free(streamName);
                            free(filePath);

                            nextEvent.eventId = OtaEventRequestFileBlock;
                            SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);
                            return;
                        }
                        free(streamName);
                    }
                    free(filePath);
                }
                SendUpdateForJob(Rejected, NULL);
                nextEvent.eventId = OtaEventReady;
                SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);

            } else {
                ESP_LOGE(TAG, "This is not an OTA Document Job");

                SendUpdateForJob(Rejected, NULL);
                nextEvent.eventId = OtaEventReady;
                SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);
            }
            break;

        case OtaEventRequestFileBlock:
            ESP_LOGI(TAG, "Request File Block event Received");
            ESP_LOGI(TAG, "-----------------------------------");

            if (currentBlockOffset == 0) {
                ESP_LOGI(TAG, "Starting The Download.");
            }
            otaAgentState = OtaStateRequestingFileBlock;
            prvRequestDataBlock();

            break;
        case OtaEventReceivedFileBlock:
            otaAgentState = OtaStateProcessingFileBlock;
            ESP_LOGI(TAG, "Received File Block event Received");
            ESP_LOGI(TAG, "---------------------------------------");

            prvProcessReceivedDataBlock(recvEvent);

            if (numOfBlocksRemaining == 0) {
                nextEvent.eventId = OtaEventFinishDownload;
                SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);
            } else {
                nextEvent.eventId = OtaEventRequestFileBlock;
                SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);
            }
            break;
        case OtaEventFinishDownload:
            otaAgentState = OtaStateDownloadFinalized;
            ESP_LOGI(TAG, "Finishing download");
            ESP_LOGI(TAG, "-----------------------");
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
            uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "HIGH WATER MARK | ota_agent %d", uxHighWaterMark);
#endif

            if (prvFinishFirmwareUpdate()) {
                prvSendJobSuccessUpdate();
                vTaskDelay(pdMS_TO_TICKS(WAIT_RESPONSE));
                esp_restart();
            } else {
                prvSendJobFailedUpdate();
                nextEvent.eventId = OtaEventReady;
                SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);
            }

            break;
        default:
            break;
    }
}

static bool prvActivateNewImage()
{
    esp_err_t xError = esp_ota_set_boot_partition(ota_ctx.update_partition);
    if (xError != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition Failed %d", xError);
        return false;
    }
    ESP_LOGI(TAG, "Image successfully activated");
    return true;
}

static void prvSendJobSuccessUpdate()
{
    char* statusDetails = strndup(SUCCESS_OTA_STATUS_DETAILS, strlen(SUCCESS_OTA_STATUS_DETAILS));

    if (statusDetails == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for status details");
        return;
    }
    SendUpdateForJob(Succeeded, statusDetails);

    free((void*)statusDetails);
}

static void prvSendJobFailedUpdate()
{
    char* statusDetails = strndup(FAILED_OTA_STATUS_DETAILS, strlen(FAILED_OTA_STATUS_DETAILS));

    if (statusDetails == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for status details");
        return;
    }
    SendUpdateForJob(Failed, statusDetails);

    free((void*)statusDetails);
}

static bool prvFinishFirmwareUpdate()
{
    esp_err_t xError;

    if (ota_ctx.OtaPartition_type == OtaPatchPartition) {
        if (ApplyPatch(&ota_ctx)) {
            xError = esp_ota_end(ota_ctx.update_handle);
            if (xError != ESP_OK) {
                ESP_LOGE(TAG, "Ota end Failed %d\n", xError);
                return false;
            }
            return prvActivateNewImage();
        }
    } else {
        xError = esp_ota_end(ota_ctx.update_handle);
        if (xError != ESP_OK) {
            ESP_LOGE(TAG, "Ota end Failed %d\n", xError);
            return false;
        }
        return prvActivateNewImage();
    }
    return false;
}

static void prvProcessReceivedDataBlock(OtaEventMsg_t recvEvent)
{
    MQTTFileDownloaderStatus_t xStatus = 0;
    uint8_t decodedData[mqttFileDownloader_CONFIG_BLOCK_SIZE];
    size_t decodedDataLength = 0;
    int32_t fileId = 0, blockId = 0, blockSize = 0;

    /*
     * MQTT streams Library:
     * Extracting and decoding the received data block from the incoming MQTT message.
     */
    xStatus = mqttDownloader_processReceivedDataBlock(&mqttFileDownloaderContext,
                                                      recvEvent.dataEvent->data,
                                                      recvEvent.dataEvent->dataLength,
                                                      &fileId,
                                                      &blockId,
                                                      &blockSize,
                                                      decodedData,
                                                      &decodedDataLength);

    if (xStatus != MQTTFileDownloaderSuccess) {
        ESP_LOGE(TAG, "Process Received Data Block failed %d\n", xStatus);
        return;
    }
    prvHandleMqttStreamsBlockArrived(blockId, decodedData, decodedDataLength);
    prvFreeOtaDataEventBuffer(recvEvent.dataEvent);
    numOfBlocksRemaining--;
    currentBlockOffset++;
}

static bool prvJobDocumentParser(char* message, size_t messageLength, AfrOtaJobDocumentFields_t* jobFields)
{
    int8_t fileIndex = 0;

    do {
        fileIndex = otaParser_parseJobDocFile(message,
                                              messageLength,
                                              fileIndex,
                                              jobFields);
    } while (fileIndex > 0);

    ESP_LOGI(TAG, "fileIndex =%d", fileIndex);
    // File index will be -1 if an error occured, and 0 if all files were
    // processed
    return fileIndex == 0;
}

/* Creates and publishes a request for the next data block in the OTA update. */
static void prvRequestDataBlock(void)
{
    char getStreamRequest[GET_STREAM_REQUEST_BUFFER_SIZE];
    /*
     * MQTT streams Library:
     * Creating the Get data block request. MQTT streams library only
     * creates the get block request. To publish the request, MQTT libraries
     * like coreMQTT are required.
     */
    size_t getStreamRequestLength = mqttDownloader_createGetDataBlockRequest(mqttFileDownloaderContext.dataType,
                                                                             currentFileId,
                                                                             mqttFileDownloader_CONFIG_BLOCK_SIZE,
                                                                             currentBlockOffset,
                                                                             NUM_OF_BLOCKS_REQUESTED,
                                                                             getStreamRequest,
                                                                             GET_STREAM_REQUEST_BUFFER_SIZE);

    if (getStreamRequestLength > 0) {
        PublishToTopic(mqttFileDownloaderContext.topicGetStream,
                       mqttFileDownloaderContext.topicGetStreamLength,
                       getStreamRequest,
                       getStreamRequestLength,
                       MQTTQoS0,
                       TAG);
    } else {
        ESP_LOGE(TAG, "Failed creating the Get data block request");
    }
}

/* Initializes the MQTT downloader with the information from the job document. */
static bool prvInitMqttDownloader(AfrOtaJobDocumentFields_t* jobFields)
{
    MQTTFileDownloaderStatus_t xStatus;

    numOfBlocksRemaining = jobFields->fileSize / mqttFileDownloader_CONFIG_BLOCK_SIZE;
    numOfBlocksRemaining += (jobFields->fileSize % mqttFileDownloader_CONFIG_BLOCK_SIZE > 0) ? 1 : 0;

    ESP_LOGI(TAG, "Number of blocks to receive %ld", numOfBlocksRemaining);

    currentFileId      = jobFields->fileId;
    currentBlockOffset = 0;
    totalBytesReceived = 0;
    lastBlock          = numOfBlocksRemaining;

    /*
     * MQTT streams Library:
     * Initializing the MQTT streams downloader. Passing the
     * parameters extracted from the AWS IoT OTA jobs document
     * using OTA jobs parser.
     */
    xStatus = mqttDownloader_init(&mqttFileDownloaderContext,
                                  jobFields->imageRef,
                                  jobFields->imageRefLen,
                                  GetThingName(),
                                  strlen(GetThingName()),
                                  DATA_TYPE_JSON);

    if (xStatus != MQTTFileDownloaderSuccess) {
        ESP_LOGE(TAG, "MQTTFileDownloader initialization failed. Parsing of the job document failed");
        return false;
    }
    return true;
}

/* Subscribes to MQTT topics for receiving data blocks in the OTA stream. */
static bool prvSubscribeStreamDataTopics(const char* streamName)
{
    MQTTAgentSubscribeArgs_t xSubscribeArgs                       = {0};
    MQTTSubscribeInfo_t subscriptionList[NUMBER_OF_SUBSCRIPTIONS] = {0};

    topic_filters[0] = (char*)calloc(TOPIC_FILTER_LENGTH, sizeof(char));
    topic_filters[1] = (char*)calloc(TOPIC_FILTER_LENGTH, sizeof(char));

    if (topic_filters[0] == NULL && topic_filters[1] == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for subscription topics");
        assert(topic_filters[0] != NULL);
        assert(topic_filters[1] != NULL);
    }

    snprintf(topic_filters[0], TOPIC_FILTER_LENGTH, STREAM_DATA_ACCEPTED_TOPIC, GetThingName(), streamName);
    snprintf(topic_filters[1], TOPIC_FILTER_LENGTH, STREAM_DATA_REJECTED_TOPIC, GetThingName(), streamName);

    for (int i = 0; i < NUMBER_OF_SUBSCRIPTIONS; i++) {
        ESP_LOGI(TAG, "Topic: %s", topic_filters[i]);
        subscriptionList[i].qos               = MQTTQoS0;
        subscriptionList[i].pTopicFilter      = topic_filters[i];
        subscriptionList[i].topicFilterLength = strlen(topic_filters[i]);
    }
    xSubscribeArgs.numSubscriptions = NUMBER_OF_SUBSCRIPTIONS;
    xSubscribeArgs.pSubscribeInfo   = subscriptionList;

    return (SubscribeToTopic(&xSubscribeArgs, &prvStreamDataIncomingPublishCallback, TAG) == MQTTSuccess);
}

/* Stores the received data blocks in the flash partition reserved for OTA */
static void prvHandleMqttStreamsBlockArrived(int32_t blockId, uint8_t* data, size_t dataLength)
{
    esp_err_t xError;

    ESP_LOGI(TAG, "Total bytes received: %lu", (totalBytesReceived + dataLength));

    assert((totalBytesReceived + dataLength) < CONFIG_MAX_FILE_SIZE);

    ESP_LOGI(TAG, "Downloaded block %lu of %lu", blockId, numOfBlocksRemaining);

    if (ota_ctx.OtaPartition_type == OtaPatchPartition) {
        xError = esp_partition_write(ota_ctx.patch_partition, totalBytesReceived, data, dataLength);
    } else {
        xError = esp_ota_write_with_offset(ota_ctx.update_handle, data, dataLength, totalBytesReceived);
    }
    if (xError != ESP_OK) {
        ESP_LOGE(TAG, "Couldn't flash at the offset %" PRIu32 "", totalBytesReceived);
        return;
    }

    totalBytesReceived += dataLength;
}

static void prvFreeOtaDataEventBuffer(OtaDataEvent_t* const pxBuffer)
{
    if (xSemaphoreTake(bufferSemaphore, portMAX_DELAY) == pdTRUE) {
        pxBuffer->bufferUsed = false;
        (void)xSemaphoreGive(bufferSemaphore);
    } else {
        ESP_LOGE(TAG, "Failed to get buffer semaphore.\n");
    }
}

static OtaDataEvent_t* prvGetOtaDataEventBuffer(void)
{
    OtaDataEvent_t* freeBuffer = NULL;

    if (xSemaphoreTake(bufferSemaphore, portMAX_DELAY) == pdTRUE) {
        for (uint32_t ulIndex = 0; ulIndex < MAX_NUM_OF_OTA_DATA_BUFFERS; ulIndex++) {
            if (dataBuffers[ulIndex].bufferUsed == false) {
                dataBuffers[ulIndex].bufferUsed = true;
                freeBuffer                      = &dataBuffers[ulIndex];
                break;
            }
        }

        (void)xSemaphoreGive(bufferSemaphore);
    } else {
        ESP_LOGE(TAG, "Failed to get buffer semaphore. \n");
    }

    return freeBuffer;
}

/*
 * Callback function for handling incoming MQTT messages for OTA streams.
*/
static void prvStreamDataIncomingPublishCallback(void* pvIncomingPublishCallbackContext, MQTTPublishInfo_t* pxPublishInfo)
{
    OtaEventMsg_t nextEvent = {0};

    char topic_filter[TOPIC_FILTER_LENGTH] = {0};

    size_t copyLength = (pxPublishInfo->topicNameLength < TOPIC_FILTER_LENGTH - 1)
                        ? pxPublishInfo->topicNameLength
                        : TOPIC_FILTER_LENGTH - 1;

    memcpy(topic_filter, pxPublishInfo->pTopicName, copyLength);
    topic_filter[copyLength] = '\0';

    ESP_LOGI("MQTT_AGENT", "Handling Stream data Incoming publish\n");

    (void)pvIncomingPublishCallbackContext;

    /* Check if the topic is not rejected */
    if (!isRejectedTopic(topic_filter)) {
        ESP_LOGI("MQTT_AGENT", "Accepted topic, processing data block.\n");

        nextEvent.eventId = OtaEventReceivedFileBlock;

        /* Get a buffer for the event data. */
        OtaDataEvent_t* dataBuf = prvGetOtaDataEventBuffer();
        if (dataBuf == NULL) {
            ESP_LOGE("MQTT_AGENT", "No available buffers for the event.");
            return;
        }

        memcpy(dataBuf->data, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
        dataBuf->dataLength = pxPublishInfo->payloadLength;

        nextEvent.dataEvent = dataBuf;

        ESP_LOGI("MQTT_AGENT", "Stream Data Block Incoming: %.*s\n",
                 (int)pxPublishInfo->payloadLength, (char*)pxPublishInfo->pPayload);
    } else {
        ESP_LOGW("MQTT_AGENT", "Rejected topic, ignoring message.\n");
        return;
    }

    SendEvent_FreeRTOS(xOtaEventQueue, &nextEvent, TAG);
}

/* Checks if a given topic corresponds to a rejected topic. */
static bool isRejectedTopic(char* receivedTopic)
{
    if (strstr(receivedTopic, JOBS_API_FAILURE) != NULL) {
        return true;
    }

    return false;
}

void prvPrint_partitions()
{
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

    if (it == NULL) {
        ESP_LOGE(TAG, "No partitions found.\n");
        return;
    }

    ESP_LOGI(TAG, "Partitions found:\n");

    do {
        const esp_partition_t* partition = esp_partition_get(it);
        if (partition != NULL) {
            ESP_LOGI(TAG, "Name: %s\n", partition->label);
            ESP_LOGI(TAG, "Type: 0x%x\n", partition->type);
            ESP_LOGI(TAG, "Subtype: 0x%x\n", partition->subtype);
            ESP_LOGI(TAG, "Start address: 0x%lx\n", partition->address);
            ESP_LOGI(TAG, "Size: 0x%x \n", (unsigned int)partition->size);
            ESP_LOGI(TAG, "---------------------------\n");
        }
        it = esp_partition_next(it);
    } while (it != NULL);

    esp_partition_iterator_release(it);
}