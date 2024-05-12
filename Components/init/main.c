// user headers
#include "wifi_config.h"
// system headers
#include "nvs_flash.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_log.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
//#include "backoff_algorithm.h"
//#include "core_mqtt.h"
#include <unistd.h>
/* POSIX includes. */
#include <unistd.h>

#include "aws_headers.h"

static const char *TAG = "INIT";

static void usr_prv_init_hw(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %"PRIu32" bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init(); 
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    
}
// static uint32_t generateRandomNumber()
// {
//     return( rand() );
// }
// static int initialize_network( NetworkContext_t * pNetworkContext )
// {
//     int returnStatus = EXIT_SUCCESS;
//     BackoffAlgorithmStatus_t backoffAlgStatus = BackoffAlgorithmSuccess;
//     TlsTransportStatus_t tlsStatus = TLS_TRANSPORT_SUCCESS;
//     BackoffAlgorithmContext_t reconnectParams;

//     pNetworkContext->pcHostname = AWS_IOT_ENDPOINT;
//     pNetworkContext->xPort = AWS_MQTT_PORT;
//     pNetworkContext->pxTls = NULL;
//     pNetworkContext->xTlsContextSemaphore = xSemaphoreCreateMutexStatic(&xTlsContextSemaphoreBuffer);

//     pNetworkContext->disableSni = 0;
//     pNetworkContext->pAlpnProtos = NULL;

//     uint16_t nextRetryBackOff;

//     /* Initialize credentials for establishing TLS session. */
//     pNetworkContext->pcServerRootCA = root_cert_auth_start;
//     pNetworkContext->pcServerRootCASize = root_cert_auth_end - root_cert_auth_start;
//     pNetworkContext->pcClientCert = client_cert_start;
//     pNetworkContext->pcClientCertSize = client_cert_end - client_cert_start;
//     pNetworkContext->pcClientKey = client_key_start;
//     pNetworkContext->pcClientKeySize = client_key_end - client_key_start;
//     /* Initialize reconnect attempts and interval */
//     BackoffAlgorithm_InitializeParams( &reconnectParams,
//                                        CONNECTION_RETRY_BACKOFF_BASE_MS,
//                                        CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
//                                        CONNECTION_RETRY_MAX_ATTEMPTS );

//     /* Attempt to connect to MQTT broker. If connection fails, retry after
//      * a timeout. Timeout value will exponentially increase until maximum
//      * attempts are reached.
//      */
//     do
//     {
//         /* Establish a TLS session with the MQTT broker. This example connects
//          * to the MQTT broker as specified in AWS_IOT_ENDPOINT and AWS_MQTT_PORT
//          * at the demo config header. */
//         ESP_LOGI(TAG, "Establishing a TLS session to %.*s:%d.",
//                    AWS_IOT_ENDPOINT_LENGTH,
//                    AWS_IOT_ENDPOINT,
//                    AWS_MQTT_PORT );

//         tlsStatus = xTlsConnect ( pNetworkContext );

//         // if( tlsStatus == TLS_TRANSPORT_SUCCESS )
//         // {
//         //     printf("TLS session success");
//         // }
//         if( returnStatus == EXIT_FAILURE )
//         {
//             /* Generate a random number and get back-off value (in milliseconds) for the next connection retry. */
//             backoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &reconnectParams, generateRandomNumber(), &nextRetryBackOff );

//             if( backoffAlgStatus == BackoffAlgorithmRetriesExhausted )
//             {
//                 //LogError( ( "Connection to the broker failed, all attempts exhausted." ) );
//                 returnStatus = EXIT_FAILURE;
//             }
//             else if( backoffAlgStatus == BackoffAlgorithmSuccess )
//             {
//                 // ESP_LOGI( ( "Connection to the broker failed. Retrying connection "
//                 //            "after %hu ms backoff.",
//                 //            ( unsigned short ) nextRetryBackOff ) );
//                 vTaskDelay( nextRetryBackOff/portTICK_PERIOD_MS );
//             }
//         }
//     } while( ( returnStatus == EXIT_FAILURE ) && ( backoffAlgStatus == BackoffAlgorithmSuccess ) );

//     return returnStatus;
// }
void usr_start_app(void)
{
    
    usr_prv_init_hw();
    usr_wifi_init_sta();
    start_wifi();

    while (1)
    {
        printf("Hello world\n");
        vTaskDelay(5000/ portTICK_PERIOD_MS);
    }
}
// void usr_prv_network_transport_connect()
// {
//     NetworkContext_t xNetworkContext = { 0 };
//     struct timespec tp;

//     /* Seed pseudo random number generator (provided by ISO C standard library) for
//      * use by retry utils library when retrying failed network operations. */

//     /* Get current time to seed pseudo random number generator. */
//     ( void ) clock_gettime( CLOCK_REALTIME, &tp );
//     /* Seed pseudo random number generator with nanoseconds. */
//     srand( tp.tv_nsec );

//     initialize_network(&xNetworkContext);



// }