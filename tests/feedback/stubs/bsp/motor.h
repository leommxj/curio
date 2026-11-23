#pragma once
#include <stdbool.h>
#define ESP_OK 0
int bsp_motor_set_strength(unsigned strength);
int bsp_motor_set(bool enabled);
