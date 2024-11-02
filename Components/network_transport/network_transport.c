#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include <string.h>
#include "sys/socket.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "network_transport.h"
#include "sdkconfig.h"

#define NETWORK_TIMEOUT 5000

#define TAG "network_transport"

Timeouts_t timeouts = { .connectionTimeoutMs = NETWORK_TIMEOUT, .sendTimeoutMs = 10000, .recvTimeoutMs = 2000 };


TlsTransportStatus_t xTlsConnect( NetworkContext_t* pxNetworkContext)
{
    TlsTransportStatus_t xRet = TLS_TRANSPORT_SUCCESS;

    esp_tls_cfg_t xEspTlsConfig = {
        .cacert_buf = (const unsigned char*) ( pxNetworkContext->pcServerRootCA ),
        .cacert_bytes = pxNetworkContext->pcServerRootCASize,
        .clientcert_buf = (const unsigned char*) ( pxNetworkContext->pcClientCert ),
        .clientcert_bytes = pxNetworkContext->pcClientCertSize,
        .skip_common_name = pxNetworkContext->disableSni,
        .alpn_protos = pxNetworkContext->pAlpnProtos,
        .use_secure_element = pxNetworkContext->use_secure_element,
        .ds_data = pxNetworkContext->ds_data,
        .clientkey_buf = ( const unsigned char* )( pxNetworkContext->pcClientKey ),
        .clientkey_bytes = pxNetworkContext->pcClientKeySize,
        .timeout_ms = NETWORK_TIMEOUT,
        .non_block = false,
        .is_plain_tcp = pxNetworkContext->is_plain_tcp
    };

    esp_tls_t* pxTls = esp_tls_init();

    xSemaphoreTake(pxNetworkContext->xTlsContextSemaphore, portMAX_DELAY);
    pxNetworkContext->pxTls = pxTls;

    if (esp_tls_conn_new_sync( pxNetworkContext->pcHostname, 
            strlen( pxNetworkContext->pcHostname ), 
            pxNetworkContext->xPort, 
            &xEspTlsConfig, pxTls) <= 0)
    {
        if (pxNetworkContext->pxTls)
        {
            esp_tls_conn_destroy(pxNetworkContext->pxTls);
            pxNetworkContext->pxTls = NULL;
        }
        xRet = TLS_TRANSPORT_CONNECT_FAILURE;
    }

    xSemaphoreGive(pxNetworkContext->xTlsContextSemaphore);

    return xRet;
}

TlsTransportStatus_t xTlsDisconnect( NetworkContext_t* pxNetworkContext )
{
    BaseType_t xRet = TLS_TRANSPORT_SUCCESS;

    xSemaphoreTake(pxNetworkContext->xTlsContextSemaphore, portMAX_DELAY);
    if (pxNetworkContext->pxTls != NULL && 
        esp_tls_conn_destroy(pxNetworkContext->pxTls) < 0)
    {
        xRet = TLS_TRANSPORT_DISCONNECT_FAILURE;
    }
    pxNetworkContext->pxTls = NULL;
    xSemaphoreGive(pxNetworkContext->xTlsContextSemaphore);

    return xRet;
}
int32_t espTlsTransportSend(NetworkContext_t* pxNetworkContext,
    const void* pvData, size_t uxDataLen)
{
    if (pvData == NULL || uxDataLen == 0)
    {
        return -1;
    }

    int32_t lBytesSent = 0;

    if(pxNetworkContext != NULL && pxNetworkContext->pxTls != NULL)
    {
        xSemaphoreTake(pxNetworkContext->xTlsContextSemaphore, portMAX_DELAY);
        lBytesSent = esp_tls_conn_write(pxNetworkContext->pxTls, pvData, uxDataLen);
        xSemaphoreGive(pxNetworkContext->xTlsContextSemaphore);
    }
    else
    {
        lBytesSent = -1;
    }

    return lBytesSent;
}

int32_t espTlsTransportRecv(NetworkContext_t* pxNetworkContext,
    void* pvData, size_t uxDataLen)
{
    if (pvData == NULL || uxDataLen == 0)
    {
        return -1;
    }
    int32_t lBytesRead = 0;
    if(pxNetworkContext != NULL && pxNetworkContext->pxTls != NULL)
    {
        xSemaphoreTake(pxNetworkContext->xTlsContextSemaphore, portMAX_DELAY);
        lBytesRead = esp_tls_conn_read(pxNetworkContext->pxTls, pvData, uxDataLen);
        xSemaphoreGive(pxNetworkContext->xTlsContextSemaphore);
    }
    else
    {
        return -1; /* pxNetworkContext or pxTls uninitialised */
    }
    if (lBytesRead == ESP_TLS_ERR_SSL_WANT_WRITE  || lBytesRead == ESP_TLS_ERR_SSL_WANT_READ) {
        return 0;
    }
    if (lBytesRead < 0) {
        return lBytesRead;
    }
    if (lBytesRead == 0) {
        /* Connection closed */
        return -1;
    }
    return lBytesRead;
}
const char* TlsTransportStatusToString(TlsTransportStatus_t status)
{
    switch (status)
    {
        case TLS_TRANSPORT_SUCCESS:
            return "TLS_TRANSPORT_SUCCESS: Communication established successfully.";
        case TLS_TRANSPORT_INVALID_PARAMETER:
            return "TLS_TRANSPORT_INVALID_PARAMETER: At least one parameter was invalid.";
        case TLS_TRANSPORT_INSUFFICIENT_MEMORY:
            return "TLS_TRANSPORT_INSUFFICIENT_MEMORY: Insufficient memory required to establish connection.";
        case TLS_TRANSPORT_INVALID_CREDENTIALS:
            return "TLS_TRANSPORT_INVALID_CREDENTIALS: Provided credentials were invalid.";
        case TLS_TRANSPORT_HANDSHAKE_FAILED:
            return "TLS_TRANSPORT_HANDSHAKE_FAILED: Performing TLS handshake with server failed.";
        case TLS_TRANSPORT_INTERNAL_ERROR:
            return "TLS_TRANSPORT_INTERNAL_ERROR: A call to a system API resulted in an internal error.";
        case TLS_TRANSPORT_CONNECT_FAILURE:
            return "TLS_TRANSPORT_CONNECT_FAILURE: Initial connection to the server failed.";
        case TLS_TRANSPORT_DISCONNECT_FAILURE:
            return "TLS_TRANSPORT_DISCONNECT_FAILURE: Failed to disconnect from server.";
        default:
            return "UNKNOWN_STATUS: Unknown TLS transport status.";
    }
}