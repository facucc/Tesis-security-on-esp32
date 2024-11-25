#ifndef _OTA_OS_FREERTOS_H_
#define _OTA_OS_FREERTOS_H_

/* Standard library include. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes. */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define DELAY_TIME pdMS_TO_TICKS(1000U)

QueueHandle_t InitEvent_FreeRTOS(UBaseType_t max_messages, UBaseType_t max_msg_size, uint8_t* ucQueueStorageArea, StaticQueue_t* xStaticQueue, const char* TAG);
BaseType_t SendEvent_FreeRTOS(QueueHandle_t xQueue, const void* eventMsg, const char* TAG);
BaseType_t ReceiveEvent_FreeRTOS(QueueHandle_t xQueue, void* eventMsg, TickType_t xTicksToWait, const char* TAG);

#endif