CC ?= cc
LVGL_DIR ?= ../../firmware/managed_components/lvgl__lvgl
LVGL_DIR := $(abspath $(LVGL_DIR))
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/ui-tests
rwildcard = $(foreach d,$(wildcard $(1)/*),$(call rwildcard,$(d),$(2)) $(filter $(subst *,%,$(2)),$(d)))
LVGL_SOURCES := $(call rwildcard,$(LVGL_DIR)/src,*.c)
LVGL_OBJECTS := $(patsubst $(LVGL_DIR)/src/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SOURCES))
APP_SOURCES := $(ROOT)/firmware/components/curio_core/curio_core.c $(ROOT)/firmware/components/curio_layout/curio_layout.c $(ROOT)/.deps/cJSON/cJSON.c
APP_OBJECTS := $(BUILD)/curio_core.o $(BUILD)/curio_layout.o $(BUILD)/cJSON.o $(BUILD)/curio_clock.o $(BUILD)/test_ui.o
CPPFLAGS += -I$(ROOT)/firmware/components/curio_config/include -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_14=1 -DLV_FONT_MONTSERRAT_18=1 -DLV_FONT_MONTSERRAT_26=1 -DLV_FONT_MONTSERRAT_32=1 -DLV_FONT_MONTSERRAT_48=1 -I$(LVGL_DIR) -I$(CURDIR)/stubs -I$(ROOT)/firmware/components/curio_core/include -I$(ROOT)/firmware/components/curio_layout/include -I$(ROOT)/firmware/components/curio_alerts/include -I$(ROOT)/.deps/cJSON
CFLAGS ?= -O0 -g
CFLAGS += -MMD -MP
.PHONY: test
test: $(BUILD)/test_ui
	$(BUILD)/test_ui
$(BUILD)/test_ui: $(LVGL_OBJECTS) $(APP_OBJECTS)
	$(CC) $^ -lm -o $@
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
$(BUILD)/curio_clock.o: $(ROOT)/firmware/main/curio_clock.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCURIO_CLOCK_FORMAT_ONLY -c $< -o $@
$(BUILD)/test_ui.o: test_ui.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
-include $(LVGL_OBJECTS:.o=.d) $(APP_OBJECTS:.o=.d)
