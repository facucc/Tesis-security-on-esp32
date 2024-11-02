set(TINYCBOR_DIR "${CMAKE_CURRENT_LIST_DIR}/../../Libs/tinycbor/src")

idf_component_register(
  SRCS ${TINYCBOR_DIR}/cborparser.c ${TINYCBOR_DIR}/cborencoder.c
       ${TINYCBOR_DIR}/cborencoder_close_container_checked.c
  INCLUDE_DIRS ${TINYCBOR_DIR})