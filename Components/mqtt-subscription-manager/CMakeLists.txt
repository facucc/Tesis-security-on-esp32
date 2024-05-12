set(SRCS
    "src/mqtt_subscription_manager.c"
)
idf_component_register(SRCS ${SRCS}
                    INCLUDE_DIRS "include"
                    REQUIRES "coreMQTT-Agent"
                    )