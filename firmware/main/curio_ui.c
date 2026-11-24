#include "curio_app.h"
#include "curio_screen.h"
#include "curio_layout.h"
#include "curio_feedback.h"
#include "curio_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include "bsp/display.h"
#include "esp_check.h"
#include "esp_iris.h"
#include "freertos/task.h"
#include "lvgl.h"

static bool first_frame_submitted;
static bool health_acceptance_scheduled;

static void accept_healthy(void *arg)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_mark_healthy());
    vTaskDelete(NULL);
}

static void first_refresh(lv_event_t *event)
{
    if (health_acceptance_scheduled) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_REFR_READY) {
        first_frame_submitted = true;
        lv_display_t *display = lv_event_get_target(event);
        lv_obj_invalidate(lv_display_get_screen_active(display));
    } else if (code == LV_EVENT_FLUSH_WAIT_FINISH && first_frame_submitted) {
        health_acceptance_scheduled = xTaskCreate(accept_healthy, "curio_healthy", 3072, NULL, 4, NULL) == pdPASS;
    }
}

typedef struct {
    lv_obj_t *name, *state, *detail;
    char name_text[160], detail_text[224], state_text[24];
    uint32_t color;
} agent_view_row_t;
typedef struct {
    lv_obj_t *filters[3], *status, *footer;
    char summary[96];
    uint64_t next_page_ms;
    size_t row_count;
    agent_view_row_t rows[];
} agents_payload_t;
typedef struct {
    lv_obj_t *value[2], *detail;
    char values[2][160], detail_text[49];
} metric_payload_t;
typedef struct {
    lv_obj_t *value, *detail;
    char value_text[65], detail_text[161];
    uint32_t color;
} status_payload_t;
typedef struct {
    lv_obj_t *time, *date;
    curio_clock_text_t text;
} clock_payload_t;
typedef struct {
    lv_obj_t *box;
    curio_widget_config_t config;
    curio_filter_t filter;
    size_t page, pages, count;
    union { clock_payload_t *clock; agents_payload_t *agents; metric_payload_t *metric; status_payload_t *status; void *payload; } data;
} widget_t;
static atomic_size_t published_count;
static widget_t *widgets[CURIO_LAYOUT_MAX_WIDGETS];
static size_t widget_count, selected_page;
static uint64_t layout_revision = UINT64_MAX;
static char active_page_id[33], default_page_id[33];
static lv_obj_t *page_title, *battery_label;
static lv_obj_t *wifi_button, *wifi_warning, *sound_button, *vibration_button, *sound_icon;
static lv_obj_t *network_popup, *network_status, *network_ip;
static uint64_t network_until;
static lv_obj_t *setup_mask, *setup_details;
static bool setup_shown;
static lv_obj_t *notification_mask, *notification_title, *notification_body, *notification_detail, *notification_countdown;
static uint32_t notification_id, notification_revision;
static bool notification_active, preview_active;
static uint64_t preview_until;
static lv_obj_t *settings_mask, *rotate_switch, *paging_switch, *flash_switch, *settings_page, *settings_alerts, *settings_error;
static lv_obj_t *alert_details_mask, *alert_details_list;
static uint32_t alert_details_revision;
static char settings_page_id[33];
static char pressed_page_id[33];
static lv_obj_t *brightness_slider, *volume_slider, *vibration_slider;
static lv_obj_t *brightness_value, *volume_value, *vibration_value;
static lv_obj_t *dragging_slider;
static void settings_open(lv_event_t *event);
static const uint32_t fg = 0xE8EFF7, muted = 0x94A6BC;
static void refresh(lv_timer_t *timer);
static void text(lv_obj_t *obj, const char *value);
static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font, uint32_t color);
static lv_obj_t *button(lv_obj_t *parent, const char *caption, int x, int y, int width, lv_event_cb_t callback, void *data);

static void notification_close(lv_event_t *event)
{
    (void)event;
    if (preview_active) {
        preview_active = false;
        refresh(NULL);
        return;
    }
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    if (curio_app.alerts && curio_alerts_revision(curio_app.alerts) == notification_revision) {
        const curio_alert_notification_t *current = curio_alerts_current(curio_app.alerts, curio_now_ms());
        if (current && current->id == notification_id) curio_alerts_dismiss(curio_app.alerts, curio_now_ms());
    }
    xSemaphoreGive(curio_app.mutex);
    refresh(NULL);
}

