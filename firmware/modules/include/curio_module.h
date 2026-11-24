#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>

bool curio_fields(const cJSON *, const char *common, const char *specific);
bool curio_string(const cJSON *, const char *, char *, size_t, const char *fallback);
bool curio_number(const cJSON *, const char *, double fallback, double min, double max, bool whole, double *);
bool curio_boolean(const cJSON *, const char *, bool fallback, bool *);
bool curio_put_string(cJSON *, const char *, const char *);
bool curio_put_number(cJSON *, const char *, double);
bool curio_put_bool(cJSON *, const char *, bool);
bool curio_identifier(const char *, size_t max);
bool curio_unit(const char *);
