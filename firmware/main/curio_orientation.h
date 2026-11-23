#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active, valid;
    uint16_t rotation;
    float x_g, y_g, z_g;
    uint64_t sampled_at_ms;
} curio_orientation_status_t;

typedef struct {
    bool pending;
    uint16_t candidate;
    uint64_t since_ms;
} curio_orientation_t;

void curio_orientation_reset(curio_orientation_t *orientation);
bool curio_orientation_update(curio_orientation_t *orientation, float x_g, float y_g, float z_g,
                              uint64_t now, uint16_t *rotation);