static void notification_show(const curio_alert_notification_t *notification, uint32_t revision, uint64_t remaining_ms)
{
    if (!notification) {
        if (notification_mask) lv_obj_delete(notification_mask);
        notification_mask = NULL;
        notification_id = 0;
        return;
    }
    bool is_new = !notification_mask || notification_id != notification->id || notification_revision != revision;
    if (is_new) for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input))
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED)
            lv_indev_wait_release(input);
    if (!notification_mask) {
        notification_mask = lv_obj_create(lv_screen_active());
        lv_obj_set_pos(notification_mask, 0, 0);
        lv_obj_set_size(notification_mask, 480, 480);
        lv_obj_set_style_pad_all(notification_mask, 0, 0);
        lv_obj_set_style_border_width(notification_mask, 0, 0);
        lv_obj_set_style_radius(notification_mask, 0, 0);
        lv_obj_set_style_bg_color(notification_mask, lv_color_hex(0x060B11), 0);
        lv_obj_set_style_bg_opa(notification_mask, LV_OPA_80, 0);
        lv_obj_remove_flag(notification_mask, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(notification_mask, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *panel = lv_obj_create(notification_mask);
        lv_obj_set_size(panel, 448, LV_SIZE_CONTENT);
        lv_obj_center(panel);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(panel, 20, 0);
        lv_obj_set_style_pad_row(panel, 16, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0x192534), 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_t *content = lv_obj_create(panel);
        lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(content, 336, 0);
        lv_obj_set_style_pad_all(content, 0, 0);
        lv_obj_set_style_pad_row(content, 12, 0);
        lv_obj_set_style_border_width(content, 0, 0);
        lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
        lv_obj_set_scroll_dir(content, LV_DIR_VER);
        lv_obj_remove_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
        notification_title = label(content, 0, 0, 408, &lv_font_montserrat_32, muted);
        notification_body = label(content, 0, 0, 408, &lv_font_montserrat_48, fg);
        notification_detail = label(content, 0, 0, 408, &lv_font_montserrat_26, muted);
        lv_label_set_long_mode(notification_title, LV_LABEL_LONG_WRAP);
        lv_label_set_long_mode(notification_body, LV_LABEL_LONG_WRAP);
        lv_label_set_long_mode(notification_detail, LV_LABEL_LONG_WRAP);
        lv_obj_t *close = button(panel, "Close", 0, 0, 408, notification_close, NULL);
        lv_obj_set_height(close, 56);
        lv_obj_set_style_text_font(lv_obj_get_child(close, 0), &lv_font_montserrat_26, 0);
        notification_countdown = label(close, 0, 0, 88, &lv_font_montserrat_26, muted);
        lv_obj_align(notification_countdown, LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_set_style_text_align(notification_countdown, LV_TEXT_ALIGN_RIGHT, 0);
    }
    notification_id = notification->id;
    notification_revision = revision;
    text(notification_title, notification->title);
    text(notification_body, notification->body);
    text(notification_detail, notification->detail);
    if (notification->detail[0]) lv_obj_remove_flag(notification_detail, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(notification_detail, LV_OBJ_FLAG_HIDDEN);
    if (is_new) lv_obj_scroll_to_y(lv_obj_get_parent(notification_body), 0, LV_ANIM_OFF);
    if (remaining_ms) {
        char countdown[24];
        snprintf(countdown, sizeof(countdown), "%llus", (unsigned long long)((remaining_ms + 999) / 1000));
        text(notification_countdown, countdown);
        lv_obj_remove_flag(notification_countdown, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(notification_countdown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notification_mask);
    if (is_new) curio_feedback_play(notification->sound ? notification->volume : 0,
                                    notification->vibrate ? notification->vibration_strength : 0,
                                    notification->flash);
}

static void text(lv_obj_t *obj, const char *value)
{
    char ascii[192];
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p && n + 1 < sizeof(ascii); p++) {
        if (*p < 128) ascii[n++] = *p >= 32 ? *p : ' ';
        else if ((*p & 0xC0) != 0x80) ascii[n++] = '?';
    }
    ascii[n] = 0;
    if (strcmp(lv_label_get_text(obj), ascii)) lv_label_set_text(obj, ascii);
}

static void battery_update(const curio_battery_t *battery, uint64_t now)
{
    curio_battery_state_t state = curio_battery_state(battery, now);
    char caption[32];
    uint32_t color = muted;
    if (state == CURIO_BATTERY_UNAVAILABLE || state == CURIO_BATTERY_ABSENT) {
        snprintf(caption, sizeof(caption), LV_SYMBOL_BATTERY_EMPTY " --");
    } else {
        const char *icon = battery->level_pct >= 90 ? LV_SYMBOL_BATTERY_FULL :
            battery->level_pct >= 65 ? LV_SYMBOL_BATTERY_3 :
            battery->level_pct >= 40 ? LV_SYMBOL_BATTERY_2 :
            battery->level_pct >= 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        snprintf(caption, sizeof(caption), "%s%s %u%%",
            state == CURIO_BATTERY_CHARGING ? LV_SYMBOL_CHARGE " " : "", icon, battery->level_pct);
        if (state == CURIO_BATTERY_CHARGING || state == CURIO_BATTERY_FULL) color = 0x80C9B1;
        else if (battery->level_pct <= 20) color = 0xE8BD79;
    }
    if (strcmp(lv_label_get_text(battery_label), caption)) lv_label_set_text(battery_label, caption);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(color), 0);
}
static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *caption, int x, int y, int width, lv_event_cb_t callback, void *data)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 44);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x243243), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    lv_obj_t *name = label(obj, 0, 0, width, &lv_font_montserrat_14, fg);
    text(name, caption);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(name);
    return obj;
}
static void header_icon_draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int x = (area.x1 + area.x2) / 2, y = (area.y1 + area.y2) / 2;
    lv_color_t color = lv_obj_get_style_text_color(obj, 0);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = 2;
    line.round_start = line.round_end = true;
    if (obj == vibration_button) {
        lv_draw_rect_dsc_t phone;
        lv_draw_rect_dsc_init(&phone);
        phone.bg_opa = LV_OPA_TRANSP;
        phone.border_color = color;
        phone.border_width = 2;
        phone.radius = 2;
        lv_area_t bounds = {x - 5, y - 10, x + 5, y + 10};
        lv_draw_rect(layer, &phone, &bounds);
        const lv_point_t wave[] = {{10, -8}, {13, -4}, {10, 0}, {13, 4}, {10, 8}};
        for (int side = -1; side <= 1; side += 2) for (size_t i = 1; i < sizeof(wave) / sizeof(wave[0]); i++) {
            line.p1 = (lv_point_precise_t){x + side * wave[i - 1].x, y + wave[i - 1].y};
            line.p2 = (lv_point_precise_t){x + side * wave[i].x, y + wave[i].y};
            lv_draw_line(layer, &line);
        }
    }
    if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        line.p1 = (lv_point_precise_t){x - 12, y + 12};
        line.p2 = (lv_point_precise_t){x + 12, y - 12};
        lv_draw_line(layer, &line);
    }
}
static void quick_mute(lv_event_t *event)
{
    curio_feedback_status_t status = curio_feedback_status();
    bool audio = status.audio.muted, vibration = status.vibration.muted;
    if (lv_event_get_target(event) == sound_button) audio = !audio;
    else vibration = !vibration;
    curio_feedback_set_muted(audio, vibration);
    refresh(NULL);
}
static void network_click(lv_event_t *event)
{
    (void)event;
    if (network_popup) {
        lv_obj_delete(network_popup);
        network_popup = NULL;
        return;
    }
    network_popup = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(network_popup, 24, 60);
    lv_obj_set_size(network_popup, 432, 112);
    lv_obj_set_style_pad_all(network_popup, 0, 0);
    lv_obj_set_style_border_width(network_popup, 1, 0);
    lv_obj_set_style_border_color(network_popup, lv_color_hex(0x34475B), 0);
    lv_obj_set_style_bg_color(network_popup, lv_color_hex(0x192534), 0);
    lv_obj_remove_flag(network_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(network_popup, network_click, LV_EVENT_CLICKED, NULL);
    network_status = label(network_popup, 16, 16, 400, &lv_font_montserrat_18, fg);
    lv_label_set_long_mode(network_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(network_status, 48);
    network_ip = label(network_popup, 16, 78, 400, &lv_font_montserrat_14, muted);
    network_until = curio_now_ms() + 5000;
    refresh(NULL);
}
static lv_obj_t *header_button(lv_obj_t *parent, int x, int width, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, 8);
    lv_obj_set_size(obj, width, 44);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x243243), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    return obj;
}
static lv_obj_t *header_icon(lv_obj_t *parent, int x, const char *symbol, lv_event_cb_t callback)
{
    lv_obj_t *obj = header_button(parent, x, 44, callback);
    lv_obj_t *icon = label(obj, 0, 0, 44, &lv_font_montserrat_18, muted);
    lv_obj_remove_local_style_prop(icon, LV_STYLE_TEXT_COLOR, 0);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(icon);
    lv_obj_add_event_cb(obj, header_icon_draw, LV_EVENT_DRAW_POST, NULL);
    return obj;
}
static void mute_button_update(lv_obj_t *obj, bool blocked)
{
    if (blocked) lv_obj_add_state(obj, LV_STATE_CHECKED);
    else lv_obj_remove_state(obj, LV_STATE_CHECKED);
}
static lv_obj_t *settings_sheet(lv_obj_t *parent, const char *title, const char *action, lv_event_cb_t close)
{
    lv_obj_t *sheet = lv_obj_create(parent);
    lv_obj_set_pos(sheet, 0, 0);
    lv_obj_set_size(sheet, 480, 480);
    lv_obj_set_style_pad_all(sheet, 0, 0);
    lv_obj_set_style_border_width(sheet, 0, 0);
    lv_obj_set_style_radius(sheet, 0, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(0x101822), 0);
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    text(label(sheet, 24, 24, 300, &lv_font_montserrat_26, fg), title);
    button(sheet, action, 364, 16, 92, close, NULL);
    return sheet;
}
static void setup_close(lv_event_t *event)
{
    (void)event;
    curio_network_setup(false);
    lv_obj_delete(setup_mask);
    setup_mask = NULL;
}
static void setup_open(lv_event_t *event)
{
    (void)event;
    if (setup_mask) return;
    setup_shown = true;
    curio_network_setup(true);
    setup_mask = settings_sheet(lv_screen_active(), "Wi-Fi setup", "Close", setup_close);
    setup_details = label(setup_mask, 32, 110, 416, &lv_font_montserrat_26, fg);
    lv_label_set_long_mode(setup_details, LV_LABEL_LONG_WRAP);
    text(setup_details, "Starting...");
    text(label(setup_mask, 32, 380, 416, &lv_font_montserrat_26, muted), "http://192.168.4.1");
}
static void alert_details_close(lv_event_t *event)
{
    (void)event;
    lv_obj_delete(alert_details_mask);
    alert_details_mask = alert_details_list = NULL;
}
static void alert_rule_card(const curio_alert_rule_t *rule, const curio_alert_config_t *config)
{
    lv_obj_t *card = lv_obj_create(alert_details_list);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x192534), 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    const char *title = rule->type == CURIO_ALERT_AGENT_DONE ? "Agent finished" :
        rule->type == CURIO_ALERT_METRIC_HIGH ? "Metric high" : "Host offline";
    text(label(card, 0, 0, 320, &lv_font_montserrat_18, fg), title);
    lv_obj_t *enabled = label(card, 352, 0, 72, &lv_font_montserrat_14, rule->enabled ? 0x80C9B1 : muted);
    lv_obj_set_style_text_align(enabled, LV_TEXT_ALIGN_RIGHT, 0);
    text(enabled, rule->enabled ? "Enabled" : "Disabled");
    lv_obj_t *id = label(card, 0, 28, 424, &lv_font_montserrat_14, muted);
    lv_label_set_long_mode(id, LV_LABEL_LONG_WRAP);
    text(id, rule->id);
    lv_obj_update_layout(card);

    char condition[320], duration[32], sound[32], vibration[32], body[640];
    const char *host = strcmp(rule->host_id, "*") ? rule->host_id : "All hosts";
    if (rule->type == CURIO_ALERT_AGENT_DONE) {
        snprintf(condition, sizeof(condition), "Source: %s\nState: Working -> %s", rule->source_id,
            rule->include_idle ? "Done / Idle" : "Done");
    } else if (rule->type == CURIO_ALERT_METRIC_HIGH) {
        bool percent = !strcmp(rule->unit, "percent");
        const char *unit = percent ? "%" : rule->unit, *space = percent ? "" : " ";
        snprintf(condition, sizeof(condition), "Metric: %s / %s\nAbove: %.15g%s%s for %lus\nRecover: <= %.15g%s%s",
            rule->source_id, rule->key, rule->threshold, space, unit, (unsigned long)rule->hold_s, rule->recover, space, unit);
    } else {
        snprintf(condition, sizeof(condition), "Offline for: %lus", (unsigned long)rule->hold_s);
    }
    if (rule->display_s) snprintf(duration, sizeof(duration), "%us", rule->display_s);
    else snprintf(duration, sizeof(duration), "Until closed");
    if (rule->sound) snprintf(sound, sizeof(sound), "On (%u%%)", config->volume);
    else snprintf(sound, sizeof(sound), "Off");
    if (rule->vibrate) snprintf(vibration, sizeof(vibration), "On (%u%%)", config->vibration_strength);
    else snprintf(vibration, sizeof(vibration), "Off");
    snprintf(body, sizeof(body), "Host: %s\n%s\nDisplay: %s\nSound: %s\nVibration: %s\nFlash: %s",
        host, condition, duration, sound, vibration,
        !rule->flash ? "Off" : config->flash_enabled ? "On" : "Off (global)");
    lv_obj_t *details = label(card, 0, 40 + lv_obj_get_height(id), 424, &lv_font_montserrat_14, fg);
    lv_label_set_long_mode(details, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(details, 6, 0);
    lv_label_set_text(details, body);
}
static void alert_details_update(void)
{
    if (!alert_details_mask) return;
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    uint32_t revision = curio_app.alert_config.revision;
    xSemaphoreGive(curio_app.mutex);
    if (revision == alert_details_revision) return;
    curio_alert_config_t *config = curio_alloc(sizeof(*config));
    if (!config) {
        alert_details_close(NULL);
        text(settings_error, "Out of memory");
        return;
    }
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    *config = curio_app.alert_config;
    xSemaphoreGive(curio_app.mutex);
    int32_t scroll = lv_obj_get_scroll_y(alert_details_list);
    lv_obj_clean(alert_details_list);
    if (!config->rule_count) text(label(alert_details_list, 12, 12, 424, &lv_font_montserrat_18, muted), "No rules");
    for (size_t i = 0; i < config->rule_count; i++) alert_rule_card(&config->rules[i], config);
    lv_obj_update_layout(alert_details_list);
    lv_obj_scroll_to_y(alert_details_list, scroll, LV_ANIM_OFF);
    alert_details_revision = config->revision;
    free(config);
}
static void alert_details_open(lv_event_t *event)
{
    (void)event;
    if (alert_details_mask) return;
    alert_details_mask = settings_sheet(settings_mask, "Alerts", "Back", alert_details_close);
    alert_details_list = lv_obj_create(alert_details_mask);
    lv_obj_set_pos(alert_details_list, 12, 76);
    lv_obj_set_size(alert_details_list, 456, 380);
    lv_obj_set_style_bg_opa(alert_details_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(alert_details_list, 0, 0);
    lv_obj_set_style_pad_all(alert_details_list, 0, 0);
    lv_obj_set_style_pad_row(alert_details_list, 12, 0);
    lv_obj_set_flex_flow(alert_details_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(alert_details_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(alert_details_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(alert_details_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    alert_details_revision = UINT32_MAX;
    alert_details_update();
}
static void settings_close(lv_event_t *event)
{
    (void)event;
    dragging_slider = NULL;
    lv_obj_delete(settings_mask);
    settings_mask = NULL;
    alert_details_mask = alert_details_list = NULL;
    refresh(NULL);
}
static void settings_change(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        snprintf(pressed_page_id, sizeof(pressed_page_id), "%s", settings_page_id);
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    esp_err_t error;
    if (target == rotate_switch) error = curio_settings_set_auto_rotate(enabled);
    else if (target == flash_switch) error = curio_settings_set_flash_enabled(enabled);
    else error = curio_settings_set_page_auto_page(pressed_page_id, enabled);
    refresh(NULL);
    text(settings_error, error == ESP_OK ? "" : esp_err_to_name(error));
}
static void preview_start(lv_event_t *event)
{
    (void)event;
    refresh(NULL);
    if (notification_active) return;
    preview_active = true;
    preview_until = curio_now_ms() + 10000;
    refresh(NULL);
}
static lv_obj_t *settings_toggle(const char *caption, int y)
{
    text(label(settings_mask, 24, y + 9, 330, &lv_font_montserrat_18, fg), caption);
    lv_obj_t *toggle = lv_switch_create(settings_mask);
    lv_obj_set_pos(toggle, 388, y);
    lv_obj_set_size(toggle, 64, 36);
    lv_obj_add_event_cb(toggle, settings_change, LV_EVENT_ALL, NULL);
    return toggle;
}
static void slider_caption(lv_obj_t *target, unsigned value)
{
    char caption[24];
    const char *suffix = value ? "" : target == volume_slider ? " (Mute)" : target == vibration_slider ? " (Off)" : "";
    snprintf(caption, sizeof(caption), "%u%%%s", value, suffix);
    text(target == brightness_slider ? brightness_value : target == volume_slider ? volume_value : vibration_value, caption);
}
static void settings_slider_change(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) dragging_slider = target;
    if (code == LV_EVENT_VALUE_CHANGED) {
        unsigned value = lv_slider_get_value(target);
        slider_caption(target, value);
        if (target == brightness_slider) bsp_display_brightness_set(value);
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    bool commit = code == LV_EVENT_RELEASED;
    dragging_slider = NULL;
    esp_err_t error = ESP_OK;
    if (commit) {
        uint8_t value = lv_slider_get_value(target);
        error = target == brightness_slider ? curio_settings_set_brightness(value) :
            target == volume_slider ? curio_settings_set_volume(value) : curio_settings_set_vibration_strength(value);
    }
    refresh(NULL);
    if (target == brightness_slider) {
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
        int brightness = curio_app.device.brightness;
        xSemaphoreGive(curio_app.mutex);
        bsp_display_brightness_set(brightness);
    }
    text(settings_error, error == ESP_OK ? "" : esp_err_to_name(error));
}
static lv_obj_t *settings_slider(const char *caption, int y, int minimum, lv_obj_t **value_label)
{
    text(label(settings_mask, 24, y, 240, &lv_font_montserrat_18, fg), caption);
    lv_obj_t *value = label(settings_mask, 314, y, 138, &lv_font_montserrat_14, muted);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    *value_label = value;
    lv_obj_t *slider = lv_slider_create(settings_mask);
    lv_obj_set_pos(slider, 36, y + 32);
    lv_obj_set_size(slider, 404, 14);
    lv_slider_set_range(slider, minimum, 100);
    lv_obj_add_event_cb(slider, settings_slider_change, LV_EVENT_ALL, NULL);
    return slider;
}
static void settings_open(lv_event_t *event)
{
    (void)event;
    if (settings_mask) return;
    settings_mask = settings_sheet(lv_screen_active(), "Settings", "Close", settings_close);
    button(settings_mask, "Wi-Fi", 264, 16, 88, setup_open, NULL);
    rotate_switch = settings_toggle("Auto rotate", 72);
    paging_switch = settings_toggle("Agent auto-page", 120);
    settings_page = label(settings_mask, 24, 162, 420, &lv_font_montserrat_14, muted);
    flash_switch = settings_toggle("Flash", 184);
    brightness_slider = settings_slider("Brightness", 234, 5, &brightness_value);
    volume_slider = settings_slider("Volume", 290, 0, &volume_value);
    vibration_slider = settings_slider("Vibration", 346, 0, &vibration_value);
    lv_obj_t *alerts_button = button(settings_mask, "", 24, 404, 228, alert_details_open, NULL);
    lv_obj_set_height(alerts_button, 50);
    text(label(alerts_button, 12, 4, 164, &lv_font_montserrat_18, fg), "Alerts");
    settings_alerts = label(alerts_button, 12, 28, 180, &lv_font_montserrat_14, muted);
    lv_label_set_text(label(alerts_button, 198, 16, 18, &lv_font_montserrat_14, muted), LV_SYMBOL_RIGHT);
    button(settings_mask, "Test alert", 276, 408, 180, preview_start, NULL);
    settings_error = label(settings_mask, 24, 458, 432, &lv_font_montserrat_14, 0xE8BD79);
    text(settings_error, "");
    refresh(NULL);
}
static void filter_click(lv_event_t *event)
{
    widget_t *w = lv_event_get_user_data(event);
    for (size_t i = 0; i < 3; i++) if (lv_event_get_target(event) == w->data.agents->filters[i]) w->filter = (curio_filter_t)i;
    w->page = 0;
    w->data.agents->next_page_ms = curio_now_ms() + 10000;
    refresh(NULL);
}
static void agent_swipe(lv_event_t *event)
{
    lv_indev_t *input = lv_indev_active();
    if (!input) return;
    lv_dir_t direction = lv_indev_get_gesture_dir(input);
    lv_indev_wait_release(input);
    widget_t *w = lv_event_get_user_data(event);
    w->data.agents->next_page_ms = curio_now_ms() + 10000;
    if (direction != LV_DIR_LEFT && direction != LV_DIR_RIGHT) return;
    if (direction == LV_DIR_LEFT && w->page + 1 < w->pages) w->page++;
    else if (direction == LV_DIR_RIGHT && w->page) w->page--;
    refresh(NULL);
}
static void switch_page(lv_event_t *event)
{
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    if (curio_app.layout.page_count) selected_page = (selected_page + 1) % curio_app.layout.page_count;
    xSemaphoreGive(curio_app.mutex);
    active_page_id[0] = 0;
    layout_revision = UINT64_MAX;
    refresh(NULL);
}
static widget_t *widget_create(lv_obj_t *parent, const curio_widget_config_t *config)
{
    curio_widget_rect_t rect = curio_widget_rect(config);
    widget_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->config = *config;
    w->filter = config->filter;
    bool agents = config->type == CURIO_WIDGET_AGENTS;
    size_t rows = agents ? (rect.height - 72) / 47 : 0;
    void *payload = calloc(1, agents ? sizeof(agents_payload_t) + rows * sizeof(agent_view_row_t) :
        config->type == CURIO_WIDGET_CLOCK ? sizeof(clock_payload_t) :
        config->type == CURIO_WIDGET_STATUS ? sizeof(status_payload_t) : sizeof(metric_payload_t));
    if (!payload) { free(w); return NULL; }
    w->data.payload = payload;
    w->box = lv_obj_create(parent);
    lv_obj_set_pos(w->box, rect.x, rect.y);
    lv_obj_set_size(w->box, rect.width, rect.height);
    lv_obj_set_style_pad_all(w->box, 0, 0);
    lv_obj_set_style_border_width(w->box, 0, 0);
    lv_obj_set_style_bg_color(w->box, lv_color_hex(0x192534), 0);
    lv_obj_remove_flag(w->box, LV_OBJ_FLAG_SCROLLABLE);
    int width = rect.width - 20;
    if (agents) {
        agents_payload_t *a = w->data.agents;
        a->row_count = rows;
        a->next_page_ms = curio_now_ms() + 10000;
        lv_obj_remove_flag(w->box, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(w->box, agent_swipe, LV_EVENT_GESTURE, w);
        const char *filters[] = {"All", "Attention", "Working"};
        for (size_t i = 0; i < 3; i++) {
            int left = rect.width * i / 3, right = rect.width * (i + 1) / 3;
            a->filters[i] = button(w->box, filters[i], left, 0, right - left - 4, filter_click, w);
            lv_obj_set_height(a->filters[i], 36);
            lv_obj_center(lv_obj_get_child(a->filters[i], 0));
        }
        for (size_t i = 0; i < rows; i++) {
            agent_view_row_t *row = &a->rows[i];
            row->name = label(w->box, 10, 44 + i * 47, width - 130, &lv_font_montserrat_18, fg);
            row->state = label(w->box, rect.width - 130, 44 + i * 47, 120, &lv_font_montserrat_18, muted);
            lv_obj_set_style_text_align(row->state, LV_TEXT_ALIGN_RIGHT, 0);
            row->detail = label(w->box, 10, 65 + i * 47, width, &lv_font_montserrat_14, muted);
        }
        a->status = label(w->box, 10, rect.height - 40, width, &lv_font_montserrat_14, 0xE8BD79);
        a->footer = label(w->box, 10, rect.height - 22, width, &lv_font_montserrat_14, muted);
        lv_obj_set_style_text_align(a->footer, LV_TEXT_ALIGN_CENTER, 0);
    } else if (config->type == CURIO_WIDGET_CLOCK) {
        clock_payload_t *c = w->data.clock;
        char title[32];
        const char *caption = config->title;
        if (!*caption) {
            int offset = abs(config->utc_offset_minutes);
            char sign = config->utc_offset_minutes < 0 ? '-' : '+';
            if (!offset) snprintf(title, sizeof(title), "Time (UTC)");
            else if (offset % 60) snprintf(title, sizeof(title), "Time (UTC%c%d:%02d)", sign, offset / 60, offset % 60);
            else snprintf(title, sizeof(title), "Time (UTC%c%d)", sign, offset / 60);
            lv_point_t size;
            lv_text_get_size(&size, title, &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            caption = size.x <= rect.width - 16 ? title : "Time";
        }
        text(label(w->box, 8, 6, rect.width - 16, &lv_font_montserrat_14, muted), caption);
        bool date = config->rows > 1;
        int available_height = rect.height - (date ? 56 : 28);
        const lv_font_t *fonts[] = {&lv_font_montserrat_48, &lv_font_montserrat_32,
            &lv_font_montserrat_26, &lv_font_montserrat_18, &lv_font_montserrat_14};
        const lv_font_t *font = fonts[4];
        for (size_t i = 0; i < 5; i++) {
            lv_point_t size;
            lv_text_get_size(&size, config->seconds ? "88:88:88" : "88:88", fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x <= rect.width - 16 && size.y <= available_height) { font = fonts[i]; break; }
        }
        int y = 25 + (available_height - lv_font_get_line_height(font)) / 2;
        c->time = label(w->box, 8, y, rect.width - 16, font, fg);
        lv_obj_set_style_text_align(c->time, LV_TEXT_ALIGN_CENTER, 0);
        if (date) {
            c->date = label(w->box, 8, rect.height - 25, rect.width - 16, &lv_font_montserrat_14, muted);
            lv_obj_set_style_text_align(c->date, LV_TEXT_ALIGN_CENTER, 0);
        }
    } else if (config->type == CURIO_WIDGET_STATUS) {
        status_payload_t *s = w->data.status;
        bool narrow = config->cols == 1, short_row = config->rows == 1;
        text(label(w->box, 8, 6, narrow || !short_row ? rect.width - 16 : width / 2 - 8,
            &lv_font_montserrat_14, muted), config->title);
        s->value = label(w->box, narrow || !short_row ? 8 : rect.width / 2, narrow || !short_row ? 25 : 3,
            narrow || !short_row ? rect.width - 16 : rect.width / 2 - 10, &lv_font_montserrat_26, fg);
        if (short_row && !narrow) lv_obj_set_style_text_align(s->value, LV_TEXT_ALIGN_RIGHT, 0);
        if (!short_row || !narrow) {
            int y = short_row ? 40 : 60;
            s->detail = label(w->box, 8, y, rect.width - 16, &lv_font_montserrat_14, muted);
            lv_label_set_long_mode(s->detail, short_row ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_WRAP);
            lv_obj_set_height(s->detail, rect.height - y - 6);
        }
    } else {
        metric_payload_t *m = w->data.metric;
        bool machine = config->type == CURIO_WIDGET_MACHINE;
        bool narrow = config->cols == 1;
        int top = machine ? 0 : config->rows == 1 ? 0 : (rect.height - (narrow ? 78 : 61)) / 2;
        lv_obj_t *title = label(w->box, narrow ? 8 : 10, top + 6, narrow ? rect.width - 16 : machine ? width : width / 2 - 8,
            &lv_font_montserrat_14, muted);
        text(title, config->title);
        if (machine) {
            for (size_t i = 0; i < 2; i++) {
                int y = 28 + i * (rect.height - 50) / 2;
                text(label(w->box, 10, y + 6, 60, &lv_font_montserrat_14, muted), i ? "RAM" : "CPU");
                m->value[i] = label(w->box, 72, y, width - 62, &lv_font_montserrat_26, fg);
                lv_obj_set_style_text_align(m->value[i], LV_TEXT_ALIGN_RIGHT, 0);
            }
            m->detail = label(w->box, 10, rect.height - 22, width, &lv_font_montserrat_14, muted);
        } else {
            m->value[0] = label(w->box, narrow ? 8 : rect.width / 2, top + (narrow ? 25 : 3),
                narrow ? rect.width - 16 : rect.width / 2 - 10, &lv_font_montserrat_26, fg);
            if (!narrow) lv_obj_set_style_text_align(m->value[0], LV_TEXT_ALIGN_RIGHT, 0);
            if (!narrow || config->rows > 1)
                m->detail = label(w->box, narrow ? 8 : 10, top + (narrow ? 58 : 40), narrow ? rect.width - 16 : width, &lv_font_montserrat_14, muted);
        }
    }
    return w;
}
static void widget_destroy(widget_t *w)
{
    lv_obj_delete(w->box);
    free(w->data.payload);
    free(w);
}
static const char *source_status(const curio_host_t *h, const curio_source_t *s, uint64_t now)
{
    if (!h) return "Waiting";
    if (curio_host_health(h, now) == CURIO_HEALTH_OFFLINE) return "Offline";
    if (!s) return "Not reported";
    curio_health_t health = curio_source_health(s, now);
    if (health == CURIO_HEALTH_STALE) return "Stale";
    if (s->status == CURIO_SOURCE_ERROR) return "Collection error";
    if (s->status == CURIO_SOURCE_UNAVAILABLE) return "Unavailable";
    if (s->status == CURIO_SOURCE_WARMING_UP) return "Warming up";
    return NULL;
}
static void metric_text(char *out, size_t size, const char *host_id, const char *source_id, const char *key, const char *unit, uint64_t now)
{
    const curio_host_t *h = curio_core_host(&curio_app.core, host_id);
    const curio_source_t *s = h ? curio_host_source(h, source_id) : NULL;
    const char *problem = source_status(h, s, now);
    const curio_metric_t *m = s && s->kind == CURIO_SOURCE_METRICS ? curio_source_metric(s, key) : NULL;
    if (!problem && s->kind != CURIO_SOURCE_METRICS) problem = "Type mismatch";
    if (!problem && !m) problem = "Not reported";
    if (!problem && unit[0] && strcmp(unit, m->unit)) problem = "Type mismatch";
    if (!problem && !m->has_value) problem = "Unavailable";
    if (!problem && curio_metric_age(m, now) > s->max_age_ms) problem = "Stale";
    if (problem) snprintf(out, size, "%s", problem);
    else curio_format_metric(out, size, m->value, m->unit);
}
static bool host_selected(const widget_t *w, const char *id)
{
    if (w->config.all_hosts) return true;
    for (size_t i = 0; i < w->config.host_count; i++) if (!strcmp(w->config.hosts[i], id)) return true;
    return false;
}
typedef struct {
    const curio_host_t *host;
    const curio_agent_t *agent;
} agent_row_t;

static int compare_rows(const void *a, const void *b)
{
    const agent_row_t *x = a, *y = b;
    int state = (int)x->agent->state - (int)y->agent->state;
    if (state) return state;
    int host = strcmp(x->host->host_id, y->host->host_id);
    return host ? host : strcmp(x->agent->id, y->agent->id);
}
static void widget_project(widget_t *w, uint64_t now, bool touching)
{
    char value[160];
    if (w->config.type == CURIO_WIDGET_CLOCK) {
        curio_clock_format(curio_clock_now(), w->config.utc_offset_minutes, w->config.seconds, &w->data.clock->text);
        return;
    }
    if (w->config.type == CURIO_WIDGET_STATUS) {
        status_payload_t *view = w->data.status;
        const curio_host_t *h = curio_core_host(&curio_app.core, w->config.host_id);
        const curio_source_t *s = curio_host_source(h, w->config.source_id);
        const char *problem = source_status(h, s, now);
        if (!problem && s->kind != CURIO_SOURCE_STATUS) problem = "Type mismatch";
        if (!problem && !s->state) problem = "Unavailable";
        snprintf(view->value_text, sizeof(view->value_text), "%s", problem ? problem : s->state->text);
        snprintf(view->detail_text, sizeof(view->detail_text), "%s", problem ? "" : s->state->detail);
        view->color = problem ? muted : s->state->level == CURIO_STATUS_OK ? 0x80C9B1 :
            s->state->level == CURIO_STATUS_WARNING ? 0xE8BD79 : s->state->level == CURIO_STATUS_ERROR ? 0xEF9090 : fg;
        return;
    }
    if (w->config.type != CURIO_WIDGET_AGENTS) {
        bool machine = w->config.type == CURIO_WIDGET_MACHINE;
        metric_text(value, sizeof(value), w->config.host_id, machine ? "system.cpu" : w->config.source_id,
                    machine ? "usage_pct" : w->config.key, machine ? "percent" : w->config.unit, now);
        snprintf(w->data.metric->values[0], sizeof(w->data.metric->values[0]), "%s", value);
        if (!machine) {
            const curio_host_t *h = curio_core_host(&curio_app.core, w->config.host_id);
            snprintf(w->data.metric->detail_text, sizeof(w->data.metric->detail_text), "%s", h ? h->host_name : w->config.host_id);
        }
        if (machine) {
            snprintf(w->data.metric->detail_text, sizeof(w->data.metric->detail_text), "%s", w->config.host_id);
            metric_text(value, sizeof(value), w->config.host_id, "system.memory", "usage_pct", "percent", now);
            snprintf(w->data.metric->values[1], sizeof(w->data.metric->values[1]), "%s", value);
        }
        return;
    }
    agent_row_t rows[CURIO_MAX_HOSTS * CURIO_MAX_AGENTS];
    size_t count = 0;
    char missing[96] = "";
    for (size_t i = 0; i < CURIO_MAX_HOSTS; i++) {
        const curio_host_t *h = &curio_app.core.hosts[i];
        if (!h->occupied || !host_selected(w, h->host_id)) continue;
        const curio_source_t *s = curio_host_source(h, w->config.source_id);
        const char *problem = source_status(h, s, now);
        if (!problem && s && s->kind != CURIO_SOURCE_AGENTS) problem = "Type mismatch";
        if (problem && !missing[0]) snprintf(missing, sizeof(missing), "%s: %s", h->host_name, problem);
        if (!s || s->kind != CURIO_SOURCE_AGENTS) continue;
        for (size_t j = 0; j < s->agent_count; j++) {
            const curio_agent_t *a = &s->agents[j];
            if ((w->filter == CURIO_FILTER_ATTENTION && a->state != CURIO_STATE_BLOCKED) ||
                (w->filter == CURIO_FILTER_WORKING && a->state != CURIO_STATE_WORKING) ||
                (w->config.project[0] && strcmp(w->config.project, a->project)) ||
                (w->config.agent[0] && strcmp(w->config.agent, a->id))) continue;
            rows[count++] = (agent_row_t){h, a};
        }
    }
    if (!w->config.all_hosts) for (size_t i = 0; i < w->config.host_count; i++) {
        if (!curio_core_host(&curio_app.core, w->config.hosts[i]) && !missing[0]) snprintf(missing, sizeof(missing), "%s: Waiting", w->config.hosts[i]);
    }
    qsort(rows, count, sizeof(rows[0]), compare_rows);
    snprintf(w->data.agents->summary, sizeof(w->data.agents->summary), "%s", missing);
    w->count = count;
    size_t visible = w->data.agents->row_count;
    size_t pages = count ? (count + visible - 1) / visible : 1;
    if (w->pages != pages || !w->config.auto_page || pages == 1 || touching)
        w->data.agents->next_page_ms = now + 10000;
    w->pages = pages;
    if (w->page >= w->pages) w->page = w->pages - 1;
    if (notification_active || settings_mask) w->data.agents->next_page_ms = now + 10000;
    if (!notification_active && !settings_mask && w->config.auto_page && w->pages > 1 && now >= w->data.agents->next_page_ms) {
        w->page = (w->page + 1) % w->pages;
        w->data.agents->next_page_ms = now + 10000;
    }
    for (size_t i = 0; i < visible; i++) {
        agent_view_row_t *view = &w->data.agents->rows[i];
        size_t index = w->page * visible + i;
        if (index >= count) {
            snprintf(view->name_text, sizeof(view->name_text), "%s", !i && !missing[0] ? "No matching agents" : "");
            view->detail_text[0] = 0;
            view->state_text[0] = 0;
            view->color = muted;
            continue;
        }
        const agent_row_t *row = &rows[index];
        const char *problem = source_status(row->host, curio_host_source(row->host, w->config.source_id), now);
        const curio_agent_t *a = row->agent;
        const char *name = a->brand[0] && strcmp(a->name, a->brand) ? a->name : a->project[0] ? a->project : a->name;
        const char *brand = a->brand[0] ? a->brand : a->name;
        snprintf(view->name_text, sizeof(view->name_text), "%s", name);
        snprintf(view->state_text, sizeof(view->state_text), "%s", problem ? problem : curio_state_name(a->state));
        view->color = problem || a->state == CURIO_STATE_BLOCKED ? 0xE8BD79 :
            a->state == CURIO_STATE_WORKING ? 0x80C9B1 : muted;
        snprintf(view->detail_text, sizeof(view->detail_text), "%s | %s%s%s", brand, row->host->host_name,
            a->project[0] && strcmp(name, a->project) ? " | " : "", a->project[0] && strcmp(name, a->project) ? a->project : "");
    }
}
static void widget_update(widget_t *w)
{
    if (w->config.type == CURIO_WIDGET_AGENTS) {
        agents_payload_t *a = w->data.agents;
        int available = curio_widget_rect(&w->config).width - 20;
        for (size_t i = 0; i < a->row_count; i++) {
            agent_view_row_t *row = &a->rows[i];
            int width = strlen(row->state_text) > 10 ? 170 : 120;
            lv_obj_set_width(row->name, available - width - 10);
            lv_obj_set_x(row->state, available + 10 - width);
            lv_obj_set_width(row->state, width);
            lv_obj_set_style_text_color(row->state, lv_color_hex(row->color), 0);
            text(row->name, row->name_text);
            text(row->state, row->state_text);
            text(row->detail, row->detail_text);
        }
        text(a->status, a->summary);
        char caption[64];
        snprintf(caption, sizeof(caption), "%u agents | %u / %u", (unsigned)w->count, (unsigned)w->page + 1, (unsigned)w->pages);
        text(a->footer, caption);
        for (size_t i = 0; i < 3; i++) lv_obj_set_style_bg_color(a->filters[i], lv_color_hex(i == w->filter ? 0x26584F : 0x243243), 0);
    } else if (w->config.type == CURIO_WIDGET_CLOCK) {
        clock_payload_t *c = w->data.clock;
        text(c->time, c->text.time);
        if (c->date) text(c->date, c->text.date);
    } else if (w->config.type == CURIO_WIDGET_STATUS) {
        status_payload_t *s = w->data.status;
        lv_point_t size;
        lv_text_get_size(&size, s->value_text, &lv_font_montserrat_26, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_set_style_text_font(s->value, size.x > lv_obj_get_style_width(s->value, 0) ? &lv_font_montserrat_18 : &lv_font_montserrat_26, 0);
        lv_obj_set_style_text_color(s->value, lv_color_hex(s->color), 0);
        text(s->value, s->value_text);
        if (s->detail) text(s->detail, s->detail_text);
    } else {
        size_t count = w->config.type == CURIO_WIDGET_MACHINE ? 2 : 1;
        for (size_t i = 0; i < count; i++) {
            lv_obj_t *value = w->data.metric->value[i];
            const char *caption = w->data.metric->values[i];
            bool number = (caption[0] >= '0' && caption[0] <= '9') || caption[0] == '-';
            const lv_font_t *font = number ? &lv_font_montserrat_26 : &lv_font_montserrat_18;
            lv_point_t size;
            lv_text_get_size(&size, caption, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (size.x > lv_obj_get_style_width(value, 0)) font = &lv_font_montserrat_14;
            lv_obj_set_style_text_font(value, font, 0);
            bool tiny = w->config.type == CURIO_WIDGET_METRIC && w->config.cols == 1 && w->config.rows == 1;
            lv_label_set_long_mode(value, tiny && !number ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_height(value, tiny ? curio_widget_rect(&w->config).height - 27 : LV_SIZE_CONTENT);
            text(value, caption);
        }
        if (w->data.metric->detail) text(w->data.metric->detail, w->data.metric->detail_text);
    }
}
size_t curio_ui_widget_count(void) { return atomic_load(&published_count); }
static void refresh(lv_timer_t *timer)
{
    (void)timer;
    uint64_t now = curio_now_ms();
    char settings_page_text[64] = "", settings_alerts_text[64] = "";
    bool settings_rotate = false, settings_paging = false;
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    if (curio_app.layout.page_count && (layout_revision != curio_app.layout.revision)) {
        const char *wanted = strcmp(default_page_id, curio_app.layout.default_page) ? curio_app.layout.default_page : active_page_id;
        if (wanted[0]) for (size_t i = 0; i < curio_app.layout.page_count; i++) if (!strcmp(curio_app.layout.pages[i].id, wanted)) { selected_page = i; break; }
        if (selected_page >= curio_app.layout.page_count) selected_page = 0;
        snprintf(active_page_id, sizeof(active_page_id), "%s", curio_app.layout.pages[selected_page].id);
        snprintf(default_page_id, sizeof(default_page_id), "%s", curio_app.layout.default_page);
        curio_page_config_t *current = curio_alloc(sizeof(*current));
        if (current) *current = curio_app.layout.pages[selected_page];
        uint64_t revision = curio_app.layout.revision;
        xSemaphoreGive(curio_app.mutex);
        if (current) {
            while (widget_count) { widget_destroy(widgets[--widget_count]); atomic_store(&published_count, widget_count); }
            const curio_page_config_t *p = current;
            text(page_title, p->title);
            for (size_t i = 0; i < p->widget_count; i++) {
                widget_t *w = widget_create(lv_screen_active(), &p->widgets[i]);
                if (w) { widgets[widget_count++] = w; atomic_store(&published_count, widget_count); }
            }
            layout_revision = revision;
            free(current);
        }
        xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    }
    char connection_text[96];
    snprintf(connection_text, sizeof(connection_text), "%s", curio_app.setup_active ? "Wi-Fi setup active" : !curio_app.configured ? "Setup required" : curio_app.network_error[0] ? curio_app.network_error : !curio_app.wifi_connected ? "Wi-Fi disconnected" : !curio_app.https_ready ? "HTTPS unavailable" : "Connected");
    bool show_setup = curio_app.setup_active && !curio_app.configured && !setup_shown;
    char setup_text[128];
    if (curio_app.setup_active)
        snprintf(setup_text, sizeof(setup_text), "Network\n%s\n\nPassword\n%s",
            curio_app.setup_ssid, curio_app.setup_password);
    else snprintf(setup_text, sizeof(setup_text), "%s", curio_app.network_error[0] ? curio_app.network_error : "Starting...");
    bool wifi_connected = curio_app.wifi_connected;
    bool network_ready = curio_app.configured && wifi_connected && curio_app.https_ready && !curio_app.network_error[0];
    bool network_error = curio_app.configured && (curio_app.network_error[0] || (wifi_connected && !curio_app.https_ready));
    char ip[16];
    snprintf(ip, sizeof(ip), "%s", curio_app.ip);
    uint64_t device_revision = curio_app.device.revision;
    int brightness = curio_app.device.brightness;
    unsigned volume = curio_app.alert_config.volume;
    unsigned vibration_strength = curio_app.alert_config.vibration_strength;
    bool flash_enabled = curio_app.alert_config.flash_enabled;
    curio_battery_t battery = curio_app.battery;
    curio_alert_notification_t notification = {0};
    bool has_notification = false;
    uint64_t remaining_ms = 0;
    uint32_t alert_revision = curio_app.alerts ? curio_alerts_revision(curio_app.alerts) : 0;
    if (curio_app.alerts) {
        curio_alerts_tick(curio_app.alerts, &curio_app.core, now, curio_app.wifi_connected);
        const curio_alert_notification_t *current = curio_alerts_current(curio_app.alerts, now);
        if (current) {
            notification = *current;
            has_notification = true;
            remaining_ms = curio_alerts_remaining_ms(curio_app.alerts, now);
        }
    }
    if (has_notification || now >= preview_until) preview_active = false;
    if (preview_active) {
        remaining_ms = preview_until - now;
        notification = (curio_alert_notification_t){.id = UINT32_MAX, .sound = true, .volume = volume,
            .vibrate = true, .vibration_strength = vibration_strength, .flash = true};
        snprintf(notification.title, sizeof(notification.title), "Test alert");
        snprintf(notification.body, sizeof(notification.body), "Sample notification");
    }
    notification_active = has_notification || preview_active;
    if (settings_mask) {
        settings_rotate = curio_app.device.auto_rotate;
        settings_page_id[0] = 0;
        settings_paging = false;
        if (selected_page < curio_app.layout.page_count) {
            const curio_page_config_t *p = &curio_app.layout.pages[selected_page];
            size_t agents = 0, paging = 0;
            for (size_t i = 0; i < p->widget_count; i++) if (p->widgets[i].type == CURIO_WIDGET_AGENTS) {
                agents++;
                paging += p->widgets[i].auto_page;
            }
            if (agents) snprintf(settings_page_id, sizeof(settings_page_id), "%s", p->id);
            settings_paging = agents && paging == agents;
            snprintf(settings_page_text, sizeof(settings_page_text), "%s%s", p->title,
                paging && paging < agents ? " | Mixed" : "");
        }
        size_t enabled = 0;
        for (size_t i = 0; i < curio_app.alert_config.rule_count; i++) enabled += curio_app.alert_config.rules[i].enabled;
        snprintf(settings_alerts_text, sizeof(settings_alerts_text), "%u / %u enabled",
            (unsigned)enabled, (unsigned)curio_app.alert_config.rule_count);
    }
    for (size_t i = 0; i < widget_count; i++) {
        bool touching = false;
        curio_widget_rect_t rect = curio_widget_rect(&widgets[i]->config);
        for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) {
            if (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER || lv_indev_get_state(input) != LV_INDEV_STATE_PRESSED) continue;
            lv_point_t point;
            lv_indev_get_point(input, &point);
            touching |= point.x >= rect.x && point.x < rect.x + rect.width && point.y >= rect.y && point.y < rect.y + rect.height;
        }
        widget_project(widgets[i], now, touching);
    }
    xSemaphoreGive(curio_app.mutex);
    curio_feedback_set_flash_enabled(flash_enabled);
    if (has_notification && dragging_slider) {
        if (dragging_slider == brightness_slider) bsp_display_brightness_set(brightness);
        dragging_slider = NULL;
    }
    static uint64_t applied_device_revision = UINT64_MAX;
    if (device_revision != applied_device_revision && (!dragging_slider || dragging_slider != brightness_slider)) {
        bsp_display_brightness_set(brightness);
        applied_device_revision = device_revision;
    }
    if (wifi_connected) lv_obj_remove_state(wifi_button, LV_STATE_CHECKED);
    else lv_obj_add_state(wifi_button, LV_STATE_CHECKED);
    lv_color_t wifi_color = lv_color_hex(network_ready ? 0x80C9B1 : network_error ? 0xE8BD79 : muted);
    lv_obj_set_style_text_color(wifi_button, wifi_color, 0);
    lv_obj_set_style_text_color(wifi_button, wifi_color, LV_STATE_CHECKED);
    if (network_error) lv_obj_remove_flag(wifi_warning, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(wifi_warning, LV_OBJ_FLAG_HIDDEN);
    curio_feedback_status_t feedback = curio_feedback_status();
    mute_button_update(sound_button, feedback.audio.muted);
    mute_button_update(vibration_button, feedback.vibration.muted);
    lv_label_set_text(sound_icon, feedback.audio.muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
    if (network_popup) {
        if (now >= network_until || settings_mask || notification_active) {
            lv_obj_delete(network_popup);
            network_popup = NULL;
        } else {
            text(network_status, connection_text);
            text(network_ip, wifi_connected && ip[0] ? ip : "No IP address");
            lv_obj_move_foreground(network_popup);
        }
    }
    battery_update(&battery, now);
    for (size_t i = 0; i < widget_count; i++) widget_update(widgets[i]);
    if (settings_mask) {
        if (dragging_slider != brightness_slider) {
            lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);
            slider_caption(brightness_slider, brightness);
        }
        if (dragging_slider != volume_slider) {
            lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);
            slider_caption(volume_slider, volume);
        }
        if (dragging_slider != vibration_slider) {
            lv_slider_set_value(vibration_slider, vibration_strength, LV_ANIM_OFF);
            slider_caption(vibration_slider, vibration_strength);
        }
        text(settings_page, settings_page_text);
        text(settings_alerts, settings_alerts_text);
        if (settings_rotate) lv_obj_add_state(rotate_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(rotate_switch, LV_STATE_CHECKED);
        if (settings_paging) lv_obj_add_state(paging_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(paging_switch, LV_STATE_CHECKED);
        if (flash_enabled) lv_obj_add_state(flash_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(flash_switch, LV_STATE_CHECKED);
        if (settings_page_id[0]) lv_obj_remove_state(paging_switch, LV_STATE_DISABLED);
        else lv_obj_add_state(paging_switch, LV_STATE_DISABLED);
        lv_obj_move_foreground(settings_mask);
    }
    alert_details_update();
    notification_show(notification_active ? &notification : NULL, preview_active ? UINT32_MAX : alert_revision, remaining_ms);
    if (show_setup) setup_open(NULL);
    if (setup_mask) {
        if (strcmp(lv_label_get_text(setup_details), setup_text)) lv_label_set_text(setup_details, setup_text);
        lv_obj_move_foreground(setup_mask);
    }
}
void curio_ui_start(void)
{
    bsp_display_config_t display_config = BSP_DISPLAY_DEFAULT_CONFIG();
    display_config.rotation = curio_app.device.rotation;
    lv_display_t *display = bsp_display_start_with_config(&display_config);
    configASSERT(display);
    configASSERT(bsp_display_lock(-1));
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101822), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(fg), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_18, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = header_button(screen, 12, 142, switch_page);
    page_title = label(header, 20, 10, 116, &lv_font_montserrat_18, fg);
    wifi_button = header_icon(screen, 162, LV_SYMBOL_WIFI, network_click);
    wifi_warning = label(wifi_button, 34, 3, 10, &lv_font_montserrat_14, 0xE8BD79);
    text(wifi_warning, "!");
    sound_button = header_icon(screen, 212, LV_SYMBOL_VOLUME_MAX, quick_mute);
    sound_icon = lv_obj_get_child(sound_button, 0);
    vibration_button = header_icon(screen, 262, "", quick_mute);
    battery_label = label(screen, 314, 24, 98, &lv_font_montserrat_14, muted);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *gear = header_button(screen, 412, 56, settings_open);
    lv_obj_t *gear_icon = label(gear, 0, 0, 32, &lv_font_montserrat_18, fg);
    lv_label_set_text(gear_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_align(gear_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(gear_icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *mute_buttons[] = {sound_button, vibration_button};
    for (size_t i = 0; i < 2; i++) {
        lv_obj_set_style_text_color(mute_buttons[i], lv_color_hex(muted), 0);
        lv_obj_set_style_text_color(mute_buttons[i], lv_color_hex(0xE8BD79), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(mute_buttons[i], lv_color_hex(0x3C3326), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(mute_buttons[i], LV_OPA_COVER, LV_STATE_CHECKED);
    }
    for (size_t i = 0; i < curio_app.layout.page_count; i++) if (!strcmp(curio_app.layout.default_page, curio_app.layout.pages[i].id)) selected_page = i;
    refresh(NULL);
    lv_timer_create(refresh, 1000, NULL);
    lv_display_add_event_cb(display, first_refresh, LV_EVENT_REFR_READY, NULL);
    lv_display_add_event_cb(display, first_refresh, LV_EVENT_FLUSH_WAIT_FINISH, NULL);
    bsp_display_unlock();
    curio_screen_start();
}
