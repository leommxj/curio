CC ?= cc
CJSON_DIR ?= ../../.deps/cJSON
CORE_DIR := ../../firmware/components/curio_core
BUILD ?= ../../build/core-tests
CPPFLAGS += -I$(CORE_DIR)/include -I$(CJSON_DIR)
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
LDLIBS += -lm
SOURCES := test_core.c $(CORE_DIR)/curio_core.c $(CJSON_DIR)/cJSON.c

.PHONY: all test sanitize clean
all: $(BUILD)/test_core
$(BUILD)/test_core: $(SOURCES) $(CORE_DIR)/include/curio_core.h $(CJSON_DIR)/cJSON.h
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@
test: $(BUILD)/test_core
	$<
sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) $(LDLIBS) -o $(BUILD)/test_core_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/test_core_sanitize
clean:
	rm -f $(BUILD)/test_core $(BUILD)/test_core_sanitize
