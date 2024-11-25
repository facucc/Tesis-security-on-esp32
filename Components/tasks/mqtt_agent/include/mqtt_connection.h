#ifndef MQTT_CONNECTION_H
#define MQTT_CONNECTION_H

#include <string.h>
#include "core_mqtt.h"

/* CoreMQTT-Agent APIS for running MQTT in a multithreaded environment. */
#include "freertos_agent_message.h"
#include "freertos_command_pool.h"

#include "network_transport.h"
#include "mqtt_agent.h"
#include "mqtt_common.h"
#include "mqtt_aws_credentials.h"

/* The maximum number of retries for connecting to server. */
#define CONNECTION_RETRY_MAX_ATTEMPTS 16000U

void NetworkTransportInit(NetworkContext_t* pNetworkContext, TransportInterface_t* xTransport);
void SetConnectionTLS(NetworkContext_t* pNetworkContext);
void SetConnectionWithoutTLS(NetworkContext_t* pNetworkContext);
void SetRootCA(NetworkContext_t* pNetworkContext, int root_ca);
MQTTStatus_t MQTTAgentInit(TransportInterface_t* xTransport);

TlsTransportStatus_t ConnectToMQTTBroker(NetworkContext_t* pNetworkContext, int maxAttempts);
MQTTStatus_t MQTTConnect(void);

/* Helper functions */
void ConnectToAWS(NetworkContext_t* pNetworkContext, TransportInterface_t* pTransport);
void EstablishMQTTSession(NetworkContext_t* pNetworkContext, uint16_t connectionRetryMaxAttemps);
void HandleMQTTDisconnect(NetworkContext_t* pNetworkContext, MQTTContext_t* pContext);
bool ReconnectWithNewCertificate(NetworkContext_t* pNetworkContext);
void RestoreMQTTConnection(NetworkContext_t* pNetworkContext);

#endif