/* OTA OS POSIX Interface Includes.*/

#include "queue_handler.h"
#include "esp_log.h"


#define DELAY_TIME pdMS_TO_TICKS( 1000U )

QueueHandle_t InitEvent_FreeRTOS(UBaseType_t max_messages, UBaseType_t max_msg_size, uint8_t * ucQueueStorageArea, StaticQueue_t * xStaticQueue, const char * TAG)
{
    QueueHandle_t xEventQueue;

    xEventQueue = xQueueCreateStatic( max_messages,
                                      max_msg_size,
                                      ucQueueStorageArea,
                                      xStaticQueue );

    if( xEventQueue == NULL )
    {   
        ESP_LOGE(TAG, "Failed to create Event Queue.\n");
        return NULL;
    }

    ESP_LOGI(TAG, "Event Queue created.\n");

    return xEventQueue;

}
BaseType_t SendEvent_FreeRTOS(QueueHandle_t xQueue, const void * eventMsg, const char * TAG)
{
    BaseType_t retVal = pdFALSE;

    /* Send the event to OTA event queue.*/
    assert (eventMsg != NULL);
    assert (xQueue != NULL);

    retVal = xQueueSendToBack( xQueue, eventMsg, ( TickType_t ) 0 );

    if( retVal == pdTRUE )
    {
        //ESP_LOGI(TAG, "Event Sent." );
        return retVal;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to send event to Event Queue: "
                      "xQueueSendToBack returned error: %d",
                       retVal);
        return retVal;
    }
}

BaseType_t ReceiveEvent_FreeRTOS(QueueHandle_t xQueue, void * eventMsg, TickType_t xTicksToWait, const char * TAG)
{
    BaseType_t retVal = pdFALSE;

    retVal = xQueueReceive( xQueue, eventMsg, xTicksToWait );

    if( retVal == pdTRUE )
    {
        //ESP_LOGI(TAG, "Event received" );
        return retVal;
    }
    else
    {       
        ESP_LOGE(TAG, "Failed to receive event or timeout from Event Queue: "
                      "xQueueReceive returned error: %d",
                       retVal);
        return retVal;
    }   
}