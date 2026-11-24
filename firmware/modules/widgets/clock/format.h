#pragma once
#include <stdbool.h>
#include <time.h>

typedef struct { char time[9]; char date[16]; } curio_clock_text_t;
void curio_clock_format(time_t utc, int offset_minutes, bool seconds, curio_clock_text_t *out);
