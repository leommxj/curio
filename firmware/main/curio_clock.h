#pragma once
#include <stdbool.h>
#include <time.h>

time_t curio_clock_now(void);
#ifdef ESP_PLATFORM
void curio_clock_update(bool enabled, bool connected, bool reconnect, const char *server);
#endif
