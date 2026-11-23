#pragma once
#include <stdint.h>
#define GPIO_NUM_34 34
#define GPIO_MODE_OUTPUT_OD 2
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
int gpio_set_level(int pin, unsigned level);
int gpio_config(const gpio_config_t *config);
int gpio_sleep_sel_dis(int pin);
