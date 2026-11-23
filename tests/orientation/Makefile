CC ?= cc
BUILD ?= ../../build/orientation-tests
CPPFLAGS += -I../../firmware/components/curio_config/include -Istubs -I../battery/stubs -I../ui/stubs -I../../firmware/main \
            -I../../firmware/components/curio_core/include -I../../firmware/components/curio_layout/include \
            -I../../firmware/components/curio_alerts/include
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
SOURCES := test_orientation.c ../../firmware/main/curio_orientation.c ../../firmware/main/curio_sensors.c \
           ../../firmware/main/curio_orientation.h ../../firmware/main/curio_sensors.h

.PHONY: test sanitize
test: $(BUILD)/test_orientation
	$<
$(BUILD)/test_orientation: $(SOURCES)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_orientation.c ../../firmware/main/curio_orientation.c -lm -o $@
sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer test_orientation.c ../../firmware/main/curio_orientation.c -lm -o $(BUILD)/test_orientation_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/test_orientation_sanitize
