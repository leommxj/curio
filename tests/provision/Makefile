CC ?= cc
ROOT := ../..
BUILD ?= $(ROOT)/build/provision-tests
YAML := $(ROOT)/.deps/libyaml
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Istubs -I../settings/stubs -I../ui/stubs \
            -I$(ROOT)/firmware/components/curio_config/include -I$(YAML)/include -I$(YAML)/src \
            -DYAML_VERSION_STRING='"0.2.5"' -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 \
            -I$(ROOT)/firmware/components/curio_core/include -I$(ROOT)/firmware/components/curio_layout/include \
            -I$(ROOT)/firmware/components/curio_alerts/include -I$(ROOT)/.deps/cJSON
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -Wno-unused-value
SOURCES := test_provision.c $(ROOT)/firmware/components/curio_config/curio_config.c $(wildcard $(YAML)/src/*.c) \
           $(ROOT)/firmware/components/curio_core/curio_core.c $(ROOT)/firmware/components/curio_layout/curio_layout.c \
           $(ROOT)/firmware/components/curio_alerts/curio_alerts.c $(ROOT)/.deps/cJSON/cJSON.c
include ../../scripts/lib/modules.mk
HEADERS := $(ROOT)/firmware/main/curio_settings.c $(ROOT)/firmware/main/curio_provision.c \
           $(ROOT)/firmware/main/curio_app.h $(wildcard stubs/*.h ../settings/stubs/*.h ../settings/stubs/freertos/*.h)

.PHONY: test sanitize
test: $(BUILD)/test_provision
	$<
$(BUILD)/test_provision: $(SOURCES) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -lm -pthread -o $@

$(BUILD)/test_provision: $(MODULE_DEPENDENCIES)

sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -lm -pthread -o $(BUILD)/test_provision_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/test_provision_sanitize
