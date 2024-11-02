include("${CMAKE_CURRENT_LIST_DIR}/../../Libs/iot-core-mqtt-file-downloader/mqttFileDownloaderFilePaths.cmake")

idf_component_register(
  SRCS ${MQTT_FILE_DOWNLOADER_SOURCES}
  INCLUDE_DIRS ${MQTT_FILE_DOWNLOADER_INCLUDES}
  PRIV_REQUIRES coreJSON tinycbor)

component_compile_options(-Wno-error=format= -Wno-format)