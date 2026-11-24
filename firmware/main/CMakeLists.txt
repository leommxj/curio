idf_component_register(
    SRCS ${CURIO_MODULE_VIEWS} ${CURIO_MODULE_SERVICES} "${CURIO_MODULE_BUILD}/widget_views.c" "${CURIO_MODULE_BUILD}/widget_services.c" "app_main.c" "curio_network.c" "curio_settings.c" "curio_ui.c" "curio_screen.c" "curio_feedback.c" "curio_battery.c"
         "curio_clock.c" "curio_orientation.c" "curio_sensors.c" "curio_tls.c" "curio_provision.c" "curio_lvgl_memory.c" "curio_wifi.c" "curio_buttons.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES curio_core curio_layout curio_alerts curio_config curio_fonts cjson esp-mosaico-bsp esp_mosaico_app_recovery esp_iris esp_codec_dev
                  esp_timer esp_wifi esp_event esp_netif esp_https_server esp_http_client nvs_flash mdns esp_driver_i2c esp_driver_gpio mbedtls
)

set(web_header "${CMAKE_CURRENT_BINARY_DIR}/curio_web_assets.h")
add_custom_command(OUTPUT "${web_header}"
    COMMAND ${PYTHON} "${PROJECT_DIR}/../scripts/firmware/embed_web.py" "${CMAKE_CURRENT_SOURCE_DIR}/web" "${web_header}" --modules "${CURIO_MODULE_BUILD}"
    DEPENDS ${CURIO_MODULE_INPUTS} "${PROJECT_DIR}/../scripts/firmware/embed_web.py" web/index.html web/style.css web/app.js web/setup.html web/setup.js web/vendor/js-yaml.min.js)
add_custom_target(curio_web_assets DEPENDS "${web_header}")
add_dependencies(${COMPONENT_LIB} curio_web_assets)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
