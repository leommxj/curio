CC ?= cc
LVGL_DIR ?= ../../firmware/managed_components/lvgl__lvgl
LVGL_DIR := $(abspath $(LVGL_DIR))
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/ui-tests
FONTS := $(ROOT)/firmware/components/curio_fonts
FREETYPE_DIR ?= $(ROOT)/firmware/managed_components/espressif__freetype/freetype
FREETYPE := $(BUILD)/freetype/freetype/libfreetype.a
CMAKE ?= cmake
rwildcard = $(foreach d,$(wildcard $(1)/*),$(call rwildcard,$(d),$(2)) $(filter $(subst *,%,$(2)),$(d)))
LVGL_SOURCES := $(call rwildcard,$(LVGL_DIR)/src,*.c)
LVGL_OBJECTS := $(patsubst $(LVGL_DIR)/src/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SOURCES))
APP_SOURCES := $(ROOT)/firmware/components/curio_core/curio_core.c $(ROOT)/firmware/components/curio_layout/curio_layout.c $(ROOT)/.deps/cJSON/cJSON.c
include ../../scripts/lib/modules.mk
MODULE_UI_SOURCES := $(MODULE_SOURCES) $(MODULE_VIEW_SOURCES) $(ROOT)/firmware/components/curio_alerts/curio_alerts.c
MODULE_UI_OBJECTS := $(foreach source,$(MODULE_UI_SOURCES),$(BUILD)/modules/$(patsubst /%,%,$(abspath $(source:.c=.o))))
APP_OBJECTS := $(MODULE_UI_OBJECTS) $(BUILD)/curio_core.o $(BUILD)/curio_layout.o $(BUILD)/cJSON.o $(BUILD)/test_ui.o $(BUILD)/curio_fonts.o $(BUILD)/font.o
CPPFLAGS += -I$(FONTS)/include -I$(FREETYPE_DIR)/include -DLV_USE_FREETYPE=1 -DLV_FREETYPE_USE_LVGL_PORT=1 -DLV_FREETYPE_CACHE_FT_GLYPH_CNT=64 -DLV_USE_FS_MEMFS=1 -DLV_FS_MEMFS_LETTER=77
CPPFLAGS += -DFT2_BUILD_LIBRARY -DLV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB -DLV_USE_SNAPSHOT=1
CPPFLAGS += -I$(ROOT)/firmware/main -I$(ROOT)/firmware/components/curio_config/include -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_14=1 -DLV_FONT_MONTSERRAT_18=1 -DLV_FONT_MONTSERRAT_26=1 -DLV_FONT_MONTSERRAT_32=1 -DLV_FONT_MONTSERRAT_48=1 -I$(LVGL_DIR) -I$(CURDIR)/stubs -I$(ROOT)/firmware/components/curio_core/include -I$(ROOT)/firmware/components/curio_layout/include -I$(ROOT)/firmware/components/curio_alerts/include -I$(ROOT)/.deps/cJSON
CFLAGS ?= -O0 -g
CFLAGS += -MMD -MP
.PHONY: test
test: $(BUILD)/test_ui
	$(BUILD)/test_ui
$(BUILD)/test_ui: $(LVGL_OBJECTS) $(APP_OBJECTS) $(FREETYPE)
	$(CC) $^ -lm -o $@
$(FREETYPE): freetype/CMakeLists.txt $(FONTS)/freetype.cmake
	$(CMAKE) -S freetype -B $(BUILD)/freetype -DFREETYPE_DIR=$(FREETYPE_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(BUILD)/freetype -j 4
$(BUILD)/curio_fonts.o: $(FONTS)/curio_fonts.c $(FONTS)/include/curio_fonts.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/font.o: $(FONTS)/NotoSansSC-Regular-sub.ttf
	@mkdir -p $(@D)
	printf '%s\n' '.section .rodata' '.global _binary_NotoSansSC_Regular_sub_ttf_start' '.global _binary_NotoSansSC_Regular_sub_ttf_end' '_binary_NotoSansSC_Regular_sub_ttf_start:' '.incbin "$(FONTS)/NotoSansSC-Regular-sub.ttf"' '_binary_NotoSansSC_Regular_sub_ttf_end:' '.section .note.GNU-stack,"",@progbits' | $(CC) -x assembler -c -o $@ -
$(BUILD)/lvgl/%.o: $(LVGL_DIR)/src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/curio_core.o: $(word 1,$(APP_SOURCES))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/curio_layout.o: $(word 2,$(APP_SOURCES))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/cJSON.o: $(word 3,$(APP_SOURCES))
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/test_ui.o: test_ui.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
-include $(LVGL_OBJECTS:.o=.d) $(APP_OBJECTS:.o=.d)

$(LVGL_OBJECTS) $(APP_OBJECTS): Makefile

define compile_module
$(BUILD)/modules/$(patsubst /%,%,$(abspath $(1:.c=.o))): $(1)
	@mkdir -p $$(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $$< -o $$@
endef
$(foreach source,$(MODULE_UI_SOURCES),$(eval $(call compile_module,$(source))))

$(MODULE_UI_OBJECTS): $(MODULE_DEPENDENCIES) $(wildcard $(ROOT)/firmware/modules/include/*.h)
