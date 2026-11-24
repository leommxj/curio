#include "curio_alerts.h"
uint64_t fixture_alerts_remaining_ms(const curio_alerts_t *, uint64_t);
uint32_t fixture_alerts_revision(const curio_alerts_t *);
void fixture_alerts_tick(curio_alerts_t *, const curio_core_t *, uint64_t, bool);
const curio_alert_notification_t *fixture_alerts_current(const curio_alerts_t *, uint64_t);
void fixture_alerts_dismiss(curio_alerts_t *, uint64_t);
#define curio_alerts_remaining_ms fixture_alerts_remaining_ms
#define curio_alerts_revision fixture_alerts_revision
#define curio_alerts_tick fixture_alerts_tick
#define curio_alerts_current fixture_alerts_current
#define curio_alerts_dismiss fixture_alerts_dismiss
#include "../../firmware/main/curio_ui.c"
#undef curio_alerts_remaining_ms
#undef curio_alerts_revision
#undef curio_alerts_tick
#undef curio_alerts_current
#undef curio_alerts_dismiss
#include "../../firmware/modules/widgets/agents/view.h"
#include "../../firmware/modules/widgets/metric/view.h"
#include "../../firmware/modules/widgets/status/view.h"
#include "../../firmware/modules/widgets/clock/view.h"
#include "../../firmware/modules/widgets/machine/view.h"
#include "../../firmware/modules/alerts/agent_done/config.h"
#include "../../firmware/modules/alerts/metric_high/config.h"
#include "../../firmware/modules/alerts/host_offline/config.h"
#include <assert.h>
#include <stdio.h>
static curio_widget_config_t fixture_widget(const curio_widget_type_t *type, unsigned cols, unsigned rows)
{
    curio_widget_config_t widget = {.type = type, .cols = cols, .rows = rows};
    widget.options = calloc(1, type->options_size);
    assert(widget.options);
    return widget;
}
curio_app_t curio_app;
static unsigned setup_starts, setup_stops;
void curio_network_setup(bool enabled)
{
    curio_app.setup_active = enabled;
    if (enabled) setup_starts++;
    else setup_stops++;
}
static uint64_t now;
static time_t clock_now;
time_t curio_clock_now(void) { return clock_now; }
static curio_alert_notification_t fixture_notification;
static bool fixture_visible;
static uint32_t fixture_runtime_revision;
static uint64_t fixture_deadline;
uint64_t fixture_alerts_remaining_ms(const curio_alerts_t *alerts, uint64_t time)
{
    (void)alerts;
    return fixture_visible && fixture_deadline > time ? fixture_deadline - time : 0;
}
uint32_t fixture_alerts_revision(const curio_alerts_t *alerts)
{
    (void)alerts;
    return fixture_runtime_revision;
}
static unsigned audio_calls, vibration_calls, flash_calls, last_vibration, dismiss_calls;
static bool fixture_flash_enabled;
static esp_err_t settings_result;
static unsigned brightness_saves, volume_saves, vibration_saves, flash_saves;
static unsigned config_allocations;
static bool fail_allocation;
static curio_feedback_status_t fixture_feedback;
void curio_feedback_set_muted(bool audio, bool vibration)
{
    fixture_feedback.audio.muted = audio;
    fixture_feedback.vibration.muted = vibration;
}
void curio_feedback_set_flash_enabled(bool enabled) { fixture_flash_enabled = enabled; }
curio_feedback_status_t curio_feedback_status(void) { return fixture_feedback; }
void *curio_alloc(size_t size) { config_allocations++; return fail_allocation ? NULL : malloc(size); }
esp_err_t curio_settings_set_brightness(uint8_t value)
{
    brightness_saves++;
    if (!settings_result) { curio_app.device.brightness = value; curio_app.device.revision++; }
    return settings_result;
}
esp_err_t curio_settings_set_volume(uint8_t value)
{
    volume_saves++;
    if (!settings_result) curio_app.alert_config.volume = value;
    return settings_result;
}
esp_err_t curio_settings_set_vibration_strength(uint8_t value)
{
    vibration_saves++;
    if (!settings_result) curio_app.alert_config.vibration_strength = value;
    return settings_result;
}
esp_err_t curio_settings_set_flash_enabled(bool enabled)
{
    flash_saves++;
    if (!settings_result) {
        curio_app.alert_config.flash_enabled = enabled;
        curio_app.alert_config.revision++;
    }
    return settings_result;
}
esp_err_t curio_settings_set_auto_rotate(bool enabled)
{
    if (!settings_result) curio_app.device.auto_rotate = enabled;
    return settings_result;
}
esp_err_t curio_settings_set_page_auto_page(const char *page_id, bool enabled)
{
    if (settings_result) return settings_result;
    for (size_t i = 0; i < curio_app.layout.page_count; i++) {
        curio_page_config_t *p = &curio_app.layout.pages[i];
        if (strcmp(p->id, page_id)) continue;
        bool found = false;
        for (size_t j = 0; j < p->widget_count; j++)
            if (p->widgets[j].type == &curio_widget_agents) {
                ((curio_agents_config_t *)p->widgets[j].options)->auto_page = enabled;
                found = true;
            }
        if (found) { curio_app.layout.revision++; return ESP_OK; }
    }
    return ESP_FAIL;
}
bool curio_feedback_play(uint8_t volume, uint8_t strength, bool flash)
{
    assert(volume == 0 || volume == 35);
    if (fixture_feedback.audio.muted) volume = 0;
    if (fixture_feedback.vibration.muted) strength = 0;
    if (volume) audio_calls++;
    if (strength) { vibration_calls++; last_vibration = strength; }
    if (flash && fixture_flash_enabled) flash_calls++;
    return volume || strength || (flash && fixture_flash_enabled);
}
void fixture_alerts_tick(curio_alerts_t *alerts, const curio_core_t *core, uint64_t time, bool wifi)
{
    (void)alerts; (void)core; (void)time; (void)wifi;
}
const curio_alert_notification_t *fixture_alerts_current(const curio_alerts_t *alerts, uint64_t time)
{
    (void)alerts;
    if (fixture_deadline && time >= fixture_deadline) fixture_visible = false;
    return fixture_visible ? &fixture_notification : NULL;
}
void fixture_alerts_dismiss(curio_alerts_t *alerts, uint64_t time)
{
    (void)alerts; (void)time;
    fixture_visible = false;
    dismiss_calls++;
}
uint64_t curio_now_ms(void)
{
    return now;
}
void curio_screen_start(void) {}
static lv_indev_data_t pointer;
static lv_indev_t *indev;
static unsigned clicks;
static void count_click(lv_event_t *e)
{
    (void)e;
    clicks++;
}
static void read_pointer(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    *d = pointer;
}
static void step(int x, int y, bool down)
{
    pointer.point.x = x;
    pointer.point.y = y;
    pointer.state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    now += 20;
    lv_tick_inc(20);
    lv_indev_read(indev);
    lv_obj_update_layout(lv_screen_active());
}
static void close_notification(void)
{
    assert(notification_mask);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(lv_obj_get_parent(notification_countdown), &area);
    int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
    step(x, y, true);
    step(x, y, false);
}
static void swipe(int x, int y, int dx, int dy)
{
    step(x, y, true);
    for (int i = 1; i <= 8; i++)
        step(x + dx * i / 8, y + dy * i / 8, true);
    step(x + dx, y + dy, false);
    step(x + dx, y + dy, false);
    printf("%s filter=%d\n", lv_label_get_text(((agents_payload_t *)widgets[0]->data)->footer), ((agents_payload_t *)widgets[0]->data)->filter);
}
static void check(size_t p, curio_filter_t filter)
{
    assert(((agents_payload_t *)widgets[0]->data)->page == p);
    assert(((agents_payload_t *)widgets[0]->data)->filter == filter);
    char expected[64];
    snprintf(expected, sizeof(expected), "16 agents | %zu / 4", p + 1);
    assert(!strcmp(lv_label_get_text(((agents_payload_t *)widgets[0]->data)->footer), expected));
}
static void at(uint64_t time)
{
    assert(time >= now);
    lv_tick_inc((uint32_t)(time - now));
    now = time;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
}
static void extra_checks(void)
{
    curio_widget_t *w = widgets[0];
    agents_payload_t *a = ((agents_payload_t *)w->data);
    ((agents_payload_t *)w->data)->page = 0;
    ((curio_agents_config_t *)w->config.options)->auto_page = true;
    ((agents_payload_t *)w->data)->filter = CURIO_FILTER_ALL;
    a->next_page_ms = now + 10000;
    refresh(NULL);
    uint64_t due = a->next_page_ms;
    at(due - 1);
    check(0, CURIO_FILTER_ALL);
    at(due);
    check(1, CURIO_FILTER_ALL);
    ((agents_payload_t *)w->data)->page = 3;
    a->next_page_ms = now + 10000;
    at(a->next_page_ms);
    check(0, CURIO_FILTER_ALL);
    ((curio_agents_config_t *)w->config.options)->auto_page = false;
    at(now + 20000);
    check(0, CURIO_FILTER_ALL);
    ((curio_agents_config_t *)w->config.options)->auto_page = true;
    ((curio_agents_config_t *)w->config.options)->host_count = 0;
    ((curio_agents_config_t *)w->config.options)->all_hosts = false;
    refresh(NULL);
    assert(((agents_payload_t *)w->data)->pages == 1);
    at(now + 20000);
    assert(((agents_payload_t *)w->data)->page == 0);
    ((curio_agents_config_t *)w->config.options)->all_hosts = true;
    refresh(NULL);
    swipe(400, 250, -300, 0);
    check(1, CURIO_FILTER_ALL);
    due = a->next_page_ms;
    at(due - 1);
    check(1, CURIO_FILTER_ALL);
    at(due);
    check(2, CURIO_FILTER_ALL);
    step(400, 155, true);
    step(400, 155, false);
    assert(((agents_payload_t *)w->data)->filter == CURIO_FILTER_WORKING && ((agents_payload_t *)w->data)->page == 0);
    due = a->next_page_ms;
    at(due - 1);
    assert(((agents_payload_t *)w->data)->page == 0);
    at(due);
    assert(((agents_payload_t *)w->data)->page == 1);
    step(250, 250, true);
    at(now + 30000);
    assert(((agents_payload_t *)w->data)->page == 1);
    due = a->next_page_ms;
    step(250, 250, false);
    at(due - 1);
    assert(((agents_payload_t *)w->data)->page == 1);
    at(due);
    assert(((agents_payload_t *)w->data)->page == 2);
    step(80, 155, true);
    step(80, 155, false);
    ((curio_agents_config_t *)w->config.options)->auto_page = false;
    curio_host_t *h = &curio_app.core.hosts[0];
    curio_agent_t *v = h->sources[0].agents;
    h->last_report_ms = now;
    h->sources[0].status = CURIO_SOURCE_OK;
    h->sources[0].received_ms = now;
    strcpy(v[0].name, "Codex");
    strcpy(v[0].brand, "Codex");
    strcpy(v[0].project, "project-first");
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(a->rows[0].name), "project-first"));
    assert(strstr(lv_label_get_text(a->rows[0].detail), "Codex | H"));
    strcpy(v[0].name, "Specific title");
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(a->rows[0].name), "Specific title"));
    assert(strstr(lv_label_get_text(a->rows[0].detail), "Codex | H | project-first"));
    v[0].brand[0] = 0;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(a->rows[0].name), "project-first"));
    assert(strstr(lv_label_get_text(a->rows[0].detail), "Specific title | H"));
    v[0].project[0] = 0;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(a->rows[0].name), "Specific title"));
    assert(!strcmp(lv_label_get_text(a->rows[0].state), "working"));
    assert(a->rows[0].name != a->rows[0].state);
    assert(lv_obj_get_width(a->rows[0].state) == 120);
    assert(lv_obj_get_x(a->rows[0].state) >= lv_obj_get_x(a->rows[0].name) + lv_obj_get_width(a->rows[0].name));
    h->sources[0].status = CURIO_SOURCE_ERROR;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(!strcmp(lv_label_get_text(a->rows[0].state), "Collection error"));
    assert(lv_obj_get_width(a->rows[0].state) == 170);
    puts("Auto-page boundaries/reset/pause and brand/title/state labels passed");
}
static void battery_checks(void)
{
    curio_app.battery = (curio_battery_t){.valid = true, .present = true,
        .level_pct = 56, .current_ma = 18, .sampled_at_ms = now};
    refresh(NULL);
    assert(strstr(lv_label_get_text(battery_label), "56%"));
    assert(strstr(lv_label_get_text(battery_label), LV_SYMBOL_CHARGE));
    curio_app.battery.current_ma = -220;
    curio_app.battery.level_pct = 12;
    refresh(NULL);
    assert(strstr(lv_label_get_text(battery_label), "12%"));
    assert(!strstr(lv_label_get_text(battery_label), LV_SYMBOL_CHARGE));
    curio_app.battery.fully_charged = true;
    curio_app.battery.current_ma = 0;
    curio_app.battery.level_pct = 100;
    refresh(NULL);
    assert(strstr(lv_label_get_text(battery_label), "100%"));
    assert(!strstr(lv_label_get_text(battery_label), LV_SYMBOL_CHARGE));
    lv_obj_update_layout(lv_screen_active());
    lv_point_t size;
    lv_text_get_size(&size, LV_SYMBOL_CHARGE " " LV_SYMBOL_BATTERY_FULL " 100%",
                     &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    assert(size.x <= lv_obj_get_width(battery_label));
    assert(lv_obj_get_x(vibration_button) + lv_obj_get_width(vibration_button) <= lv_obj_get_x(battery_label));
    at(now + 15001);
    assert(strstr(lv_label_get_text(battery_label), "--"));
    curio_app.battery = (curio_battery_t){.valid = true, .sampled_at_ms = now};
    refresh(NULL);
    assert(strstr(lv_label_get_text(battery_label), "--"));
    puts("Battery header: charging, discharging, full, stale, absent and width passed");
}

static void notification_checks(void)
{
    curio_widget_t *w = widgets[0];
    ((curio_agents_config_t *)w->config.options)->auto_page = true;
    ((agents_payload_t *)w->data)->filter = CURIO_FILTER_ALL;
    ((agents_payload_t *)w->data)->page = 0;
    ((agents_payload_t *)w->data)->next_page_ms = now + 100;
    curio_app.alerts = (curio_alerts_t *)1;
    curio_app.alert_config.revision = 1;
    fixture_runtime_revision = 1;
    fixture_notification = (curio_alert_notification_t){.id = 1, .sound = true, .volume = 35};
    strcpy(fixture_notification.title, "Agent finished");
    strcpy(fixture_notification.body, "A0");
    strcpy(fixture_notification.detail, "H | a0");
    fixture_visible = true;
    unsigned opening_clicks = clicks;
    step(80, 155, true);
    refresh(NULL);
    step(80, 155, false);
    assert(clicks == opening_clicks);
    assert(notification_mask && notification_id == 1);
    assert(lv_obj_has_flag(notification_countdown, LV_OBJ_FLAG_HIDDEN));
    fixture_deadline = now + 10000;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(notification_countdown), "10s"));
    at(now + 1000);
    assert(!strcmp(lv_label_get_text(notification_countdown), "9s"));
    curio_app.alert_config.revision++;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(notification_countdown), "9s"));
    fixture_deadline = 0;
    refresh(NULL);
    assert(lv_obj_has_flag(notification_countdown, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_child(lv_screen_active(), -1) == notification_mask);
    assert(!strcmp(lv_label_get_text(notification_title), "Agent finished"));
    assert(audio_calls == 1);
    curio_app.layout.revision++;
    refresh(NULL);
    w = widgets[0];
    ((curio_agents_config_t *)w->config.options)->auto_page = true;
    ((agents_payload_t *)w->data)->next_page_ms = now + 100;
    assert(lv_obj_get_child(lv_screen_active(), -1) == notification_mask);
    assert(audio_calls == 1);
    unsigned prior_clicks = clicks;
    step(400, 155, true);
    step(400, 155, false);
    assert(clicks == prior_clicks && ((agents_payload_t *)w->data)->filter == CURIO_FILTER_ALL);
    swipe(400, 250, -300, 0);
    assert(((agents_payload_t *)w->data)->page == 0);
    at(now + 20000);
    assert(((agents_payload_t *)w->data)->page == 0 && audio_calls == 1);
    curio_app.alert_config.revision++;
    curio_app.alert_config.volume = 61;
    refresh(NULL);
    assert(audio_calls == 1 && notification_mask);
    close_notification();
    assert(!notification_mask && !fixture_visible);
    at(((agents_payload_t *)w->data)->next_page_ms);
    assert(((agents_payload_t *)w->data)->page == 1);
    fixture_visible = true;
    curio_app.alert_config.revision++;
    fixture_runtime_revision++;
    refresh(NULL);
    assert(notification_mask && audio_calls == 2);
    fixture_visible = false;
    refresh(NULL);
    assert(!notification_mask);
    unsigned sounds = audio_calls, vibrations = vibration_calls;
    fixture_notification.sound = false;
    fixture_notification.vibrate = true;
    fixture_notification.vibration_strength = 60;
    fixture_notification.id++;
    fixture_visible = true;
    refresh(NULL);
    assert(audio_calls == sounds && vibration_calls == vibrations + 1 && last_vibration == 60);
    fixture_notification.vibration_strength = 90;
    refresh(NULL);
    assert(vibration_calls == vibrations + 1);
    fixture_notification.id++;
    refresh(NULL);
    assert(audio_calls == sounds && vibration_calls == vibrations + 2 && last_vibration == 90);
    fixture_notification.vibration_strength = 0;
    fixture_notification.id++;
    refresh(NULL);
    assert(audio_calls == sounds && vibration_calls == vibrations + 2);
    fixture_notification.sound = true;
    fixture_notification.id++;
    refresh(NULL);
    assert(audio_calls == sounds + 1 && vibration_calls == vibrations + 2);
    fixture_notification.vibration_strength = 60;
    fixture_notification.id++;
    refresh(NULL);
    assert(audio_calls == sounds + 2 && vibration_calls == vibrations + 3);
    fixture_visible = false;
    refresh(NULL);
    puts("Notification overlay, input capture, page pause and independent feedback passed");
}
static void notification_layout_checks(void)
{
    curio_alert_notification_t saved = fixture_notification;
    fixture_notification = (curio_alert_notification_t){.id = 100, .title = "Agent finished",
        .body = "curio", .detail = "codex | Workstation"};
    fixture_visible = true;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_t *panel = lv_obj_get_child(notification_mask, 0);
    lv_obj_t *content = lv_obj_get_parent(notification_body);
    lv_obj_t *close = lv_obj_get_parent(notification_countdown);
    int32_t short_height = lv_obj_get_height(panel);
    assert(short_height < 350 && lv_obj_get_scroll_bottom(content) == 0);
    fixture_notification.detail[0] = 0;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(lv_obj_get_height(panel) < short_height);

    strcpy(fixture_notification.body, "Investigate build and deployment failures in the workstation monitoring service");
    strcpy(fixture_notification.detail, "codex | Workstation | /home/user/projects/curio/firmware/components/curio_alerts");
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t card, button_area, viewport;
    lv_obj_get_coords(panel, &card);
    lv_obj_get_coords(close, &button_area);
    lv_obj_get_coords(content, &viewport);
    assert(lv_obj_get_height(panel) > short_height);
    assert(card.x1 >= 16 && card.y1 >= 16 && card.x2 < 464 && card.y2 < 464);
    assert(button_area.y1 > viewport.y2 && button_area.y2 < card.y2);
    assert(lv_obj_get_scroll_bottom(content) > 0);
    size_t page = ((agents_payload_t *)widgets[0]->data)->page;
    swipe(240, viewport.y2 - 20, 0, -180);
    assert(lv_obj_get_scroll_y(content) > 0 && ((agents_payload_t *)widgets[0]->data)->page == page);
    lv_obj_scroll_to_y(content, 100, LV_ANIM_OFF);
    refresh(NULL);
    assert(lv_obj_get_scroll_y(content) == 100);
    fixture_notification.id++;
    refresh(NULL);
    assert(lv_obj_get_scroll_y(content) == 0);
    close_notification();
    assert(!notification_mask);
    fixture_notification = saved;
    puts("Notification layout: content height, long text scrolling and accessible close passed");
}
static void tap(int x, int y)
{
    step(x, y, true);
    step(x, y, false);
}
static void setup_checks(void)
{
    strcpy(curio_app.setup_ssid, "Curio-123456");
    strcpy(curio_app.setup_password, "abcdef1234567890");
    curio_app.setup_active = true;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(setup_mask && setup_shown && setup_starts == 1);
    assert(!strcmp(lv_label_get_text(setup_details), "Network\nCurio-123456\n\nPassword\nabcdef1234567890"));
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(setup_mask, -1)), "http://192.168.4.1"));
    tap(410, 38);
    assert(!setup_mask && setup_stops == 1);
    curio_app.setup_active = true;
    refresh(NULL);
    assert(!setup_mask && setup_starts == 1);
    tap(444, 28);
    assert(settings_mask);
    tap(308, 38);
    assert(setup_mask && setup_starts == 2);
    curio_app.setup_active = false;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(setup_details), "Starting..."));
    strcpy(curio_app.network_error, "Setup: ESP_FAIL");
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(setup_details), "Setup: ESP_FAIL"));
    curio_app.network_error[0] = 0;
    curio_app.setup_active = true;
    refresh(NULL);
    assert(strstr(lv_label_get_text(setup_details), "Curio-123456"));
    tap(410, 38);
    assert(!setup_mask && setup_stops == 2);
    tap(410, 38);
    assert(!settings_mask);
    puts("Wi-Fi setup: initial prompt, credentials, manual reopen and close passed");
}
static void settings_checks(void)
{
    tap(444, 28);
    assert(settings_mask);
    assert(!strcmp(lv_label_get_text(settings_error), ""));
    assert(lv_obj_get_child(lv_screen_active(), -1) == settings_mask);
    tap(418, 90);
    assert(curio_app.device.auto_rotate);
    settings_result = ESP_FAIL;
    tap(418, 90);
    assert(curio_app.device.auto_rotate && lv_obj_has_state(rotate_switch, LV_STATE_CHECKED));
    assert(!strcmp(lv_label_get_text(settings_error), "ESP_FAIL"));
    settings_result = ESP_OK;
    unsigned sounds_before_sliders = audio_calls;
    step(339, 266, true);
    step(240, 266, true);
    assert(brightness_saves == 0 && displayed_brightness == lv_slider_get_value(brightness_slider));
    refresh(NULL);
    assert(dragging_slider == brightness_slider);
    int dragged_brightness = lv_slider_get_value(brightness_slider);
    curio_app.device.brightness = 22;
    curio_app.device.revision++;
    refresh(NULL);
    assert(lv_slider_get_value(brightness_slider) == dragged_brightness);
    assert(displayed_brightness == dragged_brightness);
    step(240, 266, false);
    assert(brightness_saves == 1 && curio_app.device.brightness == lv_slider_get_value(brightness_slider));
    unsigned saved_brightness = curio_app.device.brightness;
    settings_result = ESP_FAIL;
    step(240, 266, true);
    step(420, 266, true);
    assert(brightness_saves == 1);
    step(420, 266, false);
    assert(brightness_saves == 2 && curio_app.device.brightness == saved_brightness);
    assert(displayed_brightness == saved_brightness);
    assert(!strcmp(lv_label_get_text(settings_error), "ESP_FAIL"));
    settings_result = ESP_OK;
    tap(36, 322);
    assert(volume_saves == 1 && curio_app.alert_config.volume == 0);
    assert(!strcmp(lv_label_get_text(volume_value), "0% (Mute)"));
    settings_result = ESP_FAIL;
    step(36, 322, true);
    step(300, 322, true);
    assert(volume_saves == 1);
    step(300, 322, false);
    assert(volume_saves == 2 && curio_app.alert_config.volume == 0);
    assert(lv_slider_get_value(volume_slider) == 0);
    settings_result = ESP_OK;
    curio_app.device.brightness = 0;
    curio_app.device.revision++;
    curio_app.alert_config.volume = 64;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(brightness_value), "0%"));
    assert(lv_slider_get_value(brightness_slider) == 5);
    assert(lv_slider_get_value(volume_slider) == 64);
    assert(brightness_saves == 2 && volume_saves == 2 && audio_calls == sounds_before_sliders);
    unsigned vibrations_before_sliders = vibration_calls;
    tap(36, 378);
    assert(vibration_saves == 1 && curio_app.alert_config.vibration_strength == 0);
    assert(!strcmp(lv_label_get_text(vibration_value), "0% (Off)"));
    settings_result = ESP_FAIL;
    step(36, 378, true);
    step(300, 378, true);
    assert(vibration_saves == 1);
    refresh(NULL);
    assert(dragging_slider == vibration_slider && lv_slider_get_value(vibration_slider) > 0);
    step(300, 378, false);
    assert(vibration_saves == 2 && curio_app.alert_config.vibration_strength == 0);
    assert(lv_slider_get_value(vibration_slider) == 0);
    settings_result = ESP_OK;
    tap(440, 378);
    assert(vibration_saves == 3 && curio_app.alert_config.vibration_strength == 100);
    curio_app.alert_config.vibration_strength = 60;
    refresh(NULL);
    assert(lv_slider_get_value(vibration_slider) == 60 && vibration_saves == 3);
    assert(vibration_calls == vibrations_before_sliders);
    bool initial_paging = ((curio_agents_config_t *)curio_app.layout.pages[0].widgets[0].options)->auto_page;
    tap(418, 138);
    assert(((curio_agents_config_t *)curio_app.layout.pages[0].widgets[0].options)->auto_page != initial_paging);
    curio_app.device.auto_rotate = false;
    ((curio_agents_config_t *)curio_app.layout.pages[0].widgets[0].options)->auto_page = true;
    curio_app.layout.revision++;
    refresh(NULL);
    assert(!lv_obj_has_state(rotate_switch, LV_STATE_CHECKED));
    assert(lv_obj_has_state(paging_switch, LV_STATE_CHECKED));
    step(418, 138, true);
    char original_id[33];
    snprintf(original_id, sizeof(original_id), "%s", curio_app.layout.pages[0].id);
    strcpy(curio_app.layout.pages[0].id, "replacement");
    curio_app.layout.revision++;
    refresh(NULL);
    step(418, 138, false);
    assert(((curio_agents_config_t *)curio_app.layout.pages[0].widgets[0].options)->auto_page);
    assert(!strcmp(lv_label_get_text(settings_error), "ESP_FAIL"));
    strcpy(curio_app.layout.pages[0].id, original_id);
    curio_app.layout.revision++;
    refresh(NULL);
    size_t page = ((agents_payload_t *)widgets[0]->data)->page;
    swipe(400, 250, -300, 0);
    at(now + 20000);
    assert(((agents_payload_t *)widgets[0]->data)->page == page);
    curio_app.alert_config.volume = 35;
    unsigned sounds = audio_calls, vibrations = vibration_calls, dismissed = dismiss_calls;
    tap(350, 430);
    assert(preview_active && notification_mask && audio_calls == sounds + 1);
    assert(vibration_calls == vibrations + 1 && last_vibration == 60);
    refresh(NULL);
    assert(audio_calls == sounds + 1 && vibration_calls == vibrations + 1);
    uint64_t started = now;
    assert(!strcmp(lv_label_get_text(notification_countdown), "10s"));
    at(started + 1000);
    assert(!strcmp(lv_label_get_text(notification_countdown), "9s"));
    at(started + 9000);
    assert(!strcmp(lv_label_get_text(notification_countdown), "1s"));
    at(started + 9999);
    assert(!strcmp(lv_label_get_text(notification_countdown), "1s"));
    assert(preview_active && notification_mask);
    at(started + 10000);
    assert(!preview_active && !notification_mask && dismiss_calls == dismissed);
    tap(350, 430);
    close_notification();
    assert(!preview_active && !notification_mask && dismiss_calls == dismissed);
    tap(350, 430);
    sounds = audio_calls;
    fixture_notification.id++;
    fixture_visible = true;
    refresh(NULL);
    assert(!preview_active && notification_id == fixture_notification.id);
    assert(audio_calls == sounds + 1);
    refresh(NULL);
    assert(audio_calls == sounds + 1);
    close_notification();
    assert(!fixture_visible && dismiss_calls == dismissed + 1);
    assert(settings_mask);
    tap(410, 38);
    assert(!settings_mask);
    curio_widget_config_t original_widget = curio_app.layout.pages[0].widgets[0];
    curio_app.layout.pages[0].widgets[0] = fixture_widget(&curio_widget_metric, 4, 5);
    curio_app.layout.revision++;
    refresh(NULL);
    tap(444, 28);
    assert(settings_mask && lv_obj_has_state(paging_switch, LV_STATE_DISABLED));
    tap(410, 38);
    curio_widget_config_free(&curio_app.layout.pages[0].widgets[0]);
    curio_app.layout.pages[0].widgets[0] = original_widget;
    curio_app.layout.revision++;
    refresh(NULL);
    tap(444, 28);
    unsigned saves_before_interruption = brightness_saves;
    unsigned saved_before_interruption = curio_app.device.brightness;
    step(240, 266, true);
    step(400, 266, true);
    assert(dragging_slider == brightness_slider);
    fixture_notification.id++;
    fixture_visible = true;
    refresh(NULL);
    assert(!dragging_slider && displayed_brightness == saved_before_interruption);
    step(400, 266, false);
    assert(brightness_saves == saves_before_interruption);
    close_notification();
    assert(!notification_mask && settings_mask && !dragging_slider);
    tap(410, 38);
        unsigned children = lv_obj_get_child_count(lv_screen_active());
    for (unsigned i = 0; i < 5; i++) {
        tap(444, 28);
        assert(lv_obj_get_child_count(lv_screen_active()) == children + 1);
        tap(410, 38);
        assert(lv_obj_get_child_count(lv_screen_active()) == children);
    }
    fixture_visible = true;
    fixture_deadline = now + 10000;
    fixture_notification.id++;
    refresh(NULL);
    uint64_t real_started = now;
    assert(!strcmp(lv_label_get_text(notification_countdown), "10s"));
    at(real_started + 1000);
    assert(!strcmp(lv_label_get_text(notification_countdown), "9s"));
    at(real_started + 9999);
    assert(!strcmp(lv_label_get_text(notification_countdown), "1s"));
    at(real_started + 10000);
    assert(!notification_mask);
    fixture_deadline = 0;
    puts("Settings input, persistence failures, sync, paging pause and preview lifecycle passed");
}
static lv_obj_t *find_label(lv_obj_t *parent, const char *value)
{
    if (lv_obj_check_type(parent, &lv_label_class) && strstr(lv_label_get_text(parent), value)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); i++) {
        lv_obj_t *found = find_label(lv_obj_get_child(parent, i), value);
        if (found) return found;
    }
    return NULL;
}
static void alert_details_checks(void)
{
    curio_alert_config_t saved = curio_app.alert_config;
    unsigned sounds = audio_calls, vibrations = vibration_calls, allocations = config_allocations;
    unsigned saves = brightness_saves + volume_saves + vibration_saves + flash_saves;
    tap(444, 28);
    assert(!alert_details_mask && config_allocations == allocations);
    unsigned settings_children = lv_obj_get_child_count(settings_mask);
    tap(120, 430);
    assert(alert_details_mask && find_label(alert_details_list, "No rules"));
    assert(config_allocations == allocations + 1);
    refresh(NULL);
    assert(config_allocations == allocations + 1);
    tap(410, 38);
    assert(!alert_details_mask && settings_mask && lv_obj_get_child_count(settings_mask) == settings_children);

    curio_app.alert_config = (curio_alert_config_t){.revision = saved.revision + 1,
        .volume = 0, .vibration_strength = 60, .rule_count = 3, .rules = {
            {.id = "agent-complete", .type = &curio_alert_agent_done, .enabled = true, .options = &(agent_done_options_t){.host_id = "*", .source_id = "herdr", .include_idle = true}, .display_s = 10, .sound = true, .vibrate = true, .flash = true},
            {.id = "memory-high", .type = &curio_alert_metric_high, .options = &(metric_high_options_t){.host_id = "workstation", .source_id = "system.memory", .key = "usage_pct", .unit = "percent", .hold_s = 60, .threshold = 92.5, .recover = 80}, .display_s = 0},
            {.id = "offline", .type = &curio_alert_host_offline, .enabled = true, .options = &(host_offline_options_t){.host_id = "probe", .hold_s = 30}, .display_s = 10},
        }};
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(settings_alerts), "2 / 3 enabled"));
    tap(120, 430);
    assert(alert_details_mask && lv_obj_get_child_count(alert_details_list) == 3);
    assert(find_label(alert_details_list, "Host: All hosts\nSource: herdr\nState: Working -> Done / Idle"));
    assert(find_label(alert_details_list, "Display: 10s\nSound: On (0%)\nVibration: On (60%)"));
    assert(find_label(alert_details_list, "Flash: Off (global)"));
    assert(find_label(alert_details_list, "Above: 92.5% for 60s\nRecover: <= 80%"));
    assert(find_label(alert_details_list, "Display: Until closed\nSound: Off\nVibration: Off"));
    assert(find_label(alert_details_list, "Disabled") && find_label(alert_details_list, "Offline for: 30s"));
    size_t agent_page = ((agents_payload_t *)widgets[0]->data)->page;
    swipe(230, 410, 0, -260);
    assert(lv_obj_get_scroll_y(alert_details_list) > 0);
    for (unsigned i = 0; i < 40; i++) step(230, 150, false);
    lv_obj_scroll_to_y(alert_details_list, 100, LV_ANIM_OFF);
    int32_t scroll = lv_obj_get_scroll_y(alert_details_list);
    allocations = config_allocations;
    at(now + 20000);
    assert(((agents_payload_t *)widgets[0]->data)->page == agent_page && config_allocations == allocations);
    assert(lv_obj_get_scroll_y(alert_details_list) == scroll);

    curio_app.alert_config.flash_enabled = true;
    curio_app.alert_config.vibration_strength = 22;
    ((agent_done_options_t *)curio_app.alert_config.rules[0].options)->include_idle = false;
    curio_app.alert_config.revision++;
    refresh(NULL);
    assert(find_label(alert_details_list, "Vibration: On (22%)"));
    assert(find_label(alert_details_list, "Flash: On"));
    assert(find_label(alert_details_list, "State: Working -> Done\n"));
    assert(!find_label(alert_details_list, "Done / Idle"));
    assert(lv_obj_get_scroll_y(alert_details_list) == scroll);
    assert(saves == brightness_saves + volume_saves + vibration_saves + flash_saves && sounds == audio_calls && vibrations == vibration_calls);

    fixture_notification.id++;
    fixture_visible = true;
    fixture_deadline = now + 10000;
    refresh(NULL);
    assert(lv_obj_get_child(lv_screen_active(), -1) == notification_mask);
    lv_obj_update_layout(lv_screen_active());
    close_notification();
    assert(!notification_mask && alert_details_mask && lv_obj_get_scroll_y(alert_details_list) == scroll);

    curio_app.alert_config.rule_count = 8;
    for (size_t i = 3; i < 8; i++) {
        curio_app.alert_config.rules[i] = curio_app.alert_config.rules[1];
        curio_app.alert_config.rules[i].options = malloc(sizeof(metric_high_options_t));
        assert(curio_app.alert_config.rules[i].options);
        memcpy(curio_app.alert_config.rules[i].options, curio_app.alert_config.rules[1].options, sizeof(metric_high_options_t));
        snprintf(curio_app.alert_config.rules[i].id, 49, "rule-%zu", i);
        memset(((metric_high_options_t *)curio_app.alert_config.rules[i].options)->host_id, 'W', 48);
        ((metric_high_options_t *)curio_app.alert_config.rules[i].options)->host_id[48] = 0;
    }
    curio_app.alert_config.revision++;
    refresh(NULL);
    assert(lv_obj_get_child_count(alert_details_list) == 8);
    lv_obj_t *last = lv_obj_get_child(alert_details_list, -1);
    lv_obj_scroll_to_view(last, LV_ANIM_OFF);
    assert(find_label(last, ((metric_high_options_t *)curio_app.alert_config.rules[7].options)->host_id));
    assert(lv_obj_get_scroll_bottom(alert_details_list) == 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(last); i++) {
        lv_area_t card, child;
        lv_obj_get_coords(last, &card);
        lv_obj_get_coords(lv_obj_get_child(last, i), &child);
        assert(child.y2 < card.y2 && child.x2 < card.x2);
    }
    for (size_t i = 3; i < 8; i++) free(curio_app.alert_config.rules[i].options);
    curio_app.alert_config.rule_count = 0;
    curio_app.alert_config.revision++;
    refresh(NULL);
    assert(find_label(alert_details_list, "No rules") && lv_obj_get_scroll_y(alert_details_list) == 0);
    for (unsigned i = 0; i < 4; i++) {
        tap(410, 38);
        assert(!alert_details_mask && lv_obj_get_child_count(settings_mask) == settings_children);
        tap(120, 430);
        assert(alert_details_mask);
    }
    settings_close(NULL);
    assert(!settings_mask && !alert_details_mask);
    curio_app.alert_config = saved;
    fixture_deadline = 0;
    puts("Alert details: conditions, scroll, live updates, notification priority and cleanup passed");
}
static void header_checks(void)
{
    curio_alert_config_t saved = curio_app.alert_config;
    unsigned saves = brightness_saves + volume_saves + vibration_saves + flash_saves;
    unsigned sounds = audio_calls, vibrations = vibration_calls;
    assert(!fixture_feedback.audio.muted && !fixture_feedback.vibration.muted);
    tap(234, 30);
    assert(fixture_feedback.audio.muted && !fixture_feedback.vibration.muted);
    assert(lv_obj_has_state(sound_button, LV_STATE_CHECKED));
    tap(284, 30);
    assert(fixture_feedback.audio.muted && fixture_feedback.vibration.muted);
    assert(lv_obj_has_state(vibration_button, LV_STATE_CHECKED));
    assert(!memcmp(&saved, &curio_app.alert_config, sizeof(saved)));
    assert(saves == brightness_saves + volume_saves + vibration_saves + flash_saves);
    fixture_notification = (curio_alert_notification_t){.id = 1000, .sound = true, .vibrate = true, .volume = 35, .vibration_strength = 60};
    fixture_visible = true;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(notification_mask && audio_calls == sounds && vibration_calls == vibrations);
    close_notification();
    tap(234, 30);
    assert(!fixture_feedback.audio.muted && fixture_feedback.vibration.muted);
    tap(444, 28);
    curio_app.alert_config.volume = 35;
    curio_app.alert_config.vibration_strength = 60;
    tap(350, 430);
    assert(preview_active && audio_calls == sounds + 1 && vibration_calls == vibrations);
    close_notification();
    tap(410, 38);
    tap(284, 30);
    tap(234, 30);
    fixture_notification.id++;
    fixture_visible = true;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(audio_calls == sounds + 1 && vibration_calls == vibrations + 1);
    close_notification();
    tap(234, 30);
    assert(!fixture_feedback.audio.muted && !fixture_feedback.vibration.muted);

    curio_app.configured = curio_app.wifi_connected = curio_app.https_ready = true;
    strcpy(curio_app.ip, "192.168.1.123");
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(wifi_button, 0)), LV_SYMBOL_WIFI));
    assert(!lv_obj_has_state(wifi_button, LV_STATE_CHECKED) && lv_obj_has_flag(wifi_warning, LV_OBJ_FLAG_HIDDEN));
    tap(184, 30);
    assert(network_popup && !strcmp(lv_label_get_text(network_status), "Connected"));
    assert(!strcmp(lv_label_get_text(network_ip), "192.168.1.123"));
    curio_app.wifi_connected = false;
    refresh(NULL);
    assert(lv_obj_has_state(wifi_button, LV_STATE_CHECKED));
    assert(!strcmp(lv_label_get_text(network_status), "Wi-Fi disconnected"));
    assert(!strcmp(lv_label_get_text(network_ip), "No IP address"));
    tap(184, 30);
    assert(!network_popup);
    curio_app.wifi_connected = true;
    curio_app.https_ready = false;
    refresh(NULL);
    assert(!lv_obj_has_flag(wifi_warning, LV_OBJ_FLAG_HIDDEN));
    tap(184, 30);
    assert(!strcmp(lv_label_get_text(network_status), "HTTPS unavailable"));
    strcpy(curio_app.network_error, "Settings: ESP_FAIL");
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(network_status), "Settings: ESP_FAIL"));
    at(now + 5000);
    assert(!network_popup);
    curio_app.configured = false;
    tap(184, 30);
    assert(!strcmp(lv_label_get_text(network_status), "Setup required"));
    tap(444, 28);
    assert(!network_popup && settings_mask);
    tap(410, 38);
    curio_app.configured = curio_app.wifi_connected = curio_app.https_ready = true;
    curio_app.network_error[0] = 0;
    curio_app.alert_config = saved;
    puts("Header: independent temporary mute, unchanged config, preview and Wi-Fi status passed");
}
static void flash_checks(void)
{
    curio_alert_config_t saved = curio_app.alert_config;
    curio_app.alert_config.flash_enabled = false;
    curio_app.alert_config.volume = 0;
    curio_app.alert_config.vibration_strength = 0;
    refresh(NULL);
    tap(444, 28);
    unsigned flashes = flash_calls, sounds = audio_calls, vibrations = vibration_calls;
    tap(350, 430);
    assert(preview_active && flash_calls == flashes);
    close_notification();
    tap(418, 202);
    assert(curio_app.alert_config.flash_enabled && lv_obj_has_state(flash_switch, LV_STATE_CHECKED));
    settings_result = ESP_FAIL;
    tap(418, 202);
    assert(curio_app.alert_config.flash_enabled && lv_obj_has_state(flash_switch, LV_STATE_CHECKED));
    assert(!strcmp(lv_label_get_text(settings_error), "ESP_FAIL"));
    settings_result = ESP_OK;
    tap(350, 430);
    assert(preview_active && flash_calls == flashes + 1);
    refresh(NULL);
    assert(flash_calls == flashes + 1 && audio_calls == sounds && vibration_calls == vibrations);
    close_notification();
    tap(410, 38);
    curio_feedback_set_muted(true, true);
    fixture_notification = (curio_alert_notification_t){.id = 2000, .sound = true, .vibrate = true,
        .volume = 35, .vibration_strength = 60, .flash = true};
    fixture_visible = true;
    refresh(NULL);
    assert(notification_mask && flash_calls == flashes + 2 && audio_calls == sounds && vibration_calls == vibrations);
    refresh(NULL);
    assert(flash_calls == flashes + 2);
    fixture_notification.id++;
    fixture_notification.flash = false;
    refresh(NULL);
    assert(flash_calls == flashes + 2);
    curio_app.alert_config.flash_enabled = false;
    fixture_notification.id++;
    fixture_notification.flash = true;
    refresh(NULL);
    assert(flash_calls == flashes + 2);
    fixture_visible = false;
    refresh(NULL);
    curio_feedback_set_muted(false, false);
    curio_app.alert_config = saved;
    refresh(NULL);
    puts("Flash: settings save failures, preview, rule/global gating, deduplication and mute independence passed");
}
static void grid_checks(void)
{
    curio_page_config_t *p = &curio_app.layout.pages[0];
    curio_widget_config_t agents;
    assert(curio_widget_config_clone(&agents, &p->widgets[0]));
    agents.row = 0;
    agents.rows = 6;
    curio_widget_config_free(&p->widgets[0]);
    assert(curio_widget_config_clone(&p->widgets[0], &agents));
    curio_app.layout.revision++;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(widget_count == 1 && ((agents_payload_t *)widgets[0]->data)->row_count == 7);
    assert(((agents_payload_t *)widgets[0]->data)->pages == 3);
    assert(lv_obj_get_y(widgets[0]->box) == 60 && lv_obj_get_height(widgets[0]->box) == 408);
    curio_widget_t *previous = widgets[0];
    fail_allocation = true;
    p->widgets[0].rows = 5;
    p->widgets[0].row = 1;
    curio_app.layout.revision++;
    refresh(NULL);
    assert(widgets[0] == previous && ((agents_payload_t *)widgets[0]->data)->row_count == 7);
    fail_allocation = false;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(((agents_payload_t *)widgets[0]->data)->row_count == 5 && ((agents_payload_t *)widgets[0]->data)->pages == 4);

    p->widget_count = 2;
    curio_widget_config_free(&p->widgets[0]);
    assert(curio_widget_config_clone(&p->widgets[0], &agents));
    p->widgets[0].cols = p->widgets[0].rows = 3;
    ((curio_agents_config_t *)p->widgets[0].options)->auto_page = true;
    assert(curio_widget_config_clone(&p->widgets[1], &p->widgets[0]));
    strcpy(p->widgets[1].id, "second");
    p->widgets[1].row = 3;
    ((curio_agents_config_t *)p->widgets[1].options)->auto_page = false;
    curio_app.layout.revision++;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(widget_count == 2);
    assert(((agents_payload_t *)widgets[0]->data)->row_count == 2 && ((agents_payload_t *)widgets[1]->data)->row_count == 2);
    assert(((agents_payload_t *)widgets[0]->data)->pages == 8 && ((agents_payload_t *)widgets[1]->data)->pages == 8);
    swipe(300, 150, -240, 0);
    assert(((agents_payload_t *)widgets[0]->data)->page == 1 && ((agents_payload_t *)widgets[1]->data)->page == 0);
    swipe(300, 360, -240, 0);
    assert(((agents_payload_t *)widgets[0]->data)->page == 1 && ((agents_payload_t *)widgets[1]->data)->page == 1);
    tap(170, 78);
    assert(((agents_payload_t *)widgets[0]->data)->filter == CURIO_FILTER_ATTENTION && ((agents_payload_t *)widgets[0]->data)->count == 0);
    assert(((agents_payload_t *)widgets[1]->data)->filter == CURIO_FILTER_ALL && ((agents_payload_t *)widgets[1]->data)->count == 16);
    tap(50, 78);
    ((curio_agents_config_t *)widgets[0]->config.options)->auto_page = ((curio_agents_config_t *)widgets[1]->config.options)->auto_page = true;
    ((agents_payload_t *)widgets[0]->data)->page = ((agents_payload_t *)widgets[1]->data)->page = 0;
    ((agents_payload_t *)widgets[0]->data)->next_page_ms = ((agents_payload_t *)widgets[1]->data)->next_page_ms = now + 100;
    step(100, 150, true);
    at(now + 100);
    assert(((agents_payload_t *)widgets[0]->data)->page == 0 && ((agents_payload_t *)widgets[1]->data)->page == 1);
    step(100, 150, false);
    ((curio_agents_config_t *)p->widgets[0].options)->auto_page = true;
    ((curio_agents_config_t *)p->widgets[1].options)->auto_page = false;
    tap(444, 28);
    assert(strstr(lv_label_get_text(settings_page), "Mixed"));
    assert(!lv_obj_has_state(paging_switch, LV_STATE_CHECKED));
    tap(418, 138);
    assert(((curio_agents_config_t *)p->widgets[0].options)->auto_page && ((curio_agents_config_t *)p->widgets[1].options)->auto_page);
    assert(!strstr(lv_label_get_text(settings_page), "Mixed"));
    tap(418, 138);
    assert(!((curio_agents_config_t *)p->widgets[0].options)->auto_page && !((curio_agents_config_t *)p->widgets[1].options)->auto_page);
    tap(410, 38);
    for (size_t w = 0; w < 2; w++) {
        agents_payload_t *a = ((agents_payload_t *)widgets[w]->data);
        for (size_t i = 0; i < a->row_count; i++)
            assert(lv_obj_get_y(a->rows[i].detail) + lv_obj_get_height(a->rows[i].detail) <= lv_obj_get_y(a->status));
        assert(lv_obj_get_y(a->status) + lv_obj_get_height(a->status) <= lv_obj_get_y(a->footer));
    }
    ((curio_agents_config_t *)widgets[0]->config.options)->all_hosts = false;
    ((curio_agents_config_t *)widgets[0]->config.options)->host_count = 1;
    strcpy(((curio_agents_config_t *)widgets[0]->config.options)->hosts[0], "missing");
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(((agents_payload_t *)widgets[0]->data)->status), "missing: Waiting"));
    assert(((agents_payload_t *)widgets[1]->data)->count == 16);

    const char *json = "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"Workstation\",\"sources\":{\"system.cpu\":{\"kind\":\"metrics\",\"status\":\"ok\",\"sample_age_ms\":0,\"metrics\":{\"usage_pct\":{\"value\":100,\"unit\":\"percent\"}}},\"system.memory\":{\"kind\":\"metrics\",\"status\":\"ok\",\"sample_age_ms\":0,\"metrics\":{\"usage_pct\":{\"value\":99.9,\"unit\":\"percent\"}}}}}";
    assert(curio_core_apply_json(&curio_app.core, json, strlen(json), now) == CURIO_OK);
    curio_page_free(p);
    p->widget_count = 3;
    p->widgets[0] = fixture_widget(&curio_widget_metric, 1, 1);
    strcpy(p->widgets[0].id, "tiny");
    strcpy(p->widgets[0].title, "CPU");
    strcpy(((curio_metric_config_t *)p->widgets[0].options)->host_id, "h");
    strcpy(((curio_metric_config_t *)p->widgets[0].options)->source_id, "system.cpu");
    strcpy(((curio_metric_config_t *)p->widgets[0].options)->key, "usage_pct");
    strcpy(((curio_metric_config_t *)p->widgets[0].options)->unit, "percent");
    assert(curio_widget_config_clone(&p->widgets[1], &p->widgets[0]));
    p->widgets[1].col = 1;
    p->widgets[1].cols = 2;
    p->widgets[2] = fixture_widget(&curio_widget_machine, 2, 2);
    strcpy(((curio_machine_config_t *)p->widgets[2].options)->host_id, "h");
    p->widgets[2].row = 2;
    p->widgets[2].cols = p->widgets[2].rows = 2;
    curio_app.layout.revision++;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(widget_count == 3 && curio_ui_widget_count() == 3);
    metric_payload_t *m = ((metric_payload_t *)widgets[0]->data);
    assert(!m->detail);
    assert(!strcmp(lv_label_get_text(m->value), "100%"));
    lv_point_t size;
    lv_text_get_size(&size, "100%", lv_obj_get_style_text_font(m->value, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    assert(size.x <= lv_obj_get_width(m->value));
    assert(lv_obj_get_style_text_font(m->value, 0) == &lv_font_montserrat_26);
    assert(((metric_payload_t *)widgets[1]->data)->detail);
    assert(!strcmp(lv_label_get_text(((metric_payload_t *)widgets[1]->data)->detail), "Workstation"));
    assert(!strcmp(lv_label_get_text(((machine_payload_t *)widgets[2]->data)->value[1]), "99.9%"));
    const curio_host_t *host = &curio_app.core.hosts[0];
    const curio_source_t *source = &host->sources[0];
    uint64_t sampled = now;
    at(sampled + CURIO_STALE_MS);
    assert(curio_source_health(source, now) == CURIO_HEALTH_FRESH);
    assert(!strcmp(lv_label_get_text(m->value), "100%"));
    at(now + 1);
    assert(curio_source_health(source, now) == CURIO_HEALTH_STALE);
    assert(!strcmp(lv_label_get_text(m->value), "Stale"));
    at(sampled + CURIO_OFFLINE_MS);
    assert(curio_source_health(source, now) == CURIO_HEALTH_STALE);
    assert(!strcmp(lv_label_get_text(m->value), "Stale"));
    at(now + 1);
    assert(curio_host_health(host, now) == CURIO_HEALTH_OFFLINE);
    assert(curio_source_health(source, now) == CURIO_HEALTH_STALE);
    assert(!strcmp(lv_label_get_text(m->value), "Offline"));
    assert(curio_core_apply_json(&curio_app.core, json, strlen(json), now) == CURIO_OK);
    curio_app.core.hosts[0].sources[0].status = CURIO_SOURCE_ERROR;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(!strcmp(lv_label_get_text(m->value), "Collection error"));
    assert(lv_label_get_long_mode(m->value) == LV_LABEL_LONG_WRAP);
    lv_text_get_size(&size, "Collection error", lv_obj_get_style_text_font(m->value, 0), 0, 0, lv_obj_get_width(m->value), LV_TEXT_FLAG_NONE);
    assert(size.y <= lv_obj_get_height(m->value));
    curio_app.core.hosts[0].sources[0].status = CURIO_SOURCE_OK;
    curio_app.core.hosts[0].sources[0].metrics[0].value = 123456789;
    refresh(NULL);
    assert(lv_label_get_long_mode(m->value) == LV_LABEL_LONG_SCROLL_CIRCULAR);
    assert(strstr(lv_label_get_text(m->value), "123456789"));
    curio_widget_config_free(&p->widgets[1]);
    curio_widget_config_free(&p->widgets[2]);
    p->widget_count = CURIO_LAYOUT_MAX_WIDGETS;
    for (size_t i = 0; i < p->widget_count; i++) {
        if (i) assert(curio_widget_config_clone(&p->widgets[i], &p->widgets[0]));
        snprintf(p->widgets[i].id, sizeof(p->widgets[i].id), "metric-%zu", i);
        p->widgets[i].col = i % CURIO_LAYOUT_COLUMNS;
        p->widgets[i].row = i / CURIO_LAYOUT_COLUMNS;
    }
    curio_app.layout.revision++;
    refresh(NULL);
    assert(widget_count == 8 && curio_ui_widget_count() == 8);
    unsigned children = lv_obj_get_child_count(lv_screen_active());
    curio_page_free(p);
    curio_app.layout.revision++;
    refresh(NULL);
    assert(!widget_count && !curio_ui_widget_count());
    assert(lv_obj_get_child_count(lv_screen_active()) == children - 8);
    unsigned allocations = config_allocations;
    refresh(NULL);
    assert(config_allocations == allocations && !widget_count);
    curio_widget_config_free(&agents);
    puts("Grid: dynamic rows, resize, independent instances, mixed settings, compact values, status and empty page passed");
}
static void status_widget_checks(void)
{
    const char *json = "{\"v\":2,\"host_id\":\"status-host\",\"host_name\":\"Status host\",\"sources\":{"
        "\"service.api\":{\"kind\":\"status\",\"status\":\"ok\",\"sample_age_ms\":20000,\"max_age_ms\":60000,"
        "\"state\":{\"text\":\"Online\",\"level\":\"ok\",\"detail\":\"HTTP 200\"}}}}";
    assert(curio_core_apply_json(&curio_app.core, json, strlen(json), now) == CURIO_OK);
    curio_page_config_t *p = &curio_app.layout.pages[0];
    p->widget_count = 2;
    p->widgets[0] = fixture_widget(&curio_widget_status, 1, 1);
    strcpy(p->widgets[0].id, "small-status");
    strcpy(p->widgets[0].title, "API");
    strcpy(((curio_status_config_t *)p->widgets[0].options)->host_id, "status-host");
    strcpy(((curio_status_config_t *)p->widgets[0].options)->source_id, "service.api");
    assert(curio_widget_config_clone(&p->widgets[1], &p->widgets[0]));
    strcpy(p->widgets[1].id, "large-status");
    p->widgets[1].col = 1;
    p->widgets[1].cols = p->widgets[1].rows = 2;
    curio_app.layout.revision++;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    assert(curio_ui_widget_count() == 2);
    status_payload_t *small = ((status_payload_t *)widgets[0]->data), *large = ((status_payload_t *)widgets[1]->data);
    assert(!small->detail && large->detail);
    assert(!strcmp(lv_label_get_text(small->value), "Online"));
    assert(!strcmp(lv_label_get_text(large->detail), "HTTP 200"));
    assert(small->color == 0x80C9B1);
    assert(lv_obj_get_y(large->detail) + lv_obj_get_height(large->detail) <= lv_obj_get_height(widgets[1]->box));
    curio_host_t *h = (curio_host_t *)curio_core_host(&curio_app.core, "status-host");
    curio_source_t *s = &h->sources[0];
    strcpy(s->state->text, "Failed");
    s->state->level = CURIO_STATUS_ERROR;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(small->value), "Failed") && small->color == 0xEF9090);
    s->status = CURIO_SOURCE_ERROR;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(small->value), "Collection error"));
    assert(!strcmp(lv_label_get_text(large->detail), ""));
    s->status = CURIO_SOURCE_OK;
    s->sample_age_ms = 60001;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(small->value), "Stale"));
    s->sample_age_ms = 0;
    now += CURIO_OFFLINE_MS + 1;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(small->value), "Offline"));
    h->last_report_ms = now;
    strcpy(((curio_status_config_t *)widgets[0]->config.options)->source_id, "future.source");
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(small->value), "Not reported"));
    unsigned children = lv_obj_get_child_count(lv_screen_active());
    curio_page_free(p);
    curio_app.layout.revision++;
    refresh(NULL);
    assert(!curio_ui_widget_count() && lv_obj_get_child_count(lv_screen_active()) == children - 2);
    puts("Status widgets: business state, collection failures, freshness and lifecycle passed");
}
static void clock_widget_checks(void)
{
    curio_page_config_t *p = &curio_app.layout.pages[0];
    p->widget_count = 2;
    p->widgets[0] = fixture_widget(&curio_widget_clock, 1, 1);
    ((curio_clock_config_t *)p->widgets[0].options)->seconds = true;
    ((curio_clock_config_t *)p->widgets[0].options)->utc_offset_minutes = 480;
    strcpy(p->widgets[0].id, "clock-small");
    assert(curio_widget_config_clone(&p->widgets[1], &p->widgets[0]));
    strcpy(p->widgets[1].id, "clock-large");
    p->widgets[1].col = 1;
    p->widgets[1].cols = 3;
    p->widgets[1].rows = 4;
    clock_now = 1735660800; /* 2024-12-31 16:00 UTC */
    curio_app.layout.revision++;
    refresh(NULL);
    lv_obj_update_layout(lv_screen_active());
    clock_payload_t *small = ((clock_payload_t *)widgets[0]->data), *large = ((clock_payload_t *)widgets[1]->data);
    assert(!small->date && large->date);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(widgets[0]->box, 0)), "Time"));
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(widgets[1]->box, 0)), "Time (UTC+8)"));
    assert(!strcmp(lv_label_get_text(small->time), "00:00:00"));
    assert(!strcmp(lv_label_get_text(large->date), "2025-01-01"));
    for (size_t i = 0; i < widget_count; i++) {
        lv_obj_t *time = ((clock_payload_t *)widgets[i]->data)->time;
        lv_point_t size;
        lv_text_get_size(&size, lv_label_get_text(time), lv_obj_get_style_text_font(time, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        assert(size.x <= lv_obj_get_width(time));
        assert(lv_obj_get_y(time) >= 23);
        assert(lv_obj_get_y(time) + size.y <= lv_obj_get_height(widgets[i]->box));
    }
    assert(lv_obj_get_style_text_font(large->time, 0) == &lv_font_montserrat_48);
    clock_now++;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(small->time), "00:00:01"));
    ((curio_clock_config_t *)widgets[1]->config.options)->utc_offset_minutes = -60;
    ((curio_clock_config_t *)widgets[1]->config.options)->seconds = false;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(large->time), "15:00"));
    assert(!strcmp(lv_label_get_text(large->date), "2024-12-31"));
    clock_now = 0;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(small->time), "--:--:--"));
    assert(!strcmp(lv_label_get_text(large->time), "--:--"));
    assert(!strcmp(lv_label_get_text(large->date), ""));
    const int offsets[] = {0, -30, -210, 345};
    const char *titles[] = {"Time (UTC)", "Time (UTC-0:30)", "Time (UTC-3:30)", "Time (UTC+5:45)"};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); i++) {
        ((curio_clock_config_t *)p->widgets[1].options)->utc_offset_minutes = offsets[i];
        curio_app.layout.revision++;
        refresh(NULL);
        assert(!strcmp(lv_label_get_text(lv_obj_get_child(widgets[1]->box, 0)), titles[i]));
    }
    strcpy(p->widgets[1].title, "Office");
    curio_app.layout.revision++;
    refresh(NULL);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(widgets[1]->box, 0)), "Office"));
    unsigned children = lv_obj_get_child_count(lv_screen_active());
    curio_page_free(p);
    curio_app.layout.revision++;
    refresh(NULL);
    assert(!curio_ui_widget_count() && lv_obj_get_child_count(lv_screen_active()) == children - 2);
    puts("Clock widgets: ticking, timezone, unsynced time, sizing and lifecycle passed");
}
int main(void)
{
    lv_init();
    lv_display_t *d = lv_display_create(480, 480);
    lv_display_set_default(d);
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    lv_indev_set_read_cb(indev, read_pointer);
    curio_core_init(&curio_app.core);
    curio_layout_default(&curio_app.layout);
    curio_app.layout.pages[0].widgets[0].row = 1;
    curio_app.layout.pages[0].widgets[0].rows = 5;
    curio_app.device.brightness = 75;
    curio_app.alert_config.vibration_strength = 60;
    char json[4096];
    size_t n = (size_t)snprintf(json, sizeof(json),
                                "{\"v\":2,\"host_id\":\"h\",\"host_name\":\"H\",\"sources\":{\"herdr\":{"
                                "\"kind\":\"agents\",\"status\":\"ok\",\"sample_age_ms\":0,\"agents\":[");
    for (int i = 0; i < 16; i++)
        n += (size_t)snprintf(
            json + n, sizeof(json) - n,
            "%s{\"id\":\"a%d\",\"name\":\"A%d\",\"project\":\"p\",\"state\":\"working\"}",
            i ? "," : "", i, i);
    n += (size_t)snprintf(json + n, sizeof(json) - n, "]}}}");
    assert(curio_core_apply_json(&curio_app.core, json, n, 0) == CURIO_OK);
    curio_ui_start();
    lv_obj_update_layout(lv_screen_active());
    setup_checks();
    for (size_t i = 0; i < 3; i++)
        lv_obj_add_event_cb(((agents_payload_t *)widgets[0]->data)->filters[i], count_click, LV_EVENT_CLICKED,
                            NULL);
    step(400, 155, true);
    step(400, 155, false);
    assert(((agents_payload_t *)widgets[0]->data)->filter == CURIO_FILTER_WORKING);
    assert(clicks == 1);
    step(80, 155, true);
    step(80, 155, false);
    check(0, CURIO_FILTER_ALL);
    assert(clicks == 2);
    check(0, CURIO_FILTER_ALL);
    swipe(400, 250, -300, 0);
    check(1, CURIO_FILTER_ALL);
    swipe(400, 250, -300, 0);
    check(2, CURIO_FILTER_ALL);
    swipe(400, 250, -300, 0);
    check(3, CURIO_FILTER_ALL);
    swipe(400, 250, -300, 0);
    check(3, CURIO_FILTER_ALL);
    swipe(80, 250, 300, 0);
    check(2, CURIO_FILTER_ALL);
    swipe(80, 250, 300, 0);
    check(1, CURIO_FILTER_ALL);
    swipe(80, 250, 300, 0);
    check(0, CURIO_FILTER_ALL);
    swipe(80, 250, 300, 0);
    check(0, CURIO_FILTER_ALL);
    swipe(250, 250, -10, 0);
    check(0, CURIO_FILTER_ALL);
    swipe(250, 250, 0, 100);
    check(0, CURIO_FILTER_ALL);
    swipe(400, 155, -300, 0);
    check(1, CURIO_FILTER_ALL);
    assert(clicks == 2);
    step(250, 155, true);
    for (int i = 1; i <= 8; i++)
        step(250, 155 + i * 12, true);
    for (int i = 7; i >= 0; i--)
        step(250, 155 + i * 12, true);
    step(250, 155, false);
    printf("vertical return %s filter=%d clicks=%u\n", lv_label_get_text(((agents_payload_t *)widgets[0]->data)->footer),
           ((agents_payload_t *)widgets[0]->data)->filter, clicks);
    check(1, CURIO_FILTER_ALL);
    assert(clicks == 2);
    puts("Real LVGL pointer gesture checks passed (including click suppression and positive tap "
         "control)");
    extra_checks();
    notification_checks();
    notification_layout_checks();
    battery_checks();
    settings_checks();
    alert_details_checks();
    header_checks();
    flash_checks();
    grid_checks();
    status_widget_checks();
    clock_widget_checks();
    curio_layout_free(&curio_app.layout);
    curio_core_destroy(&curio_app.core);
    return 0;
}
