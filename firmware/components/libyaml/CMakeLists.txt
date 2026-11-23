set(YAML_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../.deps/libyaml")
set(YAML_SOURCES "${YAML_DIR}/src/api.c" "${YAML_DIR}/src/reader.c" "${YAML_DIR}/src/scanner.c"
    "${YAML_DIR}/src/parser.c" "${YAML_DIR}/src/loader.c" "${YAML_DIR}/src/writer.c"
    "${YAML_DIR}/src/emitter.c" "${YAML_DIR}/src/dumper.c")
idf_component_register(SRCS ${YAML_SOURCES} "curio_yaml_alloc.c"
    INCLUDE_DIRS "${YAML_DIR}/include"
    PRIV_INCLUDE_DIRS "${YAML_DIR}/src"
    PRIV_REQUIRES heap)
set_source_files_properties(${YAML_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "malloc=curio_yaml_malloc;realloc=curio_yaml_realloc;free=curio_yaml_free;strdup=curio_yaml_strdup"
    COMPILE_OPTIONS "-Wno-unused-value;-include;${CMAKE_CURRENT_LIST_DIR}/curio_yaml_alloc.h")
target_compile_definitions(${COMPONENT_LIB} PRIVATE YAML_VERSION_STRING="0.2.5"
    YAML_VERSION_MAJOR=0 YAML_VERSION_MINOR=2 YAML_VERSION_PATCH=5)
