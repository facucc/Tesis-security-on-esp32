#ifndef MQTT_COMMON_H
#define MQTT_COMMON_H

#include "freertos/FreeRTOS.h"

#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "jobs.h"

#define AWS_ROOT_CA    1
#define GOOGLE_ROOT_CA 2

#define AWS_INSECURE_MQTT_PORT 1883
#define AWS_SECURE_MQTT_PORT   8883

#define ONBOARDING         true
#define DISABLE_ONBOARDING 0

#define MQTT_PROCESS_LOOP_TIMEOUT_MS 5000U

#define JOB_DOC_SIZE        1800U
#define JOB_ID_LENGTH       80
#define UPDATE_REQUEST_SIZE 150U
#define JOB_UPDATE_SIZE     150U
#define TOPIC_FILTER_LENGTH 100

#define THING_NAME_LENGTH 20

typedef struct JobEventData {
    char jobId[JOB_ID_LENGTH];
    char jobData[JOB_DOC_SIZE];
    size_t jobDataLength;
} JobEventData_t;

typedef struct TopicFilters {
    char* accepted_topic;
    char* rejected_topic;
} TopicFilters_t;

#define CERTIFICATE_SIGNING_REQUEST_BODY "{\n"                                       \
    "  \"certificateSigningRequest\": \"%s\"\n" \
    "}"

MQTTStatus_t PublishToTopic(const char* pcTopic, uint16_t usTopicLen, const char* pcMsg, uint32_t ulMsgSize, MQTTQoS_t xQoS, const char* TASK);
MQTTStatus_t SubscribeToTopic(MQTTAgentSubscribeArgs_t* pcSubsTopics, void* IncomingPublishCallback, const char* TASK);
MQTTStatus_t UnSubscribeToTopic(MQTTAgentSubscribeArgs_t* pcSubsTopics, const char* TASK);
MQTTStatus_t TerminateMQTTAgent(void* IncomingPublishCallback, const char* TASK);
MQTTStatus_t SubscribeToNextJobTopic();
void SendUpdateForJob(JobCurrentStatus_t pcJobStatus, const char* pcJobStatusMsg);
MQTTStatus_t WaitForPacketAck(MQTTContext_t* pMqttContext, uint16_t usPacketIdentifier, uint32_t ulTimeout);
bool ProcessLoopWithTimeout(MQTTContext_t* pMqttContext);
void ReplaceEscapedNewlines(char* str);
void EscapeNewlines(const char* input, char* output);
char* CreateStringCopy(const char* src, size_t srcLength);
char* GetMacAddress();
void SetJobId(const char* jobId);
const char* GetJobId();
char* GetThingName();
#endif
