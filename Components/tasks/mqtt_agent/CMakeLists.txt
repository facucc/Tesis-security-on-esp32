idf_component_register(SRCS "src/main.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "coreMQTT-Agent"
                             "backoffAlgorithm"
                             "mqtt-subscription-manager"
                             "network_transport"
                             "key_value_store"
                             "logging"
                    )
