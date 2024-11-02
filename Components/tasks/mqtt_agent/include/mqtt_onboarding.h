#ifndef MQTT_ONBOARDING_H
#define MQTT_ONBOARDING_H
#include <string.h>
#include "esp_log.h"

#include "core_mqtt_agent.h"
#include "core_json.h"
#include "fleet_provisioning.h"
#include "mqtt_common.h"
//#include "mqtt_agent.h"
#include <inttypes.h>
#define NUMBER_OF_SUBSCRIPTIONS_ONBOARDING 4
#define PROVISIONING_TEMPLATE_NAME "FleetProvisioningDev"
#define THING_NAME_LENGTH 17


/**
 * @brief Timeout for MQTT_ProcessLoop function in milliseconds.
 */
#define MQTT_PROCESS_LOOP_TIMEOUT_MS ( 5000U )

#define DISABLE_ONBOARDING 0

#define REGISTER_THING_BODY "{\n" \
                            "    \"certificateOwnershipToken\": \"%s\",\n" \
                            "    \"parameters\": {\n" \
                            "        \"MacAddress\": \"%s\"\n" \
                            "    }\n" \
                            "}"


typedef struct CertificateOnBoarding
{
    char * certificateOwnershipToken;
    size_t certificateOwnershipTokenLength;
    char * certificate;
    size_t certificateLength;
    char * privateKey;
    char * thingName;

} CertificateOnBoarding_t;


MQTTStatus_t SubscribeOnBoardingTopic();
void UnSubscribeOnBoardingTopic();
void CreateCertificateFromCSR();
void RegisterThingAWS();
void StoreNewCredentials();
int8_t IsOnBoardingEnabled();
void DisableOnBoarding();
char * GetMacAddress();

#endif