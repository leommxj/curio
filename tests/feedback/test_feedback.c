#include "../../firmware/main/curio_feedback.c"
#include <assert.h>
#include <string.h>

static unsigned flash_on, flash_off_calls;
static bool fail_flash_config, fail_flash_on, fail_flash_off, disable_on_delay;
static unsigned flash_level;
int gpio_set_level(int pin, unsigned level)
{
    assert(pin == GPIO_NUM_34);
    if (level) flash_off_calls++; else flash_on++;
    if ((level && fail_flash_off) || (!level && fail_flash_on)) return -1;
    flash_level = level;
    return ESP_OK;
}
int gpio_config(const gpio_config_t *config)
{
    assert(config->pin_bit_mask == (UINT64_C(1) << 34));
    assert(config->mode == GPIO_MODE_OUTPUT_OD && !config->pull_up_en && !config->pull_down_en && !config->intr_type);
    return fail_flash_config ? -1 : ESP_OK;
}
int gpio_sleep_sel_dis(int pin) { assert(pin == GPIO_NUM_34); return ESP_OK; }

static cue_t queue_items[2];
static unsigned count, queue_creates, queue_deletes, task_creates, init_calls, writes, closes, stops;
static unsigned strength, volume;
static TickType_t ticks;
static bool fail_queue, fail_task, fail_init, fail_write, fail_motor;
static void (*worker)(void *);

QueueHandle_t xQueueCreate(unsigned length, size_t item_size)
{
    assert(length == 2 && item_size == sizeof(cue_t));
    queue_creates++;
    return fail_queue ? NULL : queue_items;
}
void vQueueDelete(QueueHandle_t queue) { assert(queue == queue_items); queue_deletes++; }
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout)
{
    assert(queue == queue_items && timeout == 0);
    if (count == 2) return 0;
    queue_items[count++] = *(const cue_t *)item;
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout)
{
    assert(queue == queue_items && timeout == portMAX_DELAY);
    if (!count) return 0;
    *(cue_t *)item = queue_items[0];
    memmove(queue_items, queue_items + 1, --count * sizeof(cue_t));
    return pdTRUE;
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    assert(!strcmp(name, "curio_feedback") && stack == 4096 && !arg && priority == 4 && !handle);
    task_creates++; worker = task;
    return fail_task ? 0 : pdPASS;
}
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t delay) { ticks += delay; if (disable_on_delay) curio_feedback_set_flash_enabled(false); }
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void) { init_calls++; return fail_init ? NULL : &volume; }
int esp_codec_dev_open(esp_codec_dev_handle_t handle, const esp_codec_dev_sample_info_t *sample)
{
    assert(handle == &volume && sample->sample_rate == 16000 && sample->bits_per_sample == 16 && sample->channel == 2);
    return ESP_CODEC_DEV_OK;
}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, unsigned value) { assert(handle == &volume); volume = value; return 0; }
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t handle, bool mute) { assert(handle == &volume); (void)mute; return 0; }
int esp_codec_dev_write(esp_codec_dev_handle_t handle, void *samples, size_t size)
{
    assert(handle == &volume && samples && size && size <= 1024 && size % 4 == 0);
    writes++;
    return fail_write ? -1 : 0;
}
int esp_codec_dev_close(esp_codec_dev_handle_t handle) { assert(handle == &volume); closes++; return 0; }
int bsp_motor_set_strength(unsigned value) { strength = value; return fail_motor ? -1 : ESP_OK; }
int bsp_motor_set(bool enabled) { assert(!enabled); stops++; return ESP_OK; }

