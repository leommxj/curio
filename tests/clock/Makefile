CC ?= cc
ROOT := ../..
BUILD ?= $(ROOT)/build/clock-tests
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -I$(ROOT)/firmware/main
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
.PHONY: test
test: $(BUILD)/test_clock $(BUILD)/test_sntp
	$(BUILD)/test_clock
	$(BUILD)/test_sntp
$(BUILD)/test_clock: test_clock.c $(ROOT)/firmware/main/curio_clock.c $(ROOT)/firmware/main/curio_clock.h
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_clock.c $(ROOT)/firmware/main/curio_clock.c -o $@

$(BUILD)/test_sntp: test_sntp.c $(ROOT)/firmware/main/curio_clock.c $(ROOT)/firmware/main/curio_clock.h stubs/esp_netif_sntp.h stubs/esp_log.h
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DESP_PLATFORM -Istubs test_sntp.c $(ROOT)/firmware/main/curio_clock.c -o $@
