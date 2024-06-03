idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES "mqtt_agent"
                             "ota_agent"         
                    )
