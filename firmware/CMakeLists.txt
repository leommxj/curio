cmake_minimum_required(VERSION 3.16)

set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.deps/esp-mosaico-utils" CACHE PATH "Mosaico utilities checkout")
set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.deps/esp-mosaico-bsp" CACHE PATH "Mosaico BSP checkout")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()
list(APPEND EXTRA_COMPONENT_DIRS
    "${MOSAICO_BSP_ROOT}/components/esp-mosaico-bsp"
    "${MOSAICO_UTILS_ROOT}/ESP-Iris/components/esp_iris"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_app_recovery")
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/mosaico_idf_project.cmake")
list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.application.defaults")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CURIO_MODULE_BUILD "${CMAKE_CURRENT_LIST_DIR}/../build/modules")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/modules.py"
    --output "${CURIO_MODULE_BUILD}" RESULT_VARIABLE CURIO_MODULE_RESULT)
if(NOT CURIO_MODULE_RESULT EQUAL 0)
    message(FATAL_ERROR "Module generation failed")
endif()
include("${CURIO_MODULE_BUILD}/modules.cmake")
file(GLOB_RECURSE CURIO_MODULE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/modules/*/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/modules/*/web.js"
    "${CMAKE_CURRENT_LIST_DIR}/modules/*/style.css")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/modules/registry.json" ${CURIO_MODULE_INPUTS})
project(curio VERSION 0.2.0)

partition_table_add_check_size_target(curio_check_size
    DEPENDS gen_project_binary
    BINARY_PATH "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
    PARTITION_TYPE app PARTITION_SUBTYPE ota_0)
add_dependencies(app curio_check_size)

include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
