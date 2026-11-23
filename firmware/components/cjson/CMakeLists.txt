set(CJSON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.deps/cJSON")
idf_component_register(SRCS "${CJSON_ROOT}/cJSON.c" INCLUDE_DIRS "${CJSON_ROOT}")
