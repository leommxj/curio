# LVGL supplies FreeType's allocator and memory-file access.
get_target_property(freetype_sources freetype SOURCES)
list(FILTER freetype_sources EXCLUDE REGEX "/ftsystem\\.c$")
set_property(TARGET freetype PROPERTY SOURCES "${freetype_sources}")
