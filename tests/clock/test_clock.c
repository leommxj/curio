#include "curio_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(time_t utc, int offset, bool seconds, const char *time, const char *date)
{
    curio_clock_text_t out;
    curio_clock_format(utc, offset, seconds, &out);
    assert(!strcmp(out.time, time));
    assert(!strcmp(out.date, date));
}
int main(void)
{
    check(0, 480, false, "--:--", "");
    check(1704067199, 840, true, "--:--:--", "");
    check(1704067200, -720, false, "12:00", "2023-12-31");
    check(1704067200, 840, true, "14:00:00", "2024-01-01");
    check(1704067201, 345, true, "05:45:01", "2024-01-01");
    check(1709251140, 1, false, "00:00", "2024-03-01");
    check(1709164800, -30, false, "23:30", "2024-02-28");
    assert(curio_clock_now() > 1704067200);
    puts("clock tests passed");
}
