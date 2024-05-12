include("${CMAKE_CURRENT_LIST_DIR}/../../Libs/coreMQTT-Agent/mqttAgentFilePaths.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../Libs/coreMQTT-Agent/source/dependency/coreMQTT/mqttFilePaths.cmake")


set(COREMQTT_AGENT_PORT_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/port
)

set(COREMQTT_AGENT_CONFIG_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/config
)

set(COREMQTT_AGENT_INCLUDE_DIRS
    ${MQTT_AGENT_INCLUDE_PUBLIC_DIRS}
    ${COREMQTT_AGENT_PORT_INCLUDE_DIRS}
    ${COREMQTT_AGENT_CONFIG_INCLUDE_DIRS}
)

set(COREMQTT_AGENT_PORT_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/port/freertos_agent_message.c
    ${CMAKE_CURRENT_LIST_DIR}/port/freertos_command_pool.c
)

set(COREMQTT_AGENT_SRCS
     ${MQTT_SOURCES}
     ${MQTT_SERIALIZER_SOURCES}
     ${MQTT_AGENT_SOURCES}
     ${COREMQTT_AGENT_PORT_SRCS}
)
idf_component_register(
    SRCS
        ${COREMQTT_AGENT_SRCS}
    INCLUDE_DIRS
        ${COREMQTT_AGENT_INCLUDE_DIRS}
        ${MQTT_INCLUDE_PUBLIC_DIRS}
    REQUIRES
          logging
)

# set(MQTT_CONFIG_DIR ${CMAKE_CURRENT_LIST_DIR}/config)

# set( MQTT_AGENT_PORT_INCLUDE_PUBLIC_DIRS
#      "${CMAKE_CURRENT_LIST_DIR}/port" )



# idf_component_register(SRCS ${MQTT_SOURCES}
#                             ${MQTT_SERIALIZER_SOURCES}
#                             ${MQTT_AGENT_SOURCES}
#                             ${MQTT_AGENT_PORT_INCLUDE_PUBLIC_DIRS}
#                        INCLUDE_DIRS ${MQTT_CONFIG_DIR}
#                                     ${MQTT_INCLUDE_PUBLIC_DIRS}
#                                     ${MQTT_AGENT_INCLUDE_PUBLIC_DIRS}
#                                     ${MQTT_AGENT_PORT_INCLUDE_PUBLIC_DIRS}
                                    
#                        REQUIRES logging)