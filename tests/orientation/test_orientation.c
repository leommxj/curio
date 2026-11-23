#include "../../firmware/main/curio_sensors.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

curio_app_t curio_app;
int mutex_depth;
static uint64_t now;
static unsigned init_count, start_count, stop_count, read_count, gyro_disable_count, rotations, battery_reads;
static bool initialized, running;
static esp_err_t init_error, start_error, read_error, stop_error, rotation_error;
static int8_t gyro_error;
static float ax, ay = 1, az;
static bsp_display_rotation_t display_rotation;
static struct bmi2_dev dev;
static unsigned iterations_left;
static jmp_buf task_done;

uint64_t curio_now_ms(void) { return now; }
esp_err_t bsp_imu_init(void)
{
    assert(!mutex_depth && !initialized);
    init_count++;
    if (!init_error) initialized = true;
    return init_error;
}
esp_err_t bsp_imu_start(const bsp_imu_config_t *config)
{
    assert(!mutex_depth && initialized && config->accel_odr == BMI2_ACC_ODR_25HZ);
    start_count++;
    if (!start_error) running = true;
    return start_error;
}
esp_err_t bsp_imu_deinit(void)
{
    assert(!mutex_depth);
    stop_count++;
    if (!stop_error) initialized = running = false;
    return stop_error;
}
esp_err_t bsp_imu_get_accel(float *x, float *y, float *z)
{
    assert(!mutex_depth && initialized && running);
    read_count++;
    if (!read_error) { *x = ax; *y = ay; *z = az; }
    return read_error;
}
struct bmi2_dev *bsp_imu_get_handle(void) { assert(initialized); return &dev; }
int8_t bmi2_sensor_disable(const uint8_t *sensors, uint8_t count, struct bmi2_dev *handle)
{
    assert(!mutex_depth && handle == &dev && count == 1 && *sensors == BMI2_GYRO);
    gyro_disable_count++;
    return gyro_error;
}
bsp_display_rotation_t bsp_display_get_rotation(void) { return display_rotation; }
esp_err_t bsp_display_set_rotation(bsp_display_rotation_t rotation)
{
    assert(!mutex_depth);
    rotations++;
    if (!rotation_error) display_rotation = rotation;
    return rotation_error;
}
void curio_battery_poll(void) { assert(!mutex_depth); battery_reads++; }
void vTaskDelay(unsigned ticks)
{
    assert(ticks == 100 && !mutex_depth);
    now += ticks;
    if (!--iterations_left) longjmp(task_done, 1);
}
int xTaskCreate(void (*fn)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    assert(fn == sensors_task && !strcmp(name, "curio_sensors") && stack == 4096 && priority == 3);
    assert(!arg && !handle);
    return pdPASS;
}

static void poll_at(uint64_t at)
{
    now = at;
    sensors_poll(now);
    assert(!mutex_depth && curio_app.orientation.rotation == (uint16_t)display_rotation);
}

static void test_classifier(void)
{
    curio_orientation_t state = {0};
    uint16_t rotation = 0;
    const struct { float x, y; uint16_t expected; } directions[] = {
        {1, 0, 90}, {0, -1, 180}, {-1, 0, 270}, {0, 1, 0},
    };
    for (unsigned i = 0; i < sizeof(directions) / sizeof(*directions); i++) {
        curio_orientation_reset(&state);
        assert(!curio_orientation_update(&state, directions[i].x, directions[i].y, 0, 0, &rotation));
        assert(!curio_orientation_update(&state, directions[i].x, directions[i].y, 0, 499, &rotation));
        assert(curio_orientation_update(&state, directions[i].x, directions[i].y, 0, 500, &rotation));
        assert(rotation == directions[i].expected);
    }
    const float rejected[][3] = {
        {0, 0, 1}, {0, 0, -1}, {0.3f, 0, 0.95f}, {0, 0, 0}, {2, 0, 0},
        {0.7f, 0.7f, 0}, {0.6f, 0.7f, 0}, {0.7f, 0.6f, 0},
        {NAN, 0, 0}, {INFINITY, 0, 0},
    };
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); i++) {
        assert(!curio_orientation_update(&state, 1, 0, 0, 1000, &rotation));
        assert(!curio_orientation_update(&state, rejected[i][0], rejected[i][1], rejected[i][2], 1400, &rotation));
        assert(!state.pending);
        assert(!curio_orientation_update(&state, 1, 0, 0, 1500, &rotation));
        assert(!curio_orientation_update(&state, 1, 0, 0, 1999, &rotation));
        curio_orientation_reset(&state);
    }
    for (unsigned i = 0; i < 30; i++) {
        assert(!curio_orientation_update(&state, i % 2 ? 1 : 0, i % 2 ? 0 : -1, 0, 2100 + i * 100, &rotation));
        assert(rotation == 0);
    }
    curio_orientation_reset(&state);
    assert(!curio_orientation_update(&state, 0.81f, 0.59f, 0, 6000, &rotation));
    assert(curio_orientation_update(&state, 0.81f, 0.59f, 0, 6500, &rotation));
    assert(rotation == 90);
    assert(!curio_orientation_update(&state, -1, 0, 0, 6600, &rotation));
    assert(!curio_orientation_update(&state, -1, 0, 0, 6000, &rotation));
    assert(!curio_orientation_update(&state, -1, 0, 0, 6499, &rotation));
    assert(curio_orientation_update(&state, -1, 0, 0, 6500, &rotation));
}

