include("${CMAKE_CURRENT_LIST_DIR}/../../Libs/fleet-provisioning/fleetprovisioningFilePaths.cmake")

idf_component_register(
  SRCS ${FLEET_PROVISIONING_SOURCES}
  INCLUDE_DIRS ${FLEET_PROVISIONING_INCLUDE_PUBLIC_DIRS}
  PRIV_REQUIRES coreJSON tinycbor
)

message(STATUS "Component library name: ${COMPONENT_LIB}")
target_compile_options(${COMPONENT_LIB} PRIVATE -DFLEET_PROVISIONING_DO_NOT_USE_CUSTOM_CONFIG)