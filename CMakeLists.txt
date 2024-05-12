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

set(LIBS_DIR "${FETCHCONTENT_BASE_DIR}" CACHE STRING "Ruta de la carpeta Libs")

FetchContent_Declare(
  coreMQTT-Agent
  GIT_REPOSITORY https://github.com/FreeRTOS/coreMQTT-Agent.git
  GIT_TAG v1.2.0
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/coreMQTT-Agent) 

FetchContent_Declare(
  backoffAlgorithm
  GIT_REPOSITORY https://github.com/FreeRTOS/backoffAlgorithm.git
  GIT_TAG v1.4.0
  SOURCE_DIR ${FETCHCONTENT_BASE_DIR}/backoffAlgorithm) 

FetchContent_MakeAvailable(coreMQTT-Agent backoffAlgorithm)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(BACKOFF_ALGORITHM_PATH ${backoffalgorithm_SOURCE_DIR} CACHE STRING "")
message(STATUS "LIBS_DIR: ${LIBS_DIR}")

project(tesis)
