#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t queued;
    uint32_t played;
    uint32_t failed;
    uint32_t dropped;
    bool busy;
    bool muted;
} curio_output_status_t;

typedef struct {
    curio_output_status_t audio, vibration, flash;
} curio_feedback_status_t;

bool curio_feedback_play(uint8_t volume, uint8_t vibration_strength, bool flash);
void curio_feedback_set_flash_enabled(bool enabled);
void curio_feedback_set_muted(bool audio, bool vibration);
curio_feedback_status_t curio_feedback_status(void);
