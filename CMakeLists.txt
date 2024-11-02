# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include(FetchContent)


set(EXTRA_COMPONENT_DIRS Components
                         Components/tasks
  )

set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/Libs")

FetchContent_Declare(
  coreMQTT-Agent
  GIT_REPOSITORY https://github.com/FreeRTOS/coreMQTT-Agent.git
  #GIT_TAG v1.2.0
  GIT_TAG 498e283825dac3d45fa2f5cd47a061771896bd12
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/coreMQTT-Agent) 

FetchContent_Declare(
  backoffAlgorithm
  GIT_REPOSITORY https://github.com/FreeRTOS/backoffAlgorithm.git
  GIT_TAG v1.4.0
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/backoffAlgorithm) 
FetchContent_Declare(
  coreJSON
  GIT_REPOSITORY https://github.com/FreeRTOS/coreJSON.git
  GIT_TAG v3.3.0
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/coreJSON)
FetchContent_Declare(
  tinycbor
  GIT_REPOSITORY https://github.com/intel/tinycbor.git
  GIT_TAG 26c63e3d5977f77a6483edde4519489254670375
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/tinycbor)  
FetchContent_Declare(
  jobs
  GIT_REPOSITORY https://github.com/aws/Jobs-for-AWS-IoT-embedded-sdk.git
  GIT_TAG 02e343e93e167dd8e8795e8e4f708ae1e07a2400
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/jobs) 

FetchContent_Declare(
  iot-core-mqtt-file-downloader
  GIT_REPOSITORY https://github.com/aws/aws-iot-core-mqtt-file-streams-embedded-c.git
  GIT_TAG v1.0.0
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/iot-core-mqtt-file-downloader)
  
FetchContent_Declare(
  fleet-provisioning
  GIT_REPOSITORY https://github.com/aws/Fleet-Provisioning-for-AWS-IoT-embedded-sdk
  GIT_TAG v1.2.1
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/fleet-provisioning) 
  
FetchContent_MakeAvailable(coreMQTT-Agent backoffAlgorithm coreJSON tinycbor jobs iot-core-mqtt-file-downloader fleet-provisioning)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tesis)