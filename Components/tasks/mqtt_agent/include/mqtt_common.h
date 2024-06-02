#include "freertos/FreeRTOS.h"
#include "core_mqtt.h"

BaseType_t PublishToTopic( const char * pcTopic, uint16_t usTopicLen, const char * pcMsg, uint32_t ulMsgSize, MQTTQoS_t  xQoS, const char * TASK);
BaseType_t SubscribeToTopic( const char * pcTopicFilter, uint16_t usTopicFilterLength, MQTTQoS_t  xQoS, void * IncomingPublishCallback, const char * TASK);