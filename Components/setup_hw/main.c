#include "setup_config.h"
#include "key_value_store.h"

#define WIFI_NAMESPACE "wifi"

static const char *TAG = "WIFI_STA";

static int s_retry_num = 0;

static void prvInitFlash( void );
static void prvWifiInitSta( void );
static bool prvStartWifi( void );


void initHardware()
{
    prvInitFlash();
    prvWifiInitSta();
    prvStartWifi();

}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if( event_base == WIFI_EVENT )
    {
        switch( event_id )
        {
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI( TAG, "Station connected event" );
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI( TAG, "Station disconnected event" );
                wifiConnected = false;
                if (s_retry_num < ESP_MAXIMUM_RETRY){
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "retry to connect to the AP");
                } else {
                    xEventGroupSetBits( wifiEventGroup, WIFI_FAIL_BIT );
                    ESP_LOGI(TAG, "WIFI_FAIL_BIT was successfully set");
                }
                ESP_LOGI(TAG,"connect to the AP fail");
                break;
            case WIFI_EVENT_STA_START:
                ESP_LOGI( TAG, "Station started event" );
                wifiConnected = false;
                break;
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI( TAG, "WiFi scan completed event" );
                break;
            default:
                ESP_LOGE( TAG,
                          "Unknown WiFi event_id occurred: %d",
                          ( int ) event_id );
                break;
        }
    }
    else if( event_base == IP_EVENT )
    {
        switch( event_id )
        {
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI( TAG, "Station acquired IP address event" );
                ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&event->ip_info.ip));
                xEventGroupSetBits( wifiEventGroup, WIFI_CONNECTED_BIT );
                wifiConnected = true;
                break;
            default:
                ESP_LOGE( TAG,
                          "Unknown IP event_id occurred: %d",
                          ( int ) event_id );
                break;
        }
    }

}
static void prvInitFlash(void)
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
/* Read for more information https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#esp32-wi-fi-station-general-scenario */
static void prvWifiInitSta( void )
{
    wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
    esp_event_handler_instance_t instanceAnyId = NULL;
    esp_event_handler_instance_t instanceGotIp = NULL;

    wifiEventGroup = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_init(&initConfig));


    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instanceAnyId));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instanceGotIp));

    ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_STA ) );
    ESP_ERROR_CHECK( esp_wifi_start() );    

}
static bool prvStartWifi( void )
{
    bool success = true;
    wifi_config_t wifiConfig = { 0 };

    size_t ssidLength = 0;
    size_t passphraseLength = 0;
    // Open the "wifi" namespace in read-only mode
    nvs_handle handle;
    ESP_ERROR_CHECK(nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle) != ESP_OK);

    // Load the private key & certificate
    ESP_LOGI(TAG, "Loading wifi");

    char * ssid       = nvs_load_value_if_exist(handle, SSID_KEY, &ssidLength);
    char * passphrase = nvs_load_value_if_exist(handle, PASSPHRASE_KEY, &passphraseLength);
    // We're done with NVS
    nvs_close(handle);
    
    // Check if both items have been correctly retrieved
    if(ssid == NULL || passphrase == NULL) {
        ESP_LOGE(TAG, "ssid or passphrase could not be loaded");
        return false;
    }
    memcpy( wifiConfig.sta.ssid, ssid, ssidLength );
    memcpy( wifiConfig.sta.password, passphrase, passphraseLength );

    free(ssid);
    free(passphrase);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if( ESP_OK != esp_wifi_set_config( WIFI_IF_STA, &wifiConfig ) )
    {
        ESP_LOGE( TAG, "Failed to set WiFi config" );
        success = false;
    }
    if( success )
    {
        EventBits_t waitBits;
        ESP_LOGI( TAG,
                  "Connecting to SSID=%.*s",
                  ( int ) ssidLength,
                   wifiConfig.sta.ssid );

        xEventGroupClearBits( wifiEventGroup,
                              WIFI_CONNECTED_BIT | WIFI_FAIL_BIT );

        esp_wifi_connect();

        waitBits = xEventGroupWaitBits( wifiEventGroup,
                                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                        pdFALSE,
                                        pdFALSE,
                                        portMAX_DELAY );

        if( waitBits & WIFI_CONNECTED_BIT )
        {
            ESP_LOGI( TAG,
                      "Successfully connected to SSID=%.*s",
                      ( int ) ssidLength,
                       wifiConfig.sta.ssid );
        }
        else
        {
            ESP_LOGI( TAG,
                      "Failed to connect to SSID=%.*s",
                      ( int ) ssidLength,
                       wifiConfig.sta.ssid );
            success = false;
        }
    }

    return success;
}