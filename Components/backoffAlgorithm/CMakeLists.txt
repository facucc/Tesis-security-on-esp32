include("${CMAKE_CURRENT_LIST_DIR}/../../Libs/backoffAlgorithm/backoffAlgorithmFilePaths.cmake")


idf_component_register(SRCS ${BACKOFF_ALGORITHM_SOURCES}
                       INCLUDE_DIRS ${BACKOFF_ALGORITHM_INCLUDE_PUBLIC_DIRS})