CC ?= cc
ROOT := ../..
BUILD ?= $(ROOT)/build/layout-tests
CPPFLAGS += -I$(ROOT)/firmware/components/curio_core/include -I$(ROOT)/firmware/components/curio_layout/include
SOURCES := test_layout.c $(ROOT)/firmware/components/curio_layout/curio_layout.c
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
.PHONY: test sanitize clean
test: $(BUILD)/test_layout
	$<
$(BUILD)/test_layout: $(SOURCES) $(ROOT)/firmware/components/curio_core/include/curio_core.h $(ROOT)/firmware/components/curio_layout/include/curio_layout.h
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@
sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o $(BUILD)/test_layout_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/test_layout_sanitize
clean:
	rm -f $(BUILD)/test_layout $(BUILD)/test_layout_sanitize