static void test_sensors(void)
{
    poll_at(0);
    assert(!init_count && !read_count && !curio_app.orientation.active && !curio_app.orientation.valid);
    curio_app.device.rotation = 180;
    poll_at(100);
    assert(display_rotation == 180 && rotations == 1 && !init_count);
    curio_app.device.auto_rotate = true;
    poll_at(200);
    assert(init_count == 1 && start_count == 1 && gyro_disable_count == 1);
    assert(curio_app.orientation.active && curio_app.orientation.valid && display_rotation == 180);
    poll_at(699);
    assert(display_rotation == 180);
    poll_at(700);
    assert(display_rotation == 0 && curio_app.device.rotation == 180);
    ax = 1; ay = 0;
    poll_at(800);
    ax = 0; az = 1;
    poll_at(1300);
    assert(display_rotation == 0);
    ax = 1; az = 0;
    poll_at(1400);
    poll_at(1900);
    assert(display_rotation == 90);
    read_error = ESP_ERR_TIMEOUT;
    poll_at(2000);
    assert(!curio_app.orientation.valid && !curio_app.orientation.active && !initialized && !running);
    unsigned reads = read_count;
    poll_at(6999);
    assert(read_count == reads && init_count == 1);
    read_error = ESP_OK;
    poll_at(7000);
    assert(init_count == 2 && curio_app.orientation.valid);
    poll_at(7500);
    curio_app.device.auto_rotate = false;
    poll_at(7600);
    assert(!initialized && !curio_app.orientation.active && !curio_app.orientation.valid && display_rotation == 180);
    reads = read_count;
    poll_at(7700);
    assert(read_count == reads);
    curio_app.device.auto_rotate = true;
    init_error = ESP_FAIL;
    poll_at(7800);
    assert(!initialized && !curio_app.orientation.active && !curio_app.orientation.valid);
    poll_at(12799);
    assert(init_count == 3);
    init_error = ESP_OK;
    start_error = ESP_FAIL;
    poll_at(12800);
    assert(!initialized && !curio_app.orientation.active);
    start_error = ESP_OK;
    gyro_error = -1;
    poll_at(17800);
    assert(!initialized && !curio_app.orientation.active);
    gyro_error = BMI2_OK;
    ax = NAN;
    poll_at(22800);
    assert(!initialized && !curio_app.orientation.valid);
    ax = -1;
    poll_at(27800);
    assert(initialized && curio_app.orientation.valid);
    rotation_error = ESP_FAIL;
    poll_at(28300);
    assert(display_rotation == 180);
    rotation_error = ESP_OK;
    poll_at(28400);
    assert(display_rotation == 270);
    curio_app.device.auto_rotate = false;
    stop_error = ESP_FAIL;
    poll_at(28500);
    assert(imu_owned && initialized && !curio_app.orientation.valid);
    stop_error = ESP_OK;
    poll_at(28600);
    assert(!imu_owned && !initialized && display_rotation == 180);
    curio_sensors_start();
    iterations_left = 101;
    if (!setjmp(task_done)) sensors_task(NULL);
    assert(battery_reads == 3);
    assert(!initialized && read_count > 0 && init_count == 7);
}

int main(void)
{
    test_classifier();
    test_sensors();
    puts("Orientation: four directions, stable delay, flat/shake/boundary rejection, IMU lifecycle, read failures and battery cadence passed");
    return 0;
}
