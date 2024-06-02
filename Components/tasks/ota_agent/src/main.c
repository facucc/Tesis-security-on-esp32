#include <string.h>

#include "core_mqtt_agent.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"


#include "core_mqtt.h"
#include "MQTTFileDownloader.h"
#include "queue_handler.h"
#include "jobs.h"
#include "core_json.h"
#include "job_parser.h"
#include "ota_job_processor.h"
#include "ota_agent.h"
#include "mqtt_common.h"
#include "mqtt_agent.h"
/* Definitions*/
typedef void (* IncomingPubCallback_t )( void * pvIncomingPublishCallbackContext,
                                         MQTTPublishInfo_t * pxPublishInfo );

#define THING_NAME "test"
#define THING_NAME_LENGTH (strlen(THING_NAME)) 
/**
 * @brief The maximum amount of time in milliseconds to wait for the commands
 * to be posted to the MQTT agent should the MQTT agent's command queue be full.
 * Tasks wait in the Blocked state, so don't use any CPU time.
 */
#define MAX_COMMAND_SEND_BLOCK_TIME_MS         ( 2000 )

#define MAX_THING_NAME_SIZE     128U
#define START_JOB_MSG_LENGTH    147U

//#define TOPIC_BUFFER_SIZE 200

//#define JOBS_START_NEXT_ACCEPTED_TOPIC ("$aws/things/test/jobs/start-next/accepted")
#define JOBS_START_NEXT_ACCEPTED_TOPIC ("aws/things/test/jobs/notify-next")
#define MAX_JOB_ID_LENGTH   64U

/* Maximum size of the file which can be downloaded */
#define CONFIG_MAX_FILE_SIZE    1945600U /* Max Size OTA Partition*/
#define NUM_OF_BLOCKS_REQUESTED 1U
#define MAX_THING_NAME_SIZE     128U
#define MAX_JOB_ID_LENGTH       64U
#define MAX_NUM_OF_OTA_DATA_BUFFERS 1U
#define START_JOB_MSG_LENGTH 147U
#define UPDATE_JOB_MSG_LENGTH 48U

MqttFileDownloaderContext_t mqttFileDownloaderContext = { 0 };
static uint32_t numOfBlocksRemaining = 0;
static uint32_t currentBlockOffset = 0;
static uint8_t currentFileId = 0;
static uint32_t totalBytesReceived = 0;
char globalJobId[ MAX_JOB_ID_LENGTH ] = { 0 };

static OtaDataEvent_t dataBuffers[MAX_NUM_OF_OTA_DATA_BUFFERS] = { 0 };
static OtaJobEventData_t jobDocBuffer = { 0 };
static SemaphoreHandle_t bufferSemaphore;

static OtaState_t otaAgentState = OtaAgentStateInit;

typedef struct
{
    const esp_partition_t * update_partition;
    esp_ota_handle_t update_handle;
    bool valid_image;
} esp_ota_context_t;

static esp_ota_context_t ota_ctx;

static UBaseType_t uxHighWaterMark;

static const char *TAG = "OTA_AGENT";

// static char * JobDocument = "{\n"
//         "    \"execution\": {\n"
//         "        \"jobId\": \"test\",\n"
//         "        \"status\": \"IN_PROGRESS\",\n"
//         "        \"queuedAt\": 1716758294,\n"
//         "        \"startedAt\": 1716758316,\n"
//         "        \"lastUpdatedAt\": 1716758316,\n"
//         "        \"versionNumber\": 2,\n"
//         "        \"executionNumber\": 1,\n"
//         "        \"jobDocument\": {\n"
//         "            \"afr_ota\": {\n"
//         "                \"protocols\": [\n"
//         "                    \"MQTT\"\n"
//         "                ],\n"
//         "                \"streamname\": \"AFR_OTA-61e7c859-c731-4cdf-bfe3-8c22976c98b4\",\n"
//         "                \"files\": [\n"
//         "                    {\n"
//         "                        \"filepath\": \"firmware\",\n"
//         "                        \"filesize\": 1104808,\n"
//         "                        \"fileid\": 0,\n"
//         "                        \"certfile\": \"/Certificates/\",\n"
//         "                        \"sig-sha256-ecdsa\": \"MEYCIQCpe492WBHU6o5ZsBvY9rH2TwtdNTmNhNKEYLZSCjW6HQIhAIt+7mxUaof+oeWLsA3himhmRKg6MX1JvDZAujUn3kNq\"\n"
//         "                    }\n"
//         "                ]\n"
//         "            }\n"
//         "        }\n"
//         "    }\n"
//         "}";
/* Defines for mqtt file downloader */

