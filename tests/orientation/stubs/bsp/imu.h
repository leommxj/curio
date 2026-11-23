#pragma once
#include <stdint.h>
#include "esp_err.h"
#define BMI2_ACC_ODR_25HZ 6
#define BMI2_GYRO 1
#define BMI2_OK 0
struct bmi2_dev { int unused; };
typedef struct { uint8_t accel_odr; } bsp_imu_config_t;
#define BSP_IMU_CONFIG_DEFAULT() ((bsp_imu_config_t){0})
esp_err_t bsp_imu_init(void);
esp_err_t bsp_imu_start(const bsp_imu_config_t *config);
esp_err_t bsp_imu_deinit(void);
esp_err_t bsp_imu_get_accel(float *x, float *y, float *z);
struct bmi2_dev *bsp_imu_get_handle(void);
int8_t bmi2_sensor_disable(const uint8_t *sensors, uint8_t count, struct bmi2_dev *dev);
