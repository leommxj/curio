idf_component_register(SRCS "curio_alerts.c" ${CURIO_MODULE_ALERTS} "${CURIO_MODULE_BUILD}/alerts.c"
    INCLUDE_DIRS "include" REQUIRES curio_core curio_module)
