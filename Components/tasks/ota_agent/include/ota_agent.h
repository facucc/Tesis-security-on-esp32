#ifndef OTA_AGENT_H
#define OTA_AGENT_H

#include "queue_handler.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "mqtt_common.h"

#define CONFIG_HEADER_SIZE 2000

/**
 * @brief OTA Agent states.
 *
 * The current state of the OTA Task (OTA Agent).
 */
typedef enum OtaState
{
    OtaStateNoTransition = -1,
    OtaStateInit = 0,
    OtaStateReady,
    OtaStateProcessingJob,
    OtaStateRequestingFileBlock,
    OtaStateProcessingFileBlock,
    OtaStateDownloadFinalized,
    OtaStateAll
} OtaState_t;

typedef enum OtaPartitionType
{
    OtaUpdatePartition = 1,
    OtaPatchPartition
} OtaPartitionType_t;

typedef struct
{
    const esp_partition_t *update_partition;
    const esp_partition_t *patch_partition;
    esp_ota_handle_t update_handle;
    OtaPartitionType_t OtaPartition_type;
    uint32_t data_write_len;
    bool valid_image;
} esp_ota_context_t;

typedef struct
{
    const esp_partition_t *partition;
    size_t offset;
    size_t size;
} esp_partition_context_t;

/**
 * @ingroup ota_enum_types
 * @brief The OTA OS interface return status.
 */
typedef enum OtaOsStatus
{
    OtaOsSuccess = 0,                    /*!< @brief OTA OS interface success. */
    OtaOsEventQueueCreateFailed = 0x80U, /*!< @brief Failed to create the event queue. */
    OtaOsEventQueueSendFailed,           /*!< @brief Posting event message to the event queue failed. */
    OtaOsEventQueueReceiveFailed,        /*!< @brief Failed to receive from the event queue. */
    OtaOsEventQueueDeleteFailed,         /*!< @brief Failed to delete the event queue. */
} OtaOsStatus_t;

typedef enum OtaEvent
{
    OtaEventStart = 0,           /*!< @brief Start the OTA state machine */
    OtaEventReady,              /*!< @brief Event for requesting job document. */
    OtaEventReceivedJobDocument, /*!< @brief Event when job document is received. */
    OtaEventRequestFileBlock,    /*!< @brief Event to request file blocks. */
    OtaEventReceivedFileBlock,   /*!< @brief Event to trigger when file block is received. */
    OtaEventFinishDownload,      /*!< @brief Event to trigger finisih file download. */
    OtaEventMax                  /*!< @brief Last event specifier */
} OtaEvent_t;
/**
 * @brief  The OTA Agent event and data structures.
 */

typedef struct OtaDataEvent
{
    uint8_t data[ mqttFileDownloader_CONFIG_BLOCK_SIZE + CONFIG_HEADER_SIZE]; /*!< Buffer for storing event information. */
    size_t dataLength;                 /*!< Total space required for the event. */
    bool bufferUsed;                     /*!< Flag set when buffer is used otherwise cleared. */
} OtaDataEvent_t;

/**
 * @brief Stores information about the event message.
 *
 */
typedef struct OtaEventMsg
{
    OtaDataEvent_t * dataEvent; /*!< Data Event message. */
    JobEventData_t * jobEvent; /*!< Job Event message. */
    OtaEvent_t eventId;          /*!< Identifier for the event. */
} OtaEventMsg_t;


void otaAgentTask(void *parameters);
bool SetOTAUpdateContext( const char * pfilePath, esp_ota_context_t * ota_ctx);
bool ApplyPatch( esp_ota_context_t * ota_ctx);

#endif 
