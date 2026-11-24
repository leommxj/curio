#include "format.h"
#include <string.h>

void curio_clock_format(time_t utc, int offset_minutes, bool seconds, curio_clock_text_t *out)
{
    strcpy(out->time, seconds ? "--:--:--" : "--:--");
    out->date[0] = 0;
    if (utc < 1704067200) return;
    time_t local = utc + offset_minutes * 60;
    struct tm value;
    if (!gmtime_r(&local, &value)) return;
    strftime(out->time, sizeof(out->time), seconds ? "%H:%M:%S" : "%H:%M", &value);
    strftime(out->date, sizeof(out->date), "%Y-%m-%d", &value);
}