static void reset(void)
{
    flash_on = flash_off_calls = flash_level = 0;
    fail_flash_config = fail_flash_on = fail_flash_off = disable_on_delay = false;
    flash_ready = false;
    memset(&flash, 0, sizeof(flash));
    curio_feedback_set_flash_enabled(false);
    cues = NULL; speaker = NULL; worker = NULL;
    memset(&audio, 0, sizeof(audio)); memset(&vibration, 0, sizeof(vibration));
    curio_feedback_set_muted(false, false);
    count = queue_creates = queue_deletes = task_creates = init_calls = writes = closes = stops = strength = volume = ticks = 0;
    fail_queue = fail_task = fail_init = fail_write = fail_motor = false;
}
static void test_independent_outputs(void)
{
    reset();
    assert(!curio_feedback_play(0, 0, false) && !curio_feedback_play(101, 1, false) && !curio_feedback_play(1, 101, false));
    assert(!queue_creates && !task_creates && !init_calls);
    assert(curio_feedback_play(0, 73, false)); worker(NULL);
    curio_feedback_status_t status = curio_feedback_status();
    assert(!init_calls && !writes && strength == 73 && stops == 1 && ticks == 240);
    assert(status.vibration.queued == 1 && status.vibration.played == 1 && !status.vibration.busy && !status.audio.queued);
    assert(curio_feedback_play(42, 0, false)); worker(NULL);
    status = curio_feedback_status();
    assert(volume == 42 && writes && closes == 1 && stops == 1);
    assert(status.audio.played == 1 && !status.audio.busy && queue_creates == 1 && task_creates == 1);
}
static void test_failure_isolation(void)
{
    for (unsigned failure = 0; failure < 3; failure++) {
        reset();
        fail_init = failure == 0; fail_write = failure == 1; fail_motor = failure == 2;
        assert(curio_feedback_play(40, 60, false)); worker(NULL);
        curio_feedback_status_t status = curio_feedback_status();
        assert(stops == 1 && !status.audio.busy && !status.vibration.busy);
        if (fail_motor) assert(status.audio.played == 1 && writes && status.vibration.failed == 1);
        else assert(status.audio.failed == 1 && status.vibration.played == 1 && strength == 60 && ticks == 240);
        if (fail_write) assert(closes == 1);
    }
}
static void test_resource_failures(void)
{
    reset(); fail_queue = true;
    assert(!curio_feedback_play(20, 60, false));
    curio_feedback_status_t status = curio_feedback_status();
    assert(status.audio.failed == 1 && status.vibration.failed == 1 && !task_creates);
    reset(); fail_task = true;
    assert(!curio_feedback_play(0, 60, false));
    status = curio_feedback_status();
    assert(status.vibration.failed == 1 && !status.audio.failed && queue_deletes == 1 && !cues);
    fail_task = false;
    assert(curio_feedback_play(20, 60, false) && curio_feedback_play(20, 0, false));
    assert(!curio_feedback_play(20, 60, false));
    status = curio_feedback_status();
    assert(status.audio.queued == 2 && status.vibration.queued == 1 && status.audio.dropped == 1 && status.vibration.dropped == 1);
    worker(NULL);
    status = curio_feedback_status();
    assert(status.audio.played == 2 && status.vibration.played == 1);
}
static void test_quick_mute(void)
{
    reset();
    curio_feedback_set_muted(true, true);
    assert(!curio_feedback_play(35, 60, false) && !queue_creates && !task_creates);
    curio_feedback_status_t status = curio_feedback_status();
    assert(status.audio.muted && status.vibration.muted);
    curio_feedback_set_muted(true, false);
    assert(curio_feedback_play(35, 60, false)); worker(NULL);
    assert(!init_calls && strength == 60 && stops == 1);
    curio_feedback_set_muted(false, true);
    assert(curio_feedback_play(35, 60, false)); worker(NULL);
    assert(volume == 35 && closes == 1 && stops == 1);
    curio_feedback_set_muted(false, false);
    assert(curio_feedback_play(35, 60, false));
    curio_feedback_set_muted(true, true);
    worker(NULL);
    assert(closes == 1 && stops == 1);
    status = curio_feedback_status();
    assert(status.audio.queued == 2 && status.audio.dropped == 1);
    assert(status.vibration.queued == 2 && status.vibration.dropped == 1);
    curio_feedback_set_muted(false, false);
    worker(NULL);
    assert(closes == 1 && stops == 1);
    assert(curio_feedback_play(35, 60, false)); worker(NULL);
    status = curio_feedback_status();
    assert(status.audio.played == 2 && status.vibration.played == 2);
    assert(!status.audio.muted && !status.vibration.muted && !status.audio.failed && !status.vibration.failed);
    assert(status.audio.queued == status.audio.played + status.audio.dropped);
    assert(status.vibration.queued == status.vibration.played + status.vibration.dropped);
}
static void test_flash(void)
{
    reset();
    assert(!curio_feedback_play(0, 0, true) && !queue_creates);
    curio_feedback_set_flash_enabled(true);
    curio_feedback_set_muted(true, true);
    assert(curio_feedback_play(35, 60, true)); worker(NULL);
    curio_feedback_status_t status = curio_feedback_status();
    assert(flash_on == 2 && flash_level == 1 && ticks == 240);
    assert(!init_calls && !stops && status.flash.played == 1 && !status.flash.busy);
    assert(curio_feedback_play(0, 0, true));
    curio_feedback_set_flash_enabled(false); worker(NULL);
    assert(flash_on == 2);
    status = curio_feedback_status();
    assert(status.flash.queued == 2 && status.flash.played == 1 && status.flash.dropped == 1);
    curio_feedback_set_flash_enabled(true);
    disable_on_delay = true;
    assert(curio_feedback_play(0, 0, true)); worker(NULL);
    assert(flash_on == 3 && flash_level == 1 && !curio_feedback_status().flash.busy);
    assert(curio_feedback_status().flash.played == 2);
    reset(); curio_feedback_set_flash_enabled(true); disable_on_delay = true;
    assert(curio_feedback_play(40, 0, true)); worker(NULL);
    status = curio_feedback_status();
    assert(status.audio.played == 1 && flash_on == 0);
    assert(status.flash.queued == 1 && status.flash.dropped == 1 && !status.flash.played);
    for (unsigned failure = 0; failure < 3; failure++) {
        reset(); curio_feedback_set_flash_enabled(true);
        fail_flash_config = failure == 0; fail_flash_on = failure == 1; fail_flash_off = failure == 2;
        assert(curio_feedback_play(40, 60, true)); worker(NULL);
        status = curio_feedback_status();
        assert(status.audio.played == 1 && status.vibration.played == 1 && status.flash.failed == 1);
        assert(flash_off_calls >= 2 && !status.flash.busy);
        if (!fail_flash_off) assert(flash_level == 1);
    }
    reset(); curio_feedback_set_flash_enabled(true);
    assert(curio_feedback_play(0, 0, true) && curio_feedback_play(0, 0, true));
    assert(!curio_feedback_play(0, 0, true));
    assert(curio_feedback_status().flash.dropped == 1);
    worker(NULL);
    assert(curio_feedback_status().flash.played == 2);
    reset(); curio_feedback_set_flash_enabled(true); fail_queue = true;
    assert(!curio_feedback_play(0, 0, true) && curio_feedback_status().flash.failed == 1);
}
int main(void) { test_independent_outputs(); test_failure_isolation(); test_resource_failures(); test_quick_mute(); test_flash(); }
