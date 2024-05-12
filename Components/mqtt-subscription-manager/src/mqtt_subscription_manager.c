/**
 * @file mqtt_subscription_manager.c
 * @brief Functions for managing MQTT subscriptions.
 */

/* Standard includes. */
#include <string.h>
#include <stdio.h>

/* Subscription manager header include. */
#include "mqtt_subscription_manager.h"
#include "esp_log.h"

static const char* TAG = "Subscripcion_Manager";

bool SubscriptionManager_AddSubscription( SubscriptionElement_t * pxSubscriptionList,
                                          const char * pcTopicFilterString,
                                          uint16_t usTopicFilterLength,
                                          IncomingPubCallback_t pxIncomingPublishCallback,
                                          void * pvIncomingPublishCallbackContext )
{
    int32_t lIndex = 0;
    size_t xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
    bool xReturnStatus = false;

    if( ( pxSubscriptionList == NULL ) ||
        ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U )
         )
    {
        ESP_LOGE(TAG, "Invalid parameter. pxSubscriptionList=%p, pcTopicFilterString=%p,"
                      " usTopicFilterLength=%u, pxIncomingPublishCallback=%p.",
                       pxSubscriptionList,
                       pcTopicFilterString,
                       ( unsigned int ) usTopicFilterLength,
                       pxIncomingPublishCallback);
    }
    else
    {
        /* Start at end of array, so that we will insert at the first available index.
         * Scans backwards to find duplicates. */
        for( lIndex = ( int32_t ) SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS - 1; lIndex >= 0; lIndex-- )
        {
            if( pxSubscriptionList[ lIndex ].usFilterStringLength == 0 )
            {
                xAvailableIndex = lIndex;
            }
            else if( ( pxSubscriptionList[ lIndex ].usFilterStringLength == usTopicFilterLength ) &&
                     ( strncmp( pcTopicFilterString, pxSubscriptionList[ lIndex ].pcSubscriptionFilterString, ( size_t ) usTopicFilterLength ) == 0 ) )
            {
                /* If a subscription already exists, don't do anything. */
                if( ( pxSubscriptionList[ lIndex ].pxIncomingPublishCallback == pxIncomingPublishCallback ) &&
                    ( pxSubscriptionList[ lIndex ].pvIncomingPublishCallbackContext == pvIncomingPublishCallbackContext ) )
                {
                    ESP_LOGI( TAG, "Subscription already exists." );
                    xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
                    xReturnStatus = true;
                    break;
                }
            }
        }

        if( xAvailableIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS )
        {
            pxSubscriptionList[ xAvailableIndex ].pcSubscriptionFilterString = pcTopicFilterString;
            pxSubscriptionList[ xAvailableIndex ].usFilterStringLength = usTopicFilterLength;
            pxSubscriptionList[ xAvailableIndex ].pxIncomingPublishCallback = pxIncomingPublishCallback;
            pxSubscriptionList[ xAvailableIndex ].pvIncomingPublishCallbackContext = pvIncomingPublishCallbackContext;
            xReturnStatus = true;
        }
    }

    return xReturnStatus;
}

/*-----------------------------------------------------------*/

void SubscriptionManager_RemoveSubscription( SubscriptionElement_t * pxSubscriptionList,
                                             const char * pcTopicFilterString,
                                             uint16_t usTopicFilterLength )
{
    int32_t lIndex = 0;

    if( ( pxSubscriptionList == NULL ) ||
        ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U ) )
    {
        ESP_LOGE(TAG, "Invalid parameter. pxSubscriptionList=%p, pcTopicFilterString=%p,usTopicFilterLength=%u.", pxSubscriptionList, pcTopicFilterString, ( unsigned int ) usTopicFilterLength );
    }
    else
    {
        for( lIndex = 0; lIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; lIndex++ )
        {
            if( pxSubscriptionList[ lIndex ].usFilterStringLength == usTopicFilterLength )
            {
                if( strncmp( pxSubscriptionList[ lIndex ].pcSubscriptionFilterString, pcTopicFilterString, usTopicFilterLength ) == 0 )
                {
                    memset( &( pxSubscriptionList[ lIndex ] ), 0x00, sizeof( SubscriptionElement_t ) );
                }
            }
        }
    }
}

/*-----------------------------------------------------------*/

bool SubscriptionManager_HandleIncomingPublishes( SubscriptionElement_t * pxSubscriptionList,
                                                  MQTTPublishInfo_t * pxPublishInfo )
{
    int32_t lIndex = 0;
    bool isMatched = false, publishHandled = false;
    
    if( ( pxSubscriptionList == NULL ) ||
        ( pxPublishInfo == NULL ) )
    {
        ESP_LOGE(TAG, "Invalid parameter. pxSubscriptionList=%p, pxPublishInfo=%p,", pxSubscriptionList, pxPublishInfo);
    }   

    else
    {
        for( lIndex = 0; lIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; lIndex++ )
        {
            if( pxSubscriptionList[ lIndex ].usFilterStringLength > 0 )
            {
                MQTT_MatchTopic( pxPublishInfo->pTopicName,
                                 pxPublishInfo->topicNameLength,
                                 pxSubscriptionList[ lIndex ].pcSubscriptionFilterString,
                                 pxSubscriptionList[ lIndex ].usFilterStringLength,
                                 &isMatched );
                

                if( isMatched == true )
                {
                    pxSubscriptionList[ lIndex ].pxIncomingPublishCallback( pxSubscriptionList[ lIndex ].pvIncomingPublishCallbackContext,
                                                                            pxPublishInfo );

                    publishHandled = true;
                }
            }
        }
    }

    return publishHandled;
}
