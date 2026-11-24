#include "curio_feedback.h"
#include <stdatomic.h>
#include "bsp/audio.h"
#include "bsp/motor.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define SAMPLE_RATE BSP_AUDIO_DEFAULT_SAMPLE_RATE
#define CHUNK_SAMPLES 256
#define VIBRATION_MS 240

typedef struct { uint8_t volume, vibration_strength; bool flash; } cue_t;
typedef struct {
    atomic_uint queued, played, failed, dropped;
    atomic_bool busy;
} output_t;
static QueueHandle_t cues;
static output_t audio, vibration, flash;
static bool flash_ready;
static atomic_bool flash_enabled;
static esp_codec_dev_handle_t speaker;
static atomic_bool audio_muted, vibration_muted;

void curio_feedback_set_muted(bool mute_audio, bool mute_vibration)
{
    atomic_store(&audio_muted, mute_audio);
    atomic_store(&vibration_muted, mute_vibration);
}

void curio_feedback_set_flash_enabled(bool enabled)
{
    atomic_store(&flash_enabled, enabled);
}

static cue_t allowed(cue_t cue)
{
    if (atomic_load(&audio_muted)) cue.volume = 0;
    if (atomic_load(&vibration_muted)) cue.vibration_strength = 0;
    if (!atomic_load(&flash_enabled)) cue.flash = false;
    return cue;
}

static bool tone(esp_codec_dev_handle_t speaker, uint16_t frequency, uint16_t duration_ms)
{
    int16_t samples[CHUNK_SAMPLES * BSP_AUDIO_DEFAULT_CHANNELS];
    uint32_t phase = 0;
    uint32_t step = (uint32_t)(((uint64_t)frequency << 32) / SAMPLE_RATE);
    uint32_t total = (uint32_t)SAMPLE_RATE * duration_ms / 1000;
    for (uint32_t offset = 0; offset < total; offset += CHUNK_SAMPLES) {
        uint32_t count = total - offset < CHUNK_SAMPLES ? total - offset : CHUNK_SAMPLES;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t position = offset + i;
            uint32_t ramp = position < 80 ? position : total - position < 80 ? total - position : 80;
            int32_t triangle = (phase >> 16) < 32768 ? (int32_t)(phase >> 15) - 32768 : 98303 - (int32_t)(phase >> 15);
            int16_t value = (int16_t)((int64_t)triangle * 5000 * ramp / (32768 * 80));
            for (int channel = 0; channel < BSP_AUDIO_DEFAULT_CHANNELS; channel++)
                samples[i * BSP_AUDIO_DEFAULT_CHANNELS + channel] = value;
            phase += step;
        }
        if (esp_codec_dev_write(speaker, samples, count * BSP_AUDIO_DEFAULT_CHANNELS * sizeof(samples[0])) != ESP_CODEC_DEV_OK) return false;
    }
    return true;
}

static bool flash_off(void)
{
    if (gpio_set_level(GPIO_NUM_34, 1) == ESP_OK) return true;
    return gpio_set_level(GPIO_NUM_34, 1) == ESP_OK;
}

