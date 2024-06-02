set(COMPONENT_SRCS
	"src/queue_handler.c"
	)

set(COMPONENT_ADD_INCLUDEDIRS
	"include"
	)

idf_component_register(
    SRCS
        ${COMPONENT_SRCS}
    INCLUDE_DIRS
        ${COMPONENT_ADD_INCLUDEDIRS}
    REQUIRES 
        "iot-core-mqtt-file-downloader"
)