#define CONFIG_BLOCK_SIZE 66560 /* [256 bytes, 128KB] */


//static void prvMqtt_publish(const char * pcTopicName, size_t xtopicLength, char * payoload, size_t payloadLength, MQTTQoS_t  xQoS);
//static void prvRequestJobDocumentHandler();
static void prvProcessOTAEvents();
static bool prvReceivedJobDocumentHandler( OtaJobEventData_t * jobDoc );
static bool prvJobDocumentParser( char * message, size_t messageLength, AfrOtaJobDocumentFields_t *jobFields );
static void prvRequestDataBlock( void );
static void prvInitMqttDownloader( AfrOtaJobDocumentFields_t *jobFields );
static bool prvSetOTAUpdateContext( void );
static void prvProcessReceivedDataBlock (OtaEventMsg_t recvEvent);
static void prvHandleMqttStreamsBlockArrived( uint8_t *data, size_t dataLength );

static OtaDataEvent_t * getOtaDataEventBuffer( void );
static void freeOtaDataEventBuffer( OtaDataEvent_t * const pxBuffer );

static void prvPublishCommandCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo );
static void prvIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo );
static void prvStreamDataIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo );


static void freeOtaDataEventBuffer( OtaDataEvent_t * const pxBuffer )
{
    if( xSemaphoreTake( bufferSemaphore, portMAX_DELAY ) == pdTRUE )
    {
        pxBuffer->bufferUsed = false;
        ( void ) xSemaphoreGive( bufferSemaphore );
    }
    else
    {
        ESP_LOGE( TAG, "Failed to get buffer semaphore.\n" );
    }
}

static OtaDataEvent_t * getOtaDataEventBuffer( void )
{
    uint32_t ulIndex = 0;
    OtaDataEvent_t * freeBuffer = NULL;

    if( xSemaphoreTake( bufferSemaphore, portMAX_DELAY ) == pdTRUE )
    {
        for( ulIndex = 0; ulIndex < MAX_NUM_OF_OTA_DATA_BUFFERS; ulIndex++ )
        {
            if( dataBuffers[ ulIndex ].bufferUsed == false )
            {
                dataBuffers[ ulIndex ].bufferUsed = true;
                freeBuffer = &dataBuffers[ ulIndex ];
                break;
            }
        }

        ( void ) xSemaphoreGive( bufferSemaphore );
    }
    else
    {
        ESP_LOGE( TAG,"Failed to get buffer semaphore. \n" );
    }

    return freeBuffer;
}

