set(COMPONENT_SRCS
	"network_transport.c"
	)

set(COMPONENT_ADD_INCLUDEDIRS
	"include"
	"${CMAKE_CURRENT_LIST_DIR}"
	)

idf_component_register(
    SRCS
        ${COMPONENT_SRCS}
    INCLUDE_DIRS
        ${COMPONENT_ADD_INCLUDEDIRS}
    REQUIRES
        "esp-tls"
)