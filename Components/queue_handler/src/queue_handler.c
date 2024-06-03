/* FreeRTOS includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
/* OTA OS POSIX Interface Includes.*/

#include "queue_handler.h"
#include "esp_log.h"

/* OTA Event queue attributes.*/
#define MAX_MESSAGES    20
#define MAX_MSG_SIZE    sizeof( OtaEventMsg_t )
#define DELAY_TIME pdMS_TO_TICKS( 1000U )

/* Array containing pointer to the OTA event structures used to send events to the OTA task. */
static OtaEventMsg_t queueData[ MAX_MESSAGES * MAX_MSG_SIZE ];

/* The queue control structure.  .*/
static StaticQueue_t staticQueue;

/* The queue control handle.  .*/
static QueueHandle_t otaEventQueue;


OtaOsStatus_t OtaInitEvent_FreeRTOS(const char * TAG)
{
    OtaOsStatus_t xOtaOsStatus= OtaOsSuccess;

    otaEventQueue = xQueueCreateStatic( ( UBaseType_t ) MAX_MESSAGES,
                                        ( UBaseType_t ) MAX_MSG_SIZE,
                                        ( uint8_t * ) queueData,
                                        &staticQueue );

    if( otaEventQueue == NULL )
    {
        xOtaOsStatus= OtaOsEventQueueCreateFailed;

        ESP_LOGI(TAG, "Failed to create OTA Event Queue: "
                    "xQueueCreateStatic returned error: "
                    "OtaOsStatus_t=%d \n",
                    ( int ) xOtaOsStatus);
    }
    else
    {
        ESP_LOGI(TAG, "OTA Event Queue created.\n" );
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaSendEvent_FreeRTOS( const void * eventMsg, const char * TAG)
{
    OtaOsStatus_t xOtaOsStatus= OtaOsSuccess;
    BaseType_t retVal = pdFALSE;

    /* Send the event to OTA event queue.*/
    assert (eventMsg != NULL);
    assert (otaEventQueue != NULL);

    retVal = xQueueSendToBack( otaEventQueue, eventMsg, ( TickType_t ) 0 );

    if( retVal == pdTRUE )
    {
        ESP_LOGI(TAG, "OTA Event Sent.\n" );
    }
    else
    {
        xOtaOsStatus= OtaOsEventQueueSendFailed;

        ESP_LOGI(TAG, "Failed to send event to OTA Event Queue: "
                      "xQueueSendToBack returned error: "
                      "OtaOsStatus_t=%d \n",
                       ( int ) xOtaOsStatus);
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaReceiveEvent_FreeRTOS( void * eventMsg, const char * TAG)
{
    OtaOsStatus_t xOtaOsStatus= OtaOsSuccess;
    BaseType_t retVal = pdFALSE;

    /* Temp buffer.*/
    uint8_t buff[ sizeof( OtaEventMsg_t ) ];

    retVal = xQueueReceive( otaEventQueue, &buff, DELAY_TIME );

    if( retVal == pdTRUE )
    {
        /* copy the data from local buffer.*/
        memcpy( eventMsg, buff, MAX_MSG_SIZE );
        ESP_LOGI(TAG, "OTA Event received \n" );
    }
    else
    {
        xOtaOsStatus= OtaOsEventQueueReceiveFailed;

        ESP_LOGE(TAG, "Failed to receive event or timeout from OTA Event Queue: "
                      "xQueueReceive returned error: "
                       "OtaOsStatus_t=%d \n",
                       ( int ) xOtaOsStatus);
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaDeinitEvent_FreeRTOS(const char * TAG)
{
    OtaOsStatus_t xOtaOsStatus= OtaOsSuccess;

    /* Remove the event queue.*/
    if( otaEventQueue != NULL )
    {
        vQueueDelete( otaEventQueue );
        ESP_LOGI(TAG, "OTA Event Queue Deleted\n");
    }

    return xOtaOsStatus;
}