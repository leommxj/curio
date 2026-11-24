#include "curio_buttons.h"
#include "curio_app.h"
#include "bsp/button.h"

static void open_setup(void *button, void *context)
{
    (void)button;
    (void)context;
    curio_network_setup(true);
}

esp_err_t curio_buttons_start(void)
{
    button_handle_t buttons[BSP_BUTTON_NUM] = {0};
    esp_err_t result = bsp_iot_button_create(buttons, NULL, BSP_BUTTON_NUM);
    if (result != ESP_OK) return result;
    button_event_args_t long_press = {.long_press.press_time = 3000};
    result = iot_button_register_cb(buttons[BSP_BUTTON_AI], BUTTON_LONG_PRESS_START,
                                    &long_press, open_setup, NULL);
    if (result != ESP_OK) iot_button_delete(buttons[BSP_BUTTON_AI]);
    return result;
}
