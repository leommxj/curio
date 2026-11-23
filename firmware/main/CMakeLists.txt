idf_component_register(
    SRCS "app_main.c" "curio_network.c" "curio_settings.c" "curio_ui.c" "curio_screen.c" "curio_feedback.c" "curio_battery.c"
         "curio_clock.c" "curio_orientation.c" "curio_sensors.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES curio_core curio_layout curio_alerts curio_config cjson esp-mosaico-bsp esp_mosaico_app_recovery esp_iris esp_codec_dev
                  esp_timer esp_wifi esp_event esp_netif esp_https_server nvs_flash mdns esp_driver_i2c esp_driver_gpio
)

set(web_header "${CMAKE_CURRENT_BINARY_DIR}/curio_web_assets.h")
add_custom_command(OUTPUT "${web_header}"
    COMMAND ${PYTHON} "${PROJECT_DIR}/../scripts/firmware/embed_web.py" "${CMAKE_CURRENT_SOURCE_DIR}/web" "${web_header}"
    DEPENDS "${PROJECT_DIR}/../scripts/firmware/embed_web.py" web/index.html web/style.css web/app.js web/vendor/js-yaml.min.js)
add_custom_target(curio_web_assets DEPENDS "${web_header}")
add_dependencies(${COMPONENT_LIB} curio_web_assets)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

file(GLOB local_config_inputs CONFIGURE_DEPENDS "${PROJECT_DIR}/local/curio_config.h")
if(local_config_inputs)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/local")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CURIO_HAS_LOCAL_CONFIG=1)
endif()
