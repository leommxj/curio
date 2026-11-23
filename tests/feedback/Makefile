CC ?= cc
BUILD ?= ../../build/feedback-tests
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS += -Istubs
.PHONY: test sanitize clean
test: $(BUILD)/test_feedback
	$<
$(BUILD)/test_feedback: test_feedback.c ../../firmware/main/curio_feedback.c ../../firmware/main/curio_feedback.h $(wildcard stubs/*.h stubs/*/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
sanitize:
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer test_feedback.c -o $(BUILD)/test_feedback_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD)/test_feedback_sanitize
clean:
	rm -f $(BUILD)/test_feedback $(BUILD)/test_feedback_sanitize
