idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "nvs_flash"
                             "esp_event"
                             "esp_netif"
                             "esp_wifi"
                             "key_value_store"
                    )