void otaAgenteTask(void *parameters)
{
    (void) parameters;
    OtaEventMsg_t nextEvent = { 0 };
    uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );

    bufferSemaphore = xSemaphoreCreateMutex();

    if( bufferSemaphore != NULL )
    {
        memset( dataBuffers, 0x00, sizeof( dataBuffers ) );
    }
    
    ESP_LOGI(TAG, "Por subscribirme al topico %s\n", JOBS_START_NEXT_ACCEPTED_TOPIC);
    SubscribeToTopic(JOBS_START_NEXT_ACCEPTED_TOPIC, strlen(JOBS_START_NEXT_ACCEPTED_TOPIC), MQTTQoS0, &prvIncomingPublishCallback, TAG);

    nextEvent.eventId = OtaAgentEventWaitingJobDocument;
    OtaSendEvent_FreeRTOS( &nextEvent );

    while (1)
    {
        prvProcessOTAEvents();     
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/*-----------------------------------------------------------*/
static BaseType_t prvWaitForCommandAcknowledgment( uint32_t * pulNotifiedValue )
{
    BaseType_t xReturn;

    /* Wait for this task to get notified, passing out the value it gets
     * notified with. */
    xReturn = xTaskNotifyWait( 0,
                               0,
                               pulNotifiedValue,
                               portMAX_DELAY);
    return xReturn;
}

static void prvProcessOTAEvents()
{
    OtaEventMsg_t recvEvent = { 0 };
    OtaEvent_t recvEventId = 0;
    OtaEventMsg_t nextEvent = { 0 };

    OtaReceiveEvent_FreeRTOS(&recvEvent);
    recvEventId = recvEvent.eventId;
    ESP_LOGE( TAG,"Received Event is %d \n", recvEventId);
    ESP_LOGE( TAG,"Current state is %d \n", otaAgentState);

    switch (recvEventId)
    {
    case OtaAgentEventWaitingJobDocument:
        ESP_LOGE( TAG,"Waiting for Job Document event Received \n");
        ESP_LOGE( TAG,"-------------------------------------\n");
        if (otaAgentState == OtaAgentStateSuspended)
        {
            ESP_LOGE( TAG, "OTA-Agent is in Suspend State. Hence dropping Request Job document event. \n");
            break;
        }
        otaAgentState = OtaAgentStateWaitingForJob;
        break;
    case OtaAgentEventReceivedJobDocument:
        ESP_LOGE( TAG,"Received Job Document event Received \n");
        ESP_LOGE( TAG,"-------------------------------------\n");

        if (otaAgentState == OtaAgentStateSuspended)
        {
            ESP_LOGE( TAG,"OTA-Agent is in Suspend State. Hence dropping Job Document. \n");
            break;
        }

        if ( prvReceivedJobDocumentHandler(recvEvent.jobEvent) )
        {
            nextEvent.eventId = OtaAgentEventRequestFileBlock;
            OtaSendEvent_FreeRTOS( &nextEvent );
        }
        else
        {
            ESP_LOGE( TAG,"This is not an OTA job \n");
            nextEvent.eventId = OtaAgentEventWaitingJobDocument;
            OtaSendEvent_FreeRTOS( &nextEvent );
            otaAgentState = OtaAgentStateWaitingForJob;
        }        
        break;
    case OtaAgentEventRequestFileBlock:
        ESP_LOGE( TAG,"Request File Block event Received \n");
        ESP_LOGE( TAG,"-----------------------------------\n");
        if (otaAgentState == OtaAgentStateSuspended)
        {
            ESP_LOGE( TAG,"OTA-Agent is in Suspend State. Hence dropping Request file block event. \n");
            break;
        }
        else if (currentBlockOffset == 0)
        {
            ESP_LOGE( TAG,"Starting The Download. \n" );
        }
        otaAgentState = OtaAgentStateRequestingFileBlock;
        prvRequestDataBlock();
        
        break;
    case OtaAgentEventReceivedFileBlock:
        ESP_LOGE( TAG,"Received File Block event Received \n");
        ESP_LOGE( TAG,"---------------------------------------\n");
        if (otaAgentState == OtaAgentStateSuspended)
        {
            ESP_LOGE( TAG,"OTA-Agent is in Suspend State. Hence dropping File Block. \n");
            freeOtaDataEventBuffer(recvEvent.dataEvent);
            break;
        }
        prvProcessReceivedDataBlock(recvEvent);

        if( numOfBlocksRemaining == 0 )
        {
            nextEvent.eventId = OtaAgentEventCloseFile;
            OtaSendEvent_FreeRTOS( &nextEvent );
        }
        else
        {
            nextEvent.eventId = OtaAgentEventRequestFileBlock;
            OtaSendEvent_FreeRTOS( &nextEvent );
        }
        break;
    case OtaAgentEventCloseFile:
        uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
        ESP_LOGE( TAG,"Close file event Received \n");
        ESP_LOGE( TAG,"-----------------------\n");
        ESP_LOGE( TAG,"HIGH WATER MARK | ota_agent %d\n", uxHighWaterMark);
        uxHighWaterMark = uxTaskGetStackHighWaterMark( xTaskGetHandle("mqtt agent Task") );
        ESP_LOGE( TAG,"HIGH WATER MARK | mqtt_agent %d\n", uxHighWaterMark);
        //finishDownload();
        otaAgentState = OtaAgentStateStopped;
        break;
    case OtaAgentEventSuspend:
        ESP_LOGE( TAG,"Suspend Event Received \n");
        ESP_LOGE( TAG,"-----------------------\n");
        otaAgentState = OtaAgentStateSuspended;
        break;
    case OtaAgentEventResume:
        ESP_LOGE( TAG,"Resume Event Received \n");
        ESP_LOGE( TAG,"---------------------\n");
        otaAgentState = OtaAgentStateRequestingJob;
        nextEvent.eventId = OtaAgentEventWaitingJobDocument;
        OtaSendEvent_FreeRTOS( &nextEvent );
    default:
        break;
    }
}
static void prvProcessReceivedDataBlock (OtaEventMsg_t recvEvent)
{
    uint8_t decodedData[ mqttFileDownloader_CONFIG_BLOCK_SIZE ];
    size_t decodedDataLength = 0;
    /*
     * MQTT streams Library:
     * Extracting and decoding the received data block from the incoming MQTT message.
     */
    mqttDownloader_processReceivedDataBlock(
        &mqttFileDownloaderContext,
        recvEvent.dataEvent->data,
        recvEvent.dataEvent->dataLength,
        decodedData,
        &decodedDataLength );
    prvHandleMqttStreamsBlockArrived(decodedData, decodedDataLength);
    freeOtaDataEventBuffer(recvEvent.dataEvent);
    ESP_LOGE( TAG,"Received block number %lu \n", currentBlockOffset);
    numOfBlocksRemaining--;
    currentBlockOffset++;

}
static bool prvReceivedJobDocumentHandler( OtaJobEventData_t * jobDoc )
{
    const char * ptrJob = NULL;
    char * jobId = NULL;
    size_t jobIdLength = 0U;
    AfrOtaJobDocumentFields_t jobFields = { 0 };
    /*
     * AWS IoT Jobs library:
     * Extracting the job ID from the received OTA job document.
     */
    ESP_LOGI(TAG, "Mensaje recibido de AWS:\n%s\n", (char *)jobDoc->jobData);

    jobIdLength = Jobs_GetJobId( (char *)jobDoc->jobData, jobDoc->jobDataLength,  &ptrJob);

    if ( jobIdLength )
    {
        jobId = calloc(sizeof(char *), jobIdLength + 1);
        strncpy(jobId, ptrJob, jobIdLength);

        if ( strncmp( globalJobId, jobId, jobIdLength ) )
        {           
            ESP_LOGI( TAG,"JobID %s\n", jobId);
            strncpy( globalJobId, jobId, jobIdLength );
            if (prvJobDocumentParser( (char * )jobDoc->jobData, jobDoc->jobDataLength, &jobFields ))
            {
                ESP_LOGE( TAG,"Received OTA Job. \n" );
                prvInitMqttDownloader( &jobFields );
                prvSetOTAUpdateContext();
                free(jobId);
                return true;
            }
            free(jobId);
            return false;
        }
        free(jobId);
        return true;        
    }

    return false;        
}
static bool prvJobDocumentParser( char * message, size_t messageLength, AfrOtaJobDocumentFields_t *jobFields )
{
    char * jobDoc;
    size_t jobDocLength = 0U;
    int8_t fileIndex = -1;

    /*
     * AWS IoT Jobs library:
     * Extracting the OTA job document from the jobs message recevied from AWS IoT core.
     */
    jobDocLength = Jobs_GetJobDocument( message, messageLength, (const char **) &jobDoc );

    if( jobDocLength != 0U )
    {
        fileIndex = 0;
        do
        {
            /*
             * AWS IoT Jobs library:
             * Parsing the OTA job document to extract all of the parameters needed to download
             * the new firmware.
             */
            fileIndex = otaParser_parseJobDocFile( jobDoc,
                                                jobDocLength,
                                                fileIndex,
                                                jobFields );
        } while( fileIndex > 0 );
    }

    // File index will be -1 if an error occured, and 0 if all files were
    // processed
    return fileIndex == 0;
}

static void prvRequestDataBlock( void )
{
    char getStreamRequest[ GET_STREAM_REQUEST_BUFFER_SIZE ];
    size_t getStreamRequestLength = 0U;

    /*
     * MQTT streams Library:
     * Creating the Get data block request. MQTT streams library only
     * creates the get block request. To publish the request, MQTT libraries
     * like coreMQTT are required.
     */
    getStreamRequestLength = mqttDownloader_createGetDataBlockRequest( mqttFileDownloaderContext.dataType,
                                        currentFileId,
                                        mqttFileDownloader_CONFIG_BLOCK_SIZE,
                                        currentBlockOffset,
                                        NUM_OF_BLOCKS_REQUESTED,
                                        getStreamRequest,
                                        &getStreamRequestLength );

    /* Se envia dos veces.. Fix*/

    SubscribeToTopic( mqttFileDownloaderContext.topicStreamData,
                      mqttFileDownloaderContext.topicStreamDataLength,
                      MQTTQoS0,
                      &prvStreamDataIncomingPublishCallback,
                      TAG);

    vTaskDelay( pdMS_TO_TICKS(5000) );
    PublishToTopic( mqttFileDownloaderContext.topicGetStream,
                    mqttFileDownloaderContext.topicGetStreamLength,
                    getStreamRequest,
                    getStreamRequestLength,
                    MQTTQoS0,
                    TAG );
}
static void prvInitMqttDownloader( AfrOtaJobDocumentFields_t *jobFields )
{
/*
    numOfBlocksRemaining = jobFields->fileSize / mqttFileDownloader_CONFIG_BLOCK_SIZE;
    numOfBlocksRemaining += ( jobFields->fileSize % mqttFileDownloader_CONFIG_BLOCK_SIZE > 0 ) ? 1 : 0;
*/
    numOfBlocksRemaining = 1;
    currentFileId = jobFields->fileId;
    currentBlockOffset = 0;
    totalBytesReceived = 0;

    /*
     * MQTT streams Library:
     * Initializing the MQTT streams downloader. Passing the
     * parameters extracted from the AWS IoT OTA jobs document
     * using OTA jobs parser.
     */
    mqttDownloader_init( &mqttFileDownloaderContext,
                        jobFields->imageRef,
                        jobFields->imageRefLen,
                        GetThingName(),
                        strlen(GetThingName()),
                        DATA_TYPE_JSON );
}

static bool prvSetOTAUpdateContext( void )
{
    esp_ota_handle_t update_handle;
    esp_err_t err;

    const esp_partition_t * update_partition = esp_ota_get_next_update_partition( NULL );

    if( update_partition == NULL )
    {
        ESP_LOGE( TAG, "Failed to find update partition. \n" );
        return false;
    }

    ESP_LOGE( TAG, "Writing to partition subtype %d at offset 0x%"PRIx32"",
               update_partition->subtype, update_partition->address );

    
    err = esp_ota_begin( update_partition, OTA_SIZE_UNKNOWN, &update_handle );

    if( err != ESP_OK )
    {
        ESP_LOGE( TAG, "esp_ota_begin failed (%d)", err  );
        return false;
    }

    memset(&ota_ctx, '0', sizeof(esp_ota_context_t));
    ota_ctx.update_partition = update_partition;
    ota_ctx.update_handle = update_handle;

    ota_ctx.valid_image = false;   

    ESP_LOGE( TAG, "esp_ota_begin succeeded" );
    return true;
}

/* Stores the received data blocks in the flash partition reserved for OTA */
static void prvHandleMqttStreamsBlockArrived( uint8_t *data, size_t dataLength )
{
    assert( ( totalBytesReceived + dataLength ) < CONFIG_MAX_FILE_SIZE );

    ESP_LOGE( TAG, "Downloaded block %lu of %lu \n", currentBlockOffset, ( currentBlockOffset + numOfBlocksRemaining) );

    esp_err_t ret = esp_ota_write_with_offset( ota_ctx.update_handle, data, dataLength, totalBytesReceived );

    if( ret != ESP_OK )
    {
        ESP_LOGE( TAG, "Couldn't flash at the offset %"PRIu32"", totalBytesReceived );
        return;
    }

    totalBytesReceived += dataLength;

}

/*-----------------------------------------------------------*/

void prvPublishCommandCallback( MQTTAgentCommandContext_t * pxCommandContext, MQTTAgentReturnInfo_t * pxReturnInfo )
{
    printf("En la funcion prvPublishCommandCallback\n");
//     TaskStatus_t xTaskDetails;
//     /* Check the handle is not NULL. */
//    // configASSERT( xHandle );

//     /* Use the handle to obtain further information about the task. */
//     vTaskGetInfo( /* The handle of the task being queried. */
//                   xTaskGetCurrentTaskHandle(),
//                   /* The TaskStatus_t structure to complete with information
//                   on xTask. */
//                   &xTaskDetails,
//                   /* Include the stack high water mark value in the
//                   TaskStatus_t structure. */
//                   pdTRUE,
//                   /* Include the task state in the TaskStatus_t structure. */
//                   eInvalid );
//     printf("Task mqtt agent %s ejecutando command callback\n", xTaskDetails.pcTaskName);
    /* Store the result in the application defined context so the task that
     * initiated the publish can check the operation's status. */
    pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

    // if( pxCommandContext->xTaskToNotify != NULL )
    // {
    //     /* Send the context's ulNotificationValue as the notification value so
    //      * the receiving task can check the value it set in the context matches
    //      * the value it receives in the notification. */
    //     xTaskNotify( pxCommandContext->xTaskToNotify,
    //                  pxCommandContext->ulNotificationValue,
    //                  eSetValueWithOverwrite );
    // }
}
static void prvIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo )
{
    OtaEventMsg_t nextEvent = { 0 };

    ESP_LOGI(TAG, "Handling Incoming MQTT message\n");
    
    (void ) pvIncomingPublishCallbackContext;

    memset(jobDocBuffer.jobData, '0', JOB_DOC_SIZE);
    memcpy(jobDocBuffer.jobData, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
    nextEvent.jobEvent = &jobDocBuffer;
    jobDocBuffer.jobDataLength = pxPublishInfo->payloadLength;
    nextEvent.eventId = OtaAgentEventReceivedJobDocument;
    //ESP_LOGI(TAG, "JobDocument %s\n", (char *) nextEvent.jobEvent->jobData);
    OtaSendEvent_FreeRTOS( &nextEvent );
}

static void prvStreamDataIncomingPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo )
{
    OtaEventMsg_t nextEvent = { 0 };

    ESP_LOGI(TAG, "Handling Incoming MQTT message\n");
    
    (void ) pvIncomingPublishCallbackContext;

    nextEvent.eventId = OtaAgentEventReceivedFileBlock;
    OtaDataEvent_t * dataBuf = getOtaDataEventBuffer();
    memcpy(dataBuf->data, pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
    nextEvent.dataEvent = dataBuf;
    dataBuf->dataLength = pxPublishInfo->payloadLength;
    ESP_LOGI("MQTT_AGENT", "Bloque recibido %s\n", (char *) pxPublishInfo->pPayload);
    OtaSendEvent_FreeRTOS( &nextEvent );
}