static bool flash_pulse(void)
{
    // mosaico_module_camera/mosaico_camera.c: GPIO34 open drain, active low; high releases.
    if (!flash_ready) {
        const gpio_config_t config = {
            .pin_bit_mask = UINT64_C(1) << GPIO_NUM_34,
            .mode = GPIO_MODE_OUTPUT_OD,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (!flash_off() || gpio_config(&config) != ESP_OK ||
            gpio_sleep_sel_dis(GPIO_NUM_34) != ESP_OK) {
            flash_off();
            return false;
        }
        flash_ready = true;
    }
    bool ok = true;
    for (unsigned i = 0; i < 2 && ok && atomic_load(&flash_enabled); i++) {
        ok = gpio_set_level(GPIO_NUM_34, 0) == ESP_OK;
        if (ok) vTaskDelay(pdMS_TO_TICKS(80));
        if (!flash_off()) ok = false;
        if (i == 0 && ok) vTaskDelay(pdMS_TO_TICKS(80));
    }
    if (!flash_off()) ok = false;
    return ok;
}

static void play(cue_t cue)
{
    cue_t queued = cue;
    cue = allowed(cue);
    if (queued.volume && !cue.volume) atomic_fetch_add(&audio.dropped, 1);
    if (queued.vibration_strength && !cue.vibration_strength) atomic_fetch_add(&vibration.dropped, 1);
    if (queued.flash && !cue.flash) atomic_fetch_add(&flash.dropped, 1);
    if (!cue.volume && !cue.vibration_strength && !cue.flash) return;
    bool audio_open = false, audio_ok = false, vibration_ok = false;
    atomic_store(&audio.busy, cue.volume != 0);
    atomic_store(&vibration.busy, cue.vibration_strength != 0);
    if (cue.volume) {
        if (!speaker) speaker = bsp_audio_codec_speaker_init();
        if (speaker) {
            esp_codec_dev_sample_info_t sample = {
                .sample_rate = SAMPLE_RATE, .bits_per_sample = BSP_AUDIO_DEFAULT_BITS_PER_SAMPLE,
                .channel = BSP_AUDIO_DEFAULT_CHANNELS,
            };
            audio_open = esp_codec_dev_open(speaker, &sample) == ESP_CODEC_DEV_OK;
        }
    }
    TickType_t started = xTaskGetTickCount();
    if (cue.vibration_strength) vibration_ok = bsp_motor_set_strength(cue.vibration_strength) == ESP_OK;
    if (audio_open) {
        audio_ok = esp_codec_dev_set_out_vol(speaker, cue.volume) == ESP_CODEC_DEV_OK &&
            esp_codec_dev_set_out_mute(speaker, false) == ESP_CODEC_DEV_OK &&
            tone(speaker, 740, 90) && tone(speaker, 988, 120);
        if (audio_ok) vTaskDelay(pdMS_TO_TICKS(30));
    }
    if (cue.vibration_strength) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (vibration_ok && elapsed < pdMS_TO_TICKS(VIBRATION_MS))
            vTaskDelay(pdMS_TO_TICKS(VIBRATION_MS) - elapsed);
        if (bsp_motor_set(false) != ESP_OK) vibration_ok = false;
        atomic_fetch_add(vibration_ok ? &vibration.played : &vibration.failed, 1);
        atomic_store(&vibration.busy, false);
    }
    if (audio_open) {
        if (esp_codec_dev_set_out_mute(speaker, true) != ESP_CODEC_DEV_OK) audio_ok = false;
        if (esp_codec_dev_close(speaker) != ESP_CODEC_DEV_OK) audio_ok = false;
    }
    if (cue.volume) atomic_fetch_add(audio_ok ? &audio.played : &audio.failed, 1);
    atomic_store(&audio.busy, false);
    if (cue.flash) {
        if (atomic_load(&flash_enabled)) {
            atomic_store(&flash.busy, true);
            bool flash_ok = flash_pulse();
            atomic_fetch_add(flash_ok ? &flash.played : &flash.failed, 1);
            atomic_store(&flash.busy, false);
        } else atomic_fetch_add(&flash.dropped, 1);
    }
}

static void feedback_task(void *arg)
{
    (void)arg;
    cue_t cue;
    while (xQueueReceive(cues, &cue, portMAX_DELAY) == pdTRUE) play(cue);
}

bool curio_feedback_play(uint8_t volume, uint8_t vibration_strength, bool use_flash)
{
    if ((!volume && !vibration_strength && !use_flash) || volume > 100 || vibration_strength > 100) return false;
    cue_t cue = allowed((cue_t){volume, vibration_strength, use_flash});
    volume = cue.volume;
    vibration_strength = cue.vibration_strength;
    if (!volume && !vibration_strength && !cue.flash) return false;
    if (!cues) {
        cues = xQueueCreate(2, sizeof(cue));
        if (!cues) goto fail;
        if (xTaskCreate(feedback_task, "curio_feedback", 4096, NULL, 4, NULL) != pdPASS) {
            vQueueDelete(cues);
            cues = NULL;
            goto fail;
        }
    }
    if (xQueueSend(cues, &cue, 0) != pdTRUE) {
        if (volume) atomic_fetch_add(&audio.dropped, 1);
        if (vibration_strength) atomic_fetch_add(&vibration.dropped, 1);
        if (cue.flash) atomic_fetch_add(&flash.dropped, 1);
        return false;
    }
    if (volume) atomic_fetch_add(&audio.queued, 1);
    if (vibration_strength) atomic_fetch_add(&vibration.queued, 1);
    if (cue.flash) atomic_fetch_add(&flash.queued, 1);
    return true;
fail:
    if (volume) atomic_fetch_add(&audio.failed, 1);
    if (vibration_strength) atomic_fetch_add(&vibration.failed, 1);
    if (cue.flash) atomic_fetch_add(&flash.failed, 1);
    return false;
}

static curio_output_status_t output_status(const output_t *output)
{
    return (curio_output_status_t){
        .queued = atomic_load(&output->queued), .played = atomic_load(&output->played),
        .failed = atomic_load(&output->failed), .dropped = atomic_load(&output->dropped),
        .busy = atomic_load(&output->busy),
    };
}

curio_feedback_status_t curio_feedback_status(void)
{
    curio_feedback_status_t status = {.audio = output_status(&audio), .vibration = output_status(&vibration), .flash = output_status(&flash)};
    status.audio.muted = atomic_load(&audio_muted);
    status.vibration.muted = atomic_load(&vibration_muted);
    return status;
}
