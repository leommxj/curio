idf_component_register(SRCS "curio_layout.c" ${CURIO_MODULE_WIDGETS} "${CURIO_MODULE_BUILD}/widgets.c"
    INCLUDE_DIRS "include" REQUIRES curio_core curio_module)
