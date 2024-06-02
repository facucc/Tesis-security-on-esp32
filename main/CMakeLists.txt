idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES "init"
                             "mqtt_agent"
                             "ota_agent"         
                    )
