#ifndef MQTT_AGENT_H
#define MQTT_AGENT_H

/* CoreMQTT-Agent include. */
#include "core_mqtt_agent.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

/**
 * Defines the structure to use as the command callback context
 */
struct MQTTAgentCommandContext {
    MQTTStatus_t xReturnStatus;
    TaskHandle_t xTaskToNotify;
    uint32_t ulNotificationValue;
    void* pxIncomingPublishCallback;
    void* pArgs;
};

typedef struct AWSConnectSettings {
    char* certificate;
    char* privateKey;
    char* newCertificate;
    char* newPrivateKey;
    char* rootCA;
    char* endpoint;
    char* thingName;
} AWSConnectSettings_t;

/*
 * Entry point for executing the MQTT agent, responsible for managing
 * communication with AWS IoT Core. This function initializes the MQTT
 * connection, manages message subscriptions and publications, and handles
 * interactions with other agents, such as the certificate renewal agent.
*/
void mqttAgentTask(void* parameters);
#endif