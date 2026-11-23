#pragma once
#include "esp_err.h"
typedef enum {
    BSP_DISPLAY_ROTATE_0 = 0, BSP_DISPLAY_ROTATE_90 = 90,
    BSP_DISPLAY_ROTATE_180 = 180, BSP_DISPLAY_ROTATE_270 = 270,
} bsp_display_rotation_t;
bsp_display_rotation_t bsp_display_get_rotation(void);
esp_err_t bsp_display_set_rotation(bsp_display_rotation_t rotation);
