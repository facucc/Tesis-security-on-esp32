idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "setup_wifi" 
                            "nvs_flash"
                             "esp_event"
                             "esp_netif"
                             "esp_wifi"
                    )
