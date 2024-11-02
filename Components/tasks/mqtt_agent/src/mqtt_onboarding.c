#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "mqtt_agent.h"
#include "mqtt_onboarding.h"
#include "mqtt_subscription_manager.h"
#include "gen_csr.h"
#include "key_value_store.h"

extern MQTTAgentContext_t xGlobalMqttAgentContext;
extern SubscriptionElement_t pxGlobalSubscriptionList;
extern AWSConnectSettings_t AWSConnectSettings;

CertificateOnBoarding_t CertificateOnBoarding = {0};
static char * certificateOwnershipToken = NULL;
static const char *TAG = "MQTT_AGENT_ONBOARDING";

static void prvIncomingProvisioningPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo );
static void prvPublishToTopic( const char * pcTopic, uint16_t usTopicLen, const char * pcMsg, uint32_t ulMsgSize, MQTTQoS_t  xQoS);
static bool parseCsrResponse( const char * pResponse, size_t length);
bool ProcessLoopWithTimeout ( MQTTContext_t * pMqttContext);
bool parseRegisterThingResponse( const char * pResponse, size_t length);
static char * publishIncommingPacket(uint8_t * payload, size_t payloadLengh);

MQTTStatus_t SubscribeOnBoardingTopic()
{
    MQTTStatus_t xStatus;
    uint16_t xPacketId;

    MQTTSubscribeInfo_t subscriptionList[NUMBER_OF_SUBSCRIPTIONS_ONBOARDING] = { 0 };
 
    const char * topic_filters[ NUMBER_OF_SUBSCRIPTIONS_ONBOARDING ] = {
        FP_JSON_CREATE_CERT_ACCEPTED_TOPIC,
        FP_JSON_CREATE_CERT_REJECTED_TOPIC,
        FP_JSON_REGISTER_ACCEPTED_TOPIC(PROVISIONING_TEMPLATE_NAME),
        FP_JSON_REGISTER_REJECTED_TOPIC(PROVISIONING_TEMPLATE_NAME)
    };
    
    ESP_LOGI(TAG, "Subscribing to the onboarding topics\n");

    for( int i = 0; i < NUMBER_OF_SUBSCRIPTIONS_ONBOARDING; i++ )
    {
        //ESP_LOGI(TAG, "Topic: %s", topic_filters[ i ]);
        subscriptionList[ i ].qos = MQTTQoS0;
        subscriptionList[ i ].pTopicFilter = topic_filters[ i ];
        subscriptionList[ i ].topicFilterLength = strlen(topic_filters[ i ]);

        bool xSubscriptionAdded = SubscriptionManager_AddSubscription( ( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                                                topic_filters[ i ],
                                                strlen( topic_filters[ i ] ),
                                                &prvIncomingProvisioningPublishCallback,
                                                NULL );

        if( xSubscriptionAdded == false )
        {
            ESP_LOGE(TAG, "Failed to register an incoming publish callback for topic %.*s.", subscriptionList[i].topicFilterLength, subscriptionList[i].pTopicFilter );
        }
        else 
            ESP_LOGI(TAG,"Successful subscription");
    }    

    xPacketId = MQTT_GetPacketId( &( xGlobalMqttAgentContext.mqttContext ) );

    xStatus = MQTT_Subscribe( &( xGlobalMqttAgentContext.mqttContext ),
                                 subscriptionList,
                                 NUMBER_OF_SUBSCRIPTIONS_ONBOARDING,
                                 xPacketId );
    
    assert(xStatus == MQTTSuccess );

    if( xStatus == MQTTSuccess )
    {
        xStatus = WaitForPacketAck( &( xGlobalMqttAgentContext.mqttContext ),
                                       xPacketId,
                                       MQTT_PROCESS_LOOP_TIMEOUT_MS );
        assert(xStatus == MQTTSuccess);
    }    

    return xStatus;
}

void UnSubscribeOnBoardingTopic()
{
    MQTTStatus_t xStatus;
    MQTTSubscribeInfo_t unsubscribeList[NUMBER_OF_SUBSCRIPTIONS_ONBOARDING] = { 0 };
    
    const char * topic_filters[ NUMBER_OF_SUBSCRIPTIONS_ONBOARDING ] = {
        FP_JSON_CREATE_CERT_ACCEPTED_TOPIC,
        FP_JSON_CREATE_CERT_REJECTED_TOPIC,
        FP_JSON_REGISTER_ACCEPTED_TOPIC(PROVISIONING_TEMPLATE_NAME),
        FP_JSON_REGISTER_REJECTED_TOPIC(PROVISIONING_TEMPLATE_NAME)
    };

    ESP_LOGI(TAG, "unsubscribing to the onboarding topics\n");

    for( int i = 0; i < NUMBER_OF_SUBSCRIPTIONS_ONBOARDING; i++ )
    {
        unsubscribeList[ i ].pTopicFilter = topic_filters[ i ];
        unsubscribeList[ i ].topicFilterLength = strlen(topic_filters[ i ]);

        SubscriptionManager_RemoveSubscription( ( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                                                topic_filters[ i ],
                                                strlen( topic_filters[ i ] ));

        uint16_t xPacketId = MQTT_GetPacketId( &( xGlobalMqttAgentContext.mqttContext ) );

        xStatus = MQTT_Unsubscribe( &( xGlobalMqttAgentContext.mqttContext ),
                                    &unsubscribeList[i],
                                    1,
                                    xPacketId );
        
        assert(xStatus == MQTTSuccess );

        if( xStatus == MQTTSuccess )
        {
            xStatus = WaitForPacketAck( &( xGlobalMqttAgentContext.mqttContext ),
                                        xPacketId,
                                        MQTT_PROCESS_LOOP_TIMEOUT_MS );
            assert(xStatus == MQTTSuccess);
        }    
    }

}
void CreateCertificateFromCSR()
{
    char *req_msg = (char *)calloc(CSR_BUFFER_SIZE, sizeof(char));
    char *escaped_csr = (char *)calloc(CSR_BUFFER_SIZE, sizeof(char));

    char *key_pem = (char *)calloc(PRIVATE_KEY_BUFFER_SIZE, sizeof(char));
    char *csr_pem = (char *)calloc(CSR_BUFFER_SIZE, sizeof(char));

    if (GenerateCSR(key_pem, csr_pem) == true )
    {
        ESP_LOGI(TAG, "Certificate Signing Request \n%s", csr_pem);
        //ESP_LOGI(TAG, "Private key \n%s", key_pem);
        AWSConnectSettings.newPrivateKey = key_pem;
    }
    else
    {
        ESP_LOGE(TAG, "Failed generate csr");
    }

    EscapeNewlines(csr_pem, escaped_csr);

    snprintf(req_msg, 
             CSR_BUFFER_SIZE,
             CERTIFICATE_SIGNING_REQUEST_BODY,
             escaped_csr
            );
    
    prvPublishToTopic(FP_JSON_CREATE_CERT_PUBLISH_TOPIC,
                      FP_JSON_CREATE_CERT_PUBLISH_LENGTH,
                      req_msg,
                      strlen(req_msg),
                      MQTTQoS0);


    ProcessLoopWithTimeout(&( xGlobalMqttAgentContext.mqttContext ));

    free(req_msg);
    free(escaped_csr);    
}

void RegisterThingAWS()
{
    char buffer[1048]= {0};    
    char * mac_address = GetMacAddress();

    snprintf(buffer, 
             sizeof(buffer),
             REGISTER_THING_BODY,
             certificateOwnershipToken,
             mac_address
            );
    
    prvPublishToTopic(FP_JSON_REGISTER_PUBLISH_TOPIC(PROVISIONING_TEMPLATE_NAME),
                      FP_JSON_REGISTER_PUBLISH_LENGTH(strlen(PROVISIONING_TEMPLATE_NAME)),
                      buffer,
                      strlen(buffer),
                      MQTTQoS0);
    
    free(mac_address);

    ProcessLoopWithTimeout(&( xGlobalMqttAgentContext.mqttContext ));

}

char * GetMacAddress()
{
    uint8_t buffer[6] = {0};
    char * mac_address = (char *) calloc(THING_NAME_LENGTH, sizeof(char));

    assert(mac_address != NULL);

    if (esp_efuse_mac_get_default(buffer) == ESP_OK)
    {
        sprintf(mac_address, "%02x:%02x:%02x:%02x:%02x:%02x", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
        return mac_address;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get mac address");
        free(mac_address);
        return NULL;
    }    
    
}
static void prvPublishToTopic( const char * pcTopic, uint16_t usTopicLen, const char * pcMsg, uint32_t ulMsgSize, MQTTQoS_t  xQoS)
{
    MQTTStatus_t xStatus;
    MQTTPublishInfo_t xPublishInfo;
    uint16_t packetId;
    memset( &( xPublishInfo ), 0, sizeof( MQTTPublishInfo_t ) );
    xPublishInfo.qos = xQoS;
    xPublishInfo.pTopicName = pcTopic;
    xPublishInfo.topicNameLength = usTopicLen;
    xPublishInfo.pPayload = pcMsg;
    xPublishInfo.payloadLength = ulMsgSize;

    ESP_LOGI(TAG, "Publish packet = %s", pcMsg);

    packetId = MQTT_GetPacketId( &( xGlobalMqttAgentContext.mqttContext ) );
    xStatus = MQTT_Publish( &( xGlobalMqttAgentContext.mqttContext ), &xPublishInfo, packetId);

    if (xStatus != MQTTSuccess)
    {
        ESP_LOGE(TAG, "Failed to publish the csr %d", MQTT_Status_strerror(xStatus));
        assert(xStatus == MQTTSuccess);
    }

}
bool ProcessLoopWithTimeout ( MQTTContext_t * pMqttContext)
{
    uint32_t ulMqttProcessLoopTimeoutTime;
    uint32_t ulCurrentTime;

    MQTTStatus_t eMqttStatus = MQTTSuccess;
    bool returnStatus = false;

    ulCurrentTime = pMqttContext->getTime();
    ulMqttProcessLoopTimeoutTime = ulCurrentTime + (MQTT_PROCESS_LOOP_TIMEOUT_MS * 5);

    /* Call MQTT_ProcessLoop multiple times until the timeout expires or
     * #MQTT_ProcessLoop fails. */
    while( ( ulCurrentTime < ulMqttProcessLoopTimeoutTime ) &&
           ( eMqttStatus == MQTTSuccess || eMqttStatus == MQTTNeedMoreBytes ) )
    {
        eMqttStatus = MQTT_ProcessLoop( pMqttContext );
        ulCurrentTime = pMqttContext->getTime();
    }

    if( ( eMqttStatus != MQTTSuccess ) && ( eMqttStatus != MQTTNeedMoreBytes ) )
    {
        ESP_LOGE(TAG, "MQTT_ProcessLoop returned with status = %s.", MQTT_Status_strerror( eMqttStatus ) );
    }
    else
    {
        //ESP_LOGI(TAG, "MQTT_ProcessLoop successful.");
        returnStatus = true;
    }

    return returnStatus;
}
static void prvIncomingProvisioningPublishCallback( void * pvIncomingPublishCallbackContext, MQTTPublishInfo_t * pxPublishInfo )
{
    FleetProvisioningStatus_t xStatus;
    FleetProvisioningTopic_t api;
    ( void ) pvIncomingPublishCallbackContext;

    xStatus = FleetProvisioning_MatchTopic( pxPublishInfo->pTopicName,
                                           pxPublishInfo->topicNameLength, &api );

    if( xStatus != FleetProvisioningSuccess )
    {
        ESP_LOGE(TAG, "Unexpected publish message received. Topic: %.*s.",
                   ( int ) pxPublishInfo->topicNameLength,
                   ( const char * ) pxPublishInfo->pTopicName  );
    }
    else
    {
        char * in_packet = publishIncommingPacket(( uint8_t * ) pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
        //ESP_LOGI(TAG, "Payload: %s", in_packet);

        if( api == FleetProvJsonCreateCertFromCsrAccepted )
        {
            ESP_LOGI(TAG, "Received accepted response from Fleet Provisioning CreateCertificateFromCsr API." );

            parseCsrResponse(in_packet, pxPublishInfo->payloadLength);
            
            ESP_LOGI(TAG, "\ncertificateOwnershipToken: %s\n certificate: %s\n",
                    certificateOwnershipToken,
                    AWSConnectSettings.newCertificate);

        }
        else if( api == FleetProvJsonCreateCertFromCsrRejected )
        {
            ESP_LOGE(TAG, "Received rejected response from Fleet Provisioning CreateCertificateFromCsr API." );
            ESP_LOGE(TAG, "Errors: %s", in_packet);
            assert(api != FleetProvJsonCreateCertFromCsrRejected);
  
        }
        
        else if( api == FleetProvJsonRegisterThingAccepted )
        {
            ESP_LOGI(TAG,"Received accepted response from Fleet Provisioning RegisterThing API." );            
            parseRegisterThingResponse( in_packet, pxPublishInfo->payloadLength );
            ESP_LOGI(TAG, "ThingName %s", AWSConnectSettings.thingName);
        }
        else if( api == FleetProvJsonRegisterThingRejected )
        {
            ESP_LOGE(TAG,"Received rejected response from Fleet Provisioning RegisterThing API." );
            ESP_LOGE(TAG, "Errors: %s", in_packet);
            assert(api != FleetProvJsonRegisterThingRejected);
        }
        else
        {
            ESP_LOGE(TAG,"Received message on unexpected Fleet Provisioning topic. Topic: %.*s.",
                        ( int ) pxPublishInfo->topicNameLength,
                        ( const char * ) pxPublishInfo->pTopicName);
        }
    }
}
static char * publishIncommingPacket(uint8_t * payload, size_t payloadLengh)
{
    char * in_packet = (char *) calloc(payloadLengh + 1, sizeof(char));

    if (in_packet != NULL)
    { 
        strncpy(in_packet, (char * ) payload, payloadLengh);
        return in_packet;  
    }
    else
    {
        return NULL;
    }   
}

static char * setParametersToStruct(const char * src, size_t srcLength)
{
    char * target = calloc(srcLength + 1, sizeof(char ));

    if (target == NULL) {
        ESP_LOGE(TAG, "Failed to reserve memory\n");

    }
    strncpy(target, src, srcLength);

    return target;
}
bool parseCsrResponse( const char * pResponse, size_t length)
{
    const char * jsonValue = NULL;
    size_t jsonValueLength = 0U;
    JSONStatus_t jsonResult = JSONNotFound;     

    jsonResult = JSON_Validate( (char *) pResponse, length );

    if( jsonResult == JSONSuccess )
    {
        jsonResult = JSON_SearchConst( (char *) pResponse,
                                        length,
                                        "certificateOwnershipToken",
                                        25U,
                                        &jsonValue,
                                        &jsonValueLength,
                                        NULL );
        assert(jsonResult == JSONSuccess);
        
        certificateOwnershipToken = setParametersToStruct(jsonValue, jsonValueLength);

        jsonResult = JSON_SearchConst( (char *) pResponse,
                                        length,
                                        "certificatePem",
                                        14U,
                                        &jsonValue,
                                        &jsonValueLength,
                                        NULL );

        assert(jsonResult == JSONSuccess);
        
        char certificatePem[2048];
        strncpy(certificatePem, jsonValue, jsonValueLength);
        certificatePem[jsonValueLength] = '\0';

        ReplaceEscapedNewlines(certificatePem);

        AWSConnectSettings.newCertificate = setParametersToStruct(certificatePem, jsonValueLength + 1);

    }

    assert(jsonResult == JSONSuccess);
    return( JSONSuccess == jsonResult );
}
bool parseRegisterThingResponse( const char * pResponse, size_t length)
{

    const char * jsonValue = NULL;
    size_t jsonValueLength = 0U;
    JSONStatus_t jsonResult = JSONNotFound;

    jsonResult = JSON_Validate( (char *) pResponse, length );

    if( jsonResult == JSONSuccess )
    {
        jsonResult = JSON_SearchConst( (char *) pResponse,
                                        length,
                                        "thingName",
                                        9U,
                                        &jsonValue,
                                        &jsonValueLength,
                                        NULL );
        assert(jsonResult == JSONSuccess);

        AWSConnectSettings.thingName = setParametersToStruct(jsonValue, jsonValueLength);
    }

    return( JSONSuccess == jsonResult );
}
void StoreNewCredentials()
{
    load_value_to_nvs(CERTIFICATE_NVS_KEY, AWSConnectSettings.newCertificate);
    load_value_to_nvs(PRIVATE_KEY_NVS_KEY, AWSConnectSettings.newPrivateKey);
    load_value_to_nvs(THING_NAME_NVS_KEY,  AWSConnectSettings.thingName);

    free(certificateOwnershipToken); 
}
int8_t IsOnBoardingEnabled()
{
    esp_err_t err;
    
    int8_t isOnboardingEnabled = 0;

    ESP_LOGI(TAG, "Opening AWS Namespace");

    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READONLY, &xHandle) != ESP_OK);
    
    err = nvs_get_i8 (xHandle, ONBOARDING_ENABLED, &isOnboardingEnabled);

    assert(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);

    nvs_close(xHandle);
    return isOnboardingEnabled;
}
void DisableOnBoarding()
{
    esp_err_t err;

    nvs_handle xHandle;
    ESP_ERROR_CHECK(nvs_open(AWS_NAMESPACE, NVS_READWRITE, &xHandle) != ESP_OK);
    
    err = nvs_set_i8(xHandle, ONBOARDING_ENABLED, (int8_t) DISABLE_ONBOARDING);

    assert(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);

    nvs_close(xHandle);
}
