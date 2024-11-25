#ifndef OTA_AGENT_H
#define OTA_AGENT_H

/* 
 * ESP-IDF Libraries 
 * Includes for managing partitions and OTA updates
 */
#include "esp_partition.h"
#include "esp_ota_ops.h"

/* 
 * FreeRTOS Libraries 
 * Includes for default MQTT File Downloader configuration
 */
#include "MQTTFileDownloader_defaults.h"

/* 
 * Custom Libraries 
 * Includes for queue handling and MQTT common utilities
 */
#include "queue_handler.h"
#include "mqtt_common.h"

#define WAIT_RESPONSE 5000
#define CONFIG_HEADER_SIZE 2000

/**
 * The current state of the OTA Task (OTA Agent).
 */
typedef enum OtaState
{
    OtaStateNoTransition = -1,         /* No transition in state */
    OtaStateInit = 0,                  /* Initial state */
    OtaStateReady,                     /* Ready for processing */
    OtaStateProcessingJob,             /* Processing OTA job */
    OtaStateRequestingFileBlock,       /* Requesting file blocks */
    OtaStateProcessingFileBlock,       /* Processing received file blocks */
    OtaStateDownloadFinalized,         /* Finalized download state */
    OtaStateAll                        /* All states */
} OtaState_t;

/* 
 * Enum defining the types of OTA partitions
 */
typedef enum OtaPartitionType
{
    OtaUpdatePartition = 1,            /* OTA update partition */
    OtaPatchPartition                  /* OTA patch partition */
} OtaPartitionType_t;

/* 
 * Holds context information for managing OTA updates
 */

typedef struct
{
    const esp_partition_t *update_partition; /* Pointer to the update partition */
    const esp_partition_t *patch_partition;  /* Pointer to the patch partition */
    esp_ota_handle_t update_handle;          /* Handle for the OTA update */
    OtaPartitionType_t OtaPartition_type;    /* Type of the OTA partition */
    uint32_t data_write_len;                 /* Length of data written */
    bool valid_image;                        /* Indicates if the image is valid */
} esp_ota_context_t;

/* 
 * Stores information about a specific partition
 */
typedef struct
{
    const esp_partition_t *partition; /* Pointer to the partition */
    size_t offset;                    /* Offset within the partition */
    size_t size;                      /* Size of the partition */
} esp_partition_context_t;

/* 
 * Enum defining possible events for the OTA agent
 */
typedef enum OtaEvent
{
    OtaEventStart = 0,           /* Start the OTA state machine */
    OtaEventReady,               /* Ready to request a job document */
    OtaEventReceivedJobDocument, /* Received the job document */
    OtaEventRequestFileBlock,    /* Request file blocks */
    OtaEventReceivedFileBlock,   /* Received a file block */
    OtaEventFinishDownload,      /* Finish downloading the file */
    OtaEventMax                  /* Maximum number of events */
} OtaEvent_t;

/* 
 * This structure contains the buffer to store the downloaded block, its size, 
 * and a flag to indicate whether the buffer is currently in use.
 */
typedef struct OtaDataEvent
{
    uint8_t data[mqttFileDownloader_CONFIG_BLOCK_SIZE + CONFIG_HEADER_SIZE];
    size_t dataLength;                                                       
    bool bufferUsed;                                                         
} OtaDataEvent_t;

typedef struct OtaEventMsg
{
    OtaDataEvent_t *dataEvent; /* Pointer to the data block */
    JobEventData_t *jobEvent;  /* Pointer to the ota event */
    OtaEvent_t eventId;        /* Identifier for the event */
} OtaEventMsg_t;

/* 
 * Entry point for executing the OTA agent. 
 * This task manages the OTA update process, including state transitions, 
 * job document processing, file block requests, and firmware updates.
 */
void otaAgentTask(void *parameters);

/* 
 * Initializes the OTA update partition for firmware update. 
 * This function sets up the context required for writing to the OTA partition.
 */
bool SetOTAUpdateContext( const char * pfilePath, esp_ota_context_t * ota_ctx);

bool ApplyPatch( esp_ota_context_t * ota_ctx);

#endif 
