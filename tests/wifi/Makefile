CC ?= cc
ROOT := ../..
BUILD ?= $(ROOT)/build/wifi-tests
CPPFLAGS += -I$(ROOT)/firmware/main -Istubs -I$(ROOT)/firmware/components/curio_config/include \
            -I$(ROOT)/firmware/components/curio_layout/include -I$(ROOT)/firmware/components/curio_alerts/include \
            -I$(ROOT)/firmware/components/curio_core/include
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
SOURCES := test_wifi.c $(ROOT)/firmware/main/curio_wifi.c
HEADERS := $(ROOT)/firmware/main/curio_wifi.h $(ROOT)/firmware/components/curio_config/include/curio_config_model.h $(wildcard stubs/*.h)
.PHONY: test sanitize
test: $(BUILD)/test_wifi
	$<
$(BUILD)/test_wifi: $(SOURCES) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@
sanitize:
	$(MAKE) BUILD="$(BUILD)/sanitize" CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer" test
