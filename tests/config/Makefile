CC ?= cc
ROOT := ../..
BUILD ?= $(ROOT)/build/config-tests
YAML := $(ROOT)/.deps/libyaml
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -I$(ROOT)/firmware/components/curio_config/include -I$(ROOT)/firmware/components/curio_layout/include -I$(ROOT)/firmware/components/curio_alerts/include -I$(ROOT)/firmware/components/curio_core/include -I$(ROOT)/.deps/cJSON -I$(YAML)/include -I$(YAML)/src -DYAML_VERSION_STRING='"0.2.5"' -DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -Wno-unused-value
SOURCES := test_config.c $(ROOT)/firmware/components/curio_config/curio_config.c $(ROOT)/firmware/components/curio_layout/curio_layout.c $(ROOT)/firmware/components/curio_alerts/curio_alerts.c $(ROOT)/firmware/components/curio_core/curio_core.c $(ROOT)/.deps/cJSON/cJSON.c $(wildcard $(YAML)/src/*.c)
include ../../scripts/lib/modules.mk
OBJECTS := $(foreach source,$(SOURCES),$(BUILD)/objects/$(patsubst /%,%,$(abspath $(source:.c=.o))))
.PHONY: test sanitize
test: $(BUILD)/test_config
	$<
$(BUILD)/test_config: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -lm -o $@
define compile
$(BUILD)/objects/$(patsubst /%,%,$(abspath $(1:.c=.o))): $(1)
	@mkdir -p $$(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $$< -o $$@
endef
$(foreach source,$(SOURCES),$(eval $(call compile,$(source))))
-include $(OBJECTS:.o=.d)
sanitize:
	$(MAKE) BUILD="$(BUILD)/sanitize" CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer" test

$(BUILD)/test_config: $(MODULE_DEPENDENCIES)

$(OBJECTS): $(MODULE_DEPENDENCIES)
