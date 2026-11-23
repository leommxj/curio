#include "lvgl.h"
typedef struct {
    int rotation;
} bsp_display_config_t;
#define BSP_DISPLAY_DEFAULT_CONFIG() ((bsp_display_config_t){0})
static inline lv_display_t *bsp_display_start_with_config(bsp_display_config_t *c)
{
    (void)c;
    return lv_display_get_default();
}
static inline int bsp_display_lock(int n)
{
    (void)n;
    return 1;
}
static inline void bsp_display_unlock(void) {}
static int displayed_brightness;
static inline void bsp_display_brightness_set(int n)
{
    displayed_brightness = n;
}
