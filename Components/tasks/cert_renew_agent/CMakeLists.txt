idf_component_register(SRCS "src/main.c"
                            "src/gen_csr.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "coreMQTT-Agent"
                             "mqtt_agent"
                             "key_value_store"
                             "queue_handler"
                             "mbedtls"
                    )