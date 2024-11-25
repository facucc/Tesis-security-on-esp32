#ifndef MQTT_ONBOARDING_H
#define MQTT_ONBOARDING_H

#include "esp_log.h"
#include <string.h>

#include "core_json.h"
#include "core_mqtt_agent.h"
#include "fleet_provisioning.h"
#include "mqtt_common.h"
#include "mqtt_connection.h"
#include "mqtt_aws_credentials.h"
#include <inttypes.h>

#define NUMBER_OF_SUBSCRIPTIONS_ONBOARDING 4
#define PROVISIONING_TEMPLATE_NAME         "FleetProvisioning-prod"
#define REGISTER_THING_BUFFER_SIZE         1048

#define REGISTER_THING_BODY "{\n"                                          \
    "    \"certificateOwnershipToken\": \"%s\",\n" \
    "    \"parameters\": {\n"                      \
    "        \"MacAddress\": \"%s\"\n"             \
    "    }\n"                                      \
    "}"

typedef struct CertificateOnBoarding {
    char* certificateOwnershipToken;
    size_t certificateOwnershipTokenLength;
    char* certificate;
    size_t certificateLength;
    char* privateKey;
    char* thingName;

} CertificateOnBoarding_t;

void StartOnboarding(NetworkContext_t* pNetworkContext, TransportInterface_t* pTransport);
MQTTStatus_t SubscribeOnBoardingTopic();
void UnSubscribeOnBoardingTopic();
void CreateCertificateFromCSR();
void RegisterThingAWS();
void StoreNewCredentials();
int8_t IsOnBoardingEnabled();
void DisableOnBoarding();

#endif