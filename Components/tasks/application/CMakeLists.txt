idf_component_register(SRCS "src/main.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "coreMQTT-Agent"
                             "mqtt_agent"
                             "key_value_store"
                             "queue_handler"
                    )