idf_component_register(SRCS "curio_fonts.c" INCLUDE_DIRS "include"
    REQUIRES lvgl freetype EMBED_FILES "NotoSansSC-Regular-sub.ttf")

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_link_libraries(${lvgl_lib} PRIVATE idf::espressif__freetype)
target_compile_definitions(${lvgl_lib} PRIVATE FT2_BUILD_LIBRARY)
target_link_options(${lvgl_lib} INTERFACE "-Wl,-u,FT_New_Memory")
include("freetype.cmake")
