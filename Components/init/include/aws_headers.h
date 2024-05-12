#define AWS_IOT_ENDPOINT  "a3rouz74n8lt91-ats.iot.us-east-1.amazonaws.com"
#define AWS_MQTT_PORT 8883
/**
 * @brief Length of MQTT server host name.
 */
#define AWS_IOT_ENDPOINT_LENGTH ( ( uint16_t ) ( sizeof( AWS_IOT_ENDPOINT ) - 1 ) )
/**
 * @brief The maximum number of retries for connecting to server.
 */
#define CONNECTION_RETRY_MAX_ATTEMPTS            ( 5U )

/**
 * @brief The maximum back-off delay (in milliseconds) for retrying connection to server.
 */
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS    ( 5000U )

/**
 * @brief The base back-off delay (in milliseconds) to use for connection retry attempts.
 */
#define CONNECTION_RETRY_BACKOFF_BASE_MS         ( 500U )