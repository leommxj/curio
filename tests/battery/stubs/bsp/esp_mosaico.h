#pragma once
#include "driver/i2c_master.h"
#define BSP_BATTERY_I2C_ADDR 0x55
#define BSP_BATTERY_I2C_SPEED_HZ 400000
esp_err_t bsp_i2c_init(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
