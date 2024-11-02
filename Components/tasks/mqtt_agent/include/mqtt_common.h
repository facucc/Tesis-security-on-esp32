#ifndef MQTT_COMMON_H
#define MQTT_COMMON_H

#include "freertos/FreeRTOS.h"
#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "jobs.h"


/* Maximum size of the Job Document */
#define JOB_DOC_SIZE 1800U
#define JOB_ID_LENGTH 80
#define UPDATE_REQUEST_SIZE 150U
#define JOB_UPDATE_SIZE 150U
#define TOPIC_FILTER_LENGTH 100

typedef struct JobEventData
{
    char jobId[JOB_ID_LENGTH];
    char jobData[JOB_DOC_SIZE];
    size_t jobDataLength;
} JobEventData_t;

typedef struct TopicFilters
{
    char * accepted_topic;
    char * rejected_topic;
}TopicFilters_t;

#define CERTIFICATE_SIGNING_REQUEST_BODY "{\n" \
                                         "  \"certificateSigningRequest\": \"%s\"\n" \
                                         "}"

BaseType_t PublishToTopic( const char * pcTopic, uint16_t usTopicLen, const char * pcMsg, uint32_t ulMsgSize, MQTTQoS_t  xQoS, const char * TASK);
BaseType_t SubscribeToTopic( MQTTAgentSubscribeArgs_t * pcSubsTopics, void * IncomingPublishCallback, const char * TASK);
BaseType_t UnSubscribeToTopic( MQTTAgentSubscribeArgs_t * pcSubsTopics, const char * TASK);
MQTTStatus_t TerminateMQTTAgent(void * IncomingPublishCallback, const char * TASK);
MQTTStatus_t SubscribeToNextJobTopic();
void SendUpdateForJob(JobCurrentStatus_t pcJobStatus, const char * pcJobStatusMsg );
MQTTStatus_t WaitForPacketAck( MQTTContext_t * pMqttContext, uint16_t usPacketIdentifier, uint32_t ulTimeout );
void ReplaceEscapedNewlines(char *str);
void EscapeNewlines(const char* input, char* output);
void SetJobId(const char *jobId);
const char * GetJobId();
#endif 
