CC ?= cc
ROOT := ../..
CJSON_DIR ?= $(ROOT)/.deps/cJSON
BUILD ?= $(ROOT)/build/weather-tests
CPPFLAGS += -I$(ROOT)/firmware/components/curio_core/include -I$(ROOT)/firmware/components/curio_layout/include -I$(ROOT)/firmware/modules/widgets/weather -I$(CJSON_DIR)
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
SOURCES := test_weather.c $(ROOT)/firmware/modules/widgets/weather/weather.c $(CJSON_DIR)/cJSON.c
.PHONY: test sanitize
test:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -lm -o $(BUILD)/test_weather
	$(BUILD)/test_weather
sanitize:
	$(MAKE) test CFLAGS='$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer'
