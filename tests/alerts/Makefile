CC ?= cc
CJSON_DIR ?= ../../.deps/cJSON
ALERTS_DIR := ../../firmware/components/curio_alerts
CORE_DIR := ../../firmware/components/curio_core
BUILD ?= ../../build/alerts-tests
CPPFLAGS += -I$(ALERTS_DIR)/include -I$(CORE_DIR)/include -I$(CJSON_DIR)
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
LDLIBS += -lm
SOURCES := test_alerts.c $(ALERTS_DIR)/curio_alerts.c $(CORE_DIR)/curio_core.c $(CJSON_DIR)/cJSON.c
SOURCES += ../../firmware/components/curio_layout/curio_layout.c
include ../../scripts/lib/modules.mk
SOURCES += $(MODULE_ALERT_TEST_SOURCES)

.PHONY: test sanitize clean
test: $(BUILD)/test_alerts
	$<
$(BUILD)/test_alerts: $(SOURCES) $(CORE_DIR)/include/curio_core.h $(ALERTS_DIR)/include/curio_alerts.h $(CJSON_DIR)/cJSON.h
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDLIBS) -o $@
sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $(LDLIBS) -o $(BUILD)/test_alerts_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/test_alerts_sanitize
clean:
	rm -f $(BUILD)/test_alerts $(BUILD)/test_alerts_sanitize

$(BUILD)/test_alerts: $(MODULE_DEPENDENCIES)
