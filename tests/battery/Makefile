CC ?= cc
BUILD ?= ../../build/battery-tests
CPPFLAGS += -I../../firmware/components/curio_config/include -Istubs -I../ui/stubs -I../../firmware/components/curio_core/include \
            -I../../firmware/components/curio_layout/include -I../../firmware/components/curio_alerts/include
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
SOURCES := test_battery.c ../../firmware/main/curio_battery.c ../../firmware/main/curio_battery.h

.PHONY: test sanitize
test: $(BUILD)/test_battery
	$<
$(BUILD)/test_battery: $(SOURCES)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_battery.c -o $@
sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer test_battery.c -o $(BUILD)/test_battery_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/test_battery_sanitize
