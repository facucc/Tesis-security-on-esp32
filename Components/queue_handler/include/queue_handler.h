#ifndef _OTA_OS_FREERTOS_H_
#define _OTA_OS_FREERTOS_H_

/* Standard library include. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "MQTTFileDownloader.h"

/* Maximum size of the Job Document */
#define JOB_DOC_SIZE 1024U

#define CONFIG_HEADER_SIZE 2000
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
    OtaAgentEventStart = 0,           /*!< @brief Start the OTA state machine */
    OtaAgentEventWaitingJobDocument,  /*!< @brief Event for requesting job document. */
    OtaAgentEventReceivedJobDocument, /*!< @brief Event when job document is received. */
    OtaAgentEventCreateFile,          /*!< @brief Event to create a file. */
    OtaAgentEventRequestFileBlock,    /*!< @brief Event to request file blocks. */
    OtaAgentEventReceivedFileBlock,   /*!< @brief Event to trigger when file block is received. */
    OtaAgentEventCloseFile,           /*!< @brief Event to trigger closing file. */
    OtaAgentEventSuspend,             /*!< @brief Event to suspend ota task */
    OtaAgentEventResume,              /*!< @brief Event to resume suspended task */
    OtaAgentEventUserAbort,           /*!< @brief Event triggered by user to stop agent. */
    OtaAgentEventShutdown,            /*!< @brief Event to trigger ota shutdown */
    OtaAgentEventMax                  /*!< @brief Last event specifier */
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

typedef struct OtaJobEventData
{
    uint8_t jobData[JOB_DOC_SIZE];
    size_t jobDataLength;
} OtaJobEventData_t;

/**
 * @brief Stores information about the event message.
 *
 */
typedef struct OtaEventMsg
{
    OtaDataEvent_t * dataEvent; /*!< Data Event message. */
    OtaJobEventData_t * jobEvent; /*!< Job Event message. */
    OtaEvent_t eventId;          /*!< Identifier for the event. */
} OtaEventMsg_t;

/**
 * @brief Initialize the OTA events.
 *
 * This function initializes the OTA events mechanism for freeRTOS platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error code on failure.
 */
OtaOsStatus_t OtaInitEvent_FreeRTOS(const char * TAG);

/**
 * @brief Sends an OTA event.
 *
 * This function sends an event to OTA library event handler on FreeRTOS platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @param[pEventMsg]     Event to be sent to the OTA handler.
 *
 * @param[timeout]       The maximum amount of time (msec) the task should block.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error code on failure.
 */
OtaOsStatus_t OtaSendEvent_FreeRTOS( const void * pEventMsg, const char * TAG);

/**
 * @brief Receive an OTA event.
 *
 * This function receives next event from the pending OTA events on FreeRTOS platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @param[pEventMsg]     Pointer to store message.
 *
 * @param[timeout]       The maximum amount of time the task should block.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error code on failure.
 */
OtaOsStatus_t OtaReceiveEvent_FreeRTOS( void * pEventMsg, const char * TAG);

/**
 * @brief Deinitialize the OTA Events mechanism.
 *
 * This function deinitialize the OTA events mechanism and frees any resources
 * used on FreeRTOS platforms.
 *
 * @param[pEventCtx]     Pointer to the OTA event context.
 *
 * @return               OtaOsStatus_t, OtaOsSuccess if success , other error code on failure.
 */
OtaOsStatus_t OtaDeinitEvent_FreeRTOS(const char * TAG); 


#endif /* ifndef _OTA_OS_FREERTOS_H_ */