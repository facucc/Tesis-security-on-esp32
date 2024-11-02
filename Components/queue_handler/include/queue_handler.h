#ifndef _OTA_OS_FREERTOS_H_
#define _OTA_OS_FREERTOS_H_

/* Standard library include. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* FreeRTOS includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "MQTTFileDownloader.h"


QueueHandle_t InitEvent_FreeRTOS(UBaseType_t max_messages, UBaseType_t max_msg_size, uint8_t * ucQueueStorageArea, StaticQueue_t * xStaticQueue, const char * TAG);
BaseType_t SendEvent_FreeRTOS(QueueHandle_t xQueue, const void * eventMsg, const char * TAG);
BaseType_t ReceiveEvent_FreeRTOS(QueueHandle_t xQueue, void * eventMsg, TickType_t xTicksToWait, const char * TAG);
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
//OtaOsStatus_t OtaDeinitEvent_FreeRTOS(const char * TAG); 


#endif /* ifndef _OTA_OS_FREERTOS_H_ */