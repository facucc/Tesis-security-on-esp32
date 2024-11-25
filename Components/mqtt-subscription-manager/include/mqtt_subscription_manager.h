#ifndef MQTT_SUBSCRIPTION_MANAGER_H
#define MQTT_SUBSCRIPTION_MANAGER_H

#include "core_mqtt.h"

/* Maximum number of subscriptions maintained by the subscription manager simultaneously in a list. */
#ifndef SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS
    #define SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS    4U
#endif

/* Callback function called when receiving a publish. */
typedef void (* IncomingPubCallback_t )( void * pvIncomingPublishCallbackContext,
                                         MQTTPublishInfo_t * pxPublishInfo );

/* 
 * An element in the list of subscriptions.
 * This implementation allows multiple tasks to subscribe to the same topic.
 * In this case, another element is added to the subscription list, differing
 * in the intended publish callback. The topic filters are not
 * copied in the subscription manager and hence the topic filter strings need to
 * stay in scope until unsubscribed.
 */
typedef struct subscriptionElement
{
    IncomingPubCallback_t pxIncomingPublishCallback;
    void * pvIncomingPublishCallbackContext;
    uint16_t usFilterStringLength;
    const char * pcSubscriptionFilterString;
} SubscriptionElement_t;

/* Add a subscription to the subscription list.
 * Multiple tasks can be subscribed to the same topic with different
 * context-callback pairs. However, a single context-callback pair may only be
 * associated to the same topic filter once.
 * Returns `true` if subscription added or exists, `false` if insufficient memory.
 */
bool SubscriptionManager_AddSubscription( SubscriptionElement_t * pxSubscriptionList,
                                          const char * pcTopicFilterString,
                                          uint16_t usTopicFilterLength,
                                          IncomingPubCallback_t pxIncomingPublishCallback,
                                          void * pvIncomingPublishCallbackContext );

/* Remove a subscription from the subscription list.
 * If the topic filter exists multiple times in the subscription list,
 * then every instance of the subscription will be removed.
 */
void SubscriptionManager_RemoveSubscription( SubscriptionElement_t * pxSubscriptionList,
                                             const char * pcTopicFilterString,
                                             uint16_t usTopicFilterLength );

/* Handle incoming publishes by invoking the callbacks registered
 * for the incoming publish's topic filter.
 * Returns `true` if an application callback could be invoked; `false` otherwise.
 */
bool SubscriptionManager_HandleIncomingPublishes( SubscriptionElement_t * pxSubscriptionList,
                                                  MQTTPublishInfo_t * pxPublishInfo );

#endif
