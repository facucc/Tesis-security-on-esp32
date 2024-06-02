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

/* Array containing pointer to the OTA event structures used to send events to the OTA task. */
static OtaEventMsg_t queueData[ MAX_MESSAGES * MAX_MSG_SIZE ];

/* The queue control structure.  .*/
static StaticQueue_t staticQueue;

/* The queue control handle.  .*/
static QueueHandle_t otaEventQueue;


OtaOsStatus_t OtaInitEvent_FreeRTOS()
{
    OtaOsStatus_t otaOsStatus = OtaOsSuccess;

    otaEventQueue = xQueueCreateStatic( ( UBaseType_t ) MAX_MESSAGES,
                                        ( UBaseType_t ) MAX_MSG_SIZE,
                                        ( uint8_t * ) queueData,
                                        &staticQueue );

    if( otaEventQueue == NULL )
    {
        otaOsStatus = OtaOsEventQueueCreateFailed;

        ESP_LOGI("Queue", "Failed to create OTA Event Queue: "
                    "xQueueCreateStatic returned error: "
                    "OtaOsStatus_t=%d \n",
                    ( int ) otaOsStatus );
    }
    else
    {
        ESP_LOGI("Queue", "OTA Event Queue created.\n" );
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaSendEvent_FreeRTOS( const void * eventMsg )
{
    OtaOsStatus_t otaOsStatus = OtaOsSuccess;
    BaseType_t retVal = pdFALSE;

    /* Send the event to OTA event queue.*/
    assert (eventMsg != NULL);
    assert (otaEventQueue != NULL);
    retVal = xQueueSendToBack( otaEventQueue, eventMsg, ( TickType_t ) 0 );

    if( retVal == pdTRUE )
    {
        ESP_LOGI("Queue", "OTA Event Sent.\n" );
    }
    else
    {
        otaOsStatus = OtaOsEventQueueSendFailed;

        ESP_LOGI("Queue", "Failed to send event to OTA Event Queue: "
                    "xQueueSendToBack returned error: "
                    "OtaOsStatus_t=%d \n",
                    ( int ) otaOsStatus );
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaReceiveEvent_FreeRTOS( void * eventMsg )
{
    OtaOsStatus_t otaOsStatus = OtaOsSuccess;
    BaseType_t retVal = pdFALSE;

    /* Temp buffer.*/
    uint8_t buff[ sizeof( OtaEventMsg_t ) ];

    retVal = xQueueReceive( otaEventQueue, &buff, pdMS_TO_TICKS( 1000U ) );

    if( retVal == pdTRUE )
    {
        /* copy the data from local buffer.*/
        memcpy( eventMsg, buff, MAX_MSG_SIZE );
        printf( "OTA Event received \n" );
    }
    else
    {
        otaOsStatus = OtaOsEventQueueReceiveFailed;

        printf( "Failed to receive event or timeout from OTA Event Queue: "
                    "xQueueReceive returned error: "
                    "OtaOsStatus_t=%d \n",
                    ( int ) otaOsStatus );
    }

    return otaOsStatus;
}

OtaOsStatus_t OtaDeinitEvent_FreeRTOS()
{
    OtaOsStatus_t otaOsStatus = OtaOsSuccess;

    /* Remove the event queue.*/
    if( otaEventQueue != NULL )
    {
        vQueueDelete( otaEventQueue );

        printf( "OTA Event Queue Deleted. \n" );
    }

    return otaOsStatus;
}