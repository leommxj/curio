#include "curio_app.h"
#include "curio_screen.h"
#include "curio_layout.h"
#include "curio_widget_view.h"
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

static atomic_size_t published_count;
static curio_widget_t *widgets[CURIO_LAYOUT_MAX_WIDGETS];
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
static void refresh(lv_timer_t *timer);
static void invalidate(void) { refresh(NULL); }

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
        notification_title = curio_view_label(content, 0, 0, 408, &lv_font_montserrat_32, CURIO_COLOR_MUTED);
        notification_body = curio_view_label(content, 0, 0, 408, &lv_font_montserrat_48, CURIO_COLOR_FG);
        notification_detail = curio_view_label(content, 0, 0, 408, &lv_font_montserrat_26, CURIO_COLOR_MUTED);
        lv_label_set_long_mode(notification_title, LV_LABEL_LONG_WRAP);
        lv_label_set_long_mode(notification_body, LV_LABEL_LONG_WRAP);
        lv_label_set_long_mode(notification_detail, LV_LABEL_LONG_WRAP);
        lv_obj_t *close = curio_view_button(panel, "Close", 0, 0, 408, notification_close, NULL);
        lv_obj_set_height(close, 56);
        lv_obj_set_style_text_font(lv_obj_get_child(close, 0), &lv_font_montserrat_26, 0);
        notification_countdown = curio_view_label(close, 0, 0, 88, &lv_font_montserrat_26, CURIO_COLOR_MUTED);
        lv_obj_align(notification_countdown, LV_ALIGN_RIGHT_MID, -14, 0);
        lv_obj_set_style_text_align(notification_countdown, LV_TEXT_ALIGN_RIGHT, 0);
    }
    notification_id = notification->id;
    notification_revision = revision;
    curio_view_text(notification_title, notification->title);
    curio_view_text(notification_body, notification->body);
    curio_view_text(notification_detail, notification->detail);
    if (notification->detail[0]) lv_obj_remove_flag(notification_detail, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(notification_detail, LV_OBJ_FLAG_HIDDEN);
    if (is_new) lv_obj_scroll_to_y(lv_obj_get_parent(notification_body), 0, LV_ANIM_OFF);
    if (remaining_ms) {
        char countdown[24];
        snprintf(countdown, sizeof(countdown), "%llus", (unsigned long long)((remaining_ms + 999) / 1000));
        curio_view_text(notification_countdown, countdown);
        lv_obj_remove_flag(notification_countdown, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(notification_countdown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notification_mask);
    if (is_new) curio_feedback_play(notification->sound ? notification->volume : 0,
                                    notification->vibrate ? notification->vibration_strength : 0,
                                    notification->flash);
}


static void battery_update(const curio_battery_t *battery, uint64_t now)
{
    curio_battery_state_t state = curio_battery_state(battery, now);
    char caption[32];
    uint32_t color = CURIO_COLOR_MUTED;
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
    network_status = curio_view_label(network_popup, 16, 16, 400, &lv_font_montserrat_18, CURIO_COLOR_FG);
    lv_label_set_long_mode(network_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(network_status, 48);
    network_ip = curio_view_label(network_popup, 16, 78, 400, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
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
    lv_obj_t *icon = curio_view_label(obj, 0, 0, 44, &lv_font_montserrat_18, CURIO_COLOR_MUTED);
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
    curio_view_text(curio_view_label(sheet, 24, 24, 300, &lv_font_montserrat_26, CURIO_COLOR_FG), title);
    curio_view_button(sheet, action, 364, 16, 92, close, NULL);
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
    setup_details = curio_view_label(setup_mask, 32, 110, 416, &lv_font_montserrat_26, CURIO_COLOR_FG);
    lv_label_set_long_mode(setup_details, LV_LABEL_LONG_WRAP);
    curio_view_text(setup_details, "Starting...");
    curio_view_text(curio_view_label(setup_mask, 32, 380, 416, &lv_font_montserrat_26, CURIO_COLOR_MUTED), "http://192.168.4.1");
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
    const char *title = rule->type->title;
    curio_view_text(curio_view_label(card, 0, 0, 320, &lv_font_montserrat_18, CURIO_COLOR_FG), title);
    lv_obj_t *enabled = curio_view_label(card, 352, 0, 72, &lv_font_montserrat_14, rule->enabled ? 0x80C9B1 : CURIO_COLOR_MUTED);
    lv_obj_set_style_text_align(enabled, LV_TEXT_ALIGN_RIGHT, 0);
    curio_view_text(enabled, rule->enabled ? "Enabled" : "Disabled");
    lv_obj_t *id = curio_view_label(card, 0, 28, 424, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
    lv_label_set_long_mode(id, LV_LABEL_LONG_WRAP);
    curio_view_text(id, rule->id);
    lv_obj_update_layout(card);

    char condition[320], duration[32], sound[32], vibration[32], body[640];
    rule->type->describe(rule->options, condition, sizeof(condition));
    if (rule->display_s) snprintf(duration, sizeof(duration), "%us", rule->display_s);
    else snprintf(duration, sizeof(duration), "Until closed");
    if (rule->sound) snprintf(sound, sizeof(sound), "On (%u%%)", config->volume);
    else snprintf(sound, sizeof(sound), "Off");
    if (rule->vibrate) snprintf(vibration, sizeof(vibration), "On (%u%%)", config->vibration_strength);
    else snprintf(vibration, sizeof(vibration), "Off");
    snprintf(body, sizeof(body), "%s\nDisplay: %s\nSound: %s\nVibration: %s\nFlash: %s",
        condition, duration, sound, vibration,
        !rule->flash ? "Off" : config->flash_enabled ? "On" : "Off (global)");
    lv_obj_t *details = curio_view_label(card, 0, 40 + lv_obj_get_height(id), 424, &lv_font_montserrat_14, CURIO_COLOR_FG);
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
        curio_view_text(settings_error, "Out of memory");
        return;
    }
    xSemaphoreTake(curio_app.mutex, portMAX_DELAY);
    bool copied = curio_alert_config_clone(config, &curio_app.alert_config);
    xSemaphoreGive(curio_app.mutex);
    if (!copied) { free(config); return; }
    int32_t scroll = lv_obj_get_scroll_y(alert_details_list);
    lv_obj_clean(alert_details_list);
    if (!config->rule_count) curio_view_text(curio_view_label(alert_details_list, 12, 12, 424, &lv_font_montserrat_18, CURIO_COLOR_MUTED), "No rules");
    for (size_t i = 0; i < config->rule_count; i++) alert_rule_card(&config->rules[i], config);
    lv_obj_update_layout(alert_details_list);
    lv_obj_scroll_to_y(alert_details_list, scroll, LV_ANIM_OFF);
    alert_details_revision = config->revision;
    curio_alert_config_free(config);
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
    curio_view_text(settings_error, error == ESP_OK ? "" : esp_err_to_name(error));
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
    curio_view_text(curio_view_label(settings_mask, 24, y + 9, 330, &lv_font_montserrat_18, CURIO_COLOR_FG), caption);
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
    curio_view_text(target == brightness_slider ? brightness_value : target == volume_slider ? volume_value : vibration_value, caption);
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
    curio_view_text(settings_error, error == ESP_OK ? "" : esp_err_to_name(error));
}
static lv_obj_t *settings_slider(const char *caption, int y, int minimum, lv_obj_t **value_label)
{
    curio_view_text(curio_view_label(settings_mask, 24, y, 240, &lv_font_montserrat_18, CURIO_COLOR_FG), caption);
    lv_obj_t *value = curio_view_label(settings_mask, 314, y, 138, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
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
    curio_view_button(settings_mask, "Wi-Fi", 264, 16, 88, setup_open, NULL);
    rotate_switch = settings_toggle("Auto rotate", 72);
    paging_switch = settings_toggle("Agent auto-page", 120);
    settings_page = curio_view_label(settings_mask, 24, 162, 420, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
    flash_switch = settings_toggle("Flash", 184);
    brightness_slider = settings_slider("Brightness", 234, 5, &brightness_value);
    volume_slider = settings_slider("Volume", 290, 0, &volume_value);
    vibration_slider = settings_slider("Vibration", 346, 0, &vibration_value);
    lv_obj_t *alerts_button = curio_view_button(settings_mask, "", 24, 404, 228, alert_details_open, NULL);
    lv_obj_set_height(alerts_button, 50);
    curio_view_text(curio_view_label(alerts_button, 12, 4, 164, &lv_font_montserrat_18, CURIO_COLOR_FG), "Alerts");
    settings_alerts = curio_view_label(alerts_button, 12, 28, 180, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
    lv_label_set_text(curio_view_label(alerts_button, 198, 16, 18, &lv_font_montserrat_14, CURIO_COLOR_MUTED), LV_SYMBOL_RIGHT);
    curio_view_button(settings_mask, "Test alert", 276, 408, 180, preview_start, NULL);
    settings_error = curio_view_label(settings_mask, 24, 458, 432, &lv_font_montserrat_14, 0xE8BD79);
    curio_view_text(settings_error, "");
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
        if (current && !curio_page_clone(current, &curio_app.layout.pages[selected_page])) { free(current); current = NULL; }
        uint64_t revision = curio_app.layout.revision;
        xSemaphoreGive(curio_app.mutex);
        if (current) {
            while (widget_count) { curio_widget_destroy(widgets[--widget_count]); atomic_store(&published_count, widget_count); }
            const curio_page_config_t *p = current;
            curio_view_text(page_title, p->title);
            for (size_t i = 0; i < p->widget_count; i++) {
                curio_widget_t *w = curio_widget_create(lv_screen_active(), &p->widgets[i], invalidate, curio_now_ms);
                if (w) { widgets[widget_count++] = w; atomic_store(&published_count, widget_count); }
            }
            layout_revision = revision;
            curio_page_free(current);
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
            for (size_t i = 0; i < p->widget_count; i++) if (p->widgets[i].type->paging) {
                agents++;
                paging += p->widgets[i].type->paging(p->widgets[i].options);
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
        const curio_widget_sample_t sample = {&curio_app.core, now, curio_clock_now(), notification_active || settings_mask, touching};
        widgets[i]->view->sample(widgets[i], &sample);
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
    lv_color_t wifi_color = lv_color_hex(network_ready ? 0x80C9B1 : network_error ? 0xE8BD79 : CURIO_COLOR_MUTED);
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
            curio_view_text(network_status, connection_text);
            curio_view_text(network_ip, wifi_connected && ip[0] ? ip : "No IP address");
            lv_obj_move_foreground(network_popup);
        }
    }
    battery_update(&battery, now);
    for (size_t i = 0; i < widget_count; i++) widgets[i]->view->render(widgets[i]);
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
        curio_view_text(settings_page, settings_page_text);
        curio_view_text(settings_alerts, settings_alerts_text);
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
    lv_obj_set_style_text_color(screen, lv_color_hex(CURIO_COLOR_FG), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_18, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = header_button(screen, 12, 142, switch_page);
    page_title = curio_view_label(header, 20, 10, 116, &lv_font_montserrat_18, CURIO_COLOR_FG);
    wifi_button = header_icon(screen, 162, LV_SYMBOL_WIFI, network_click);
    wifi_warning = curio_view_label(wifi_button, 34, 3, 10, &lv_font_montserrat_14, 0xE8BD79);
    curio_view_text(wifi_warning, "!");
    sound_button = header_icon(screen, 212, LV_SYMBOL_VOLUME_MAX, quick_mute);
    sound_icon = lv_obj_get_child(sound_button, 0);
    vibration_button = header_icon(screen, 262, "", quick_mute);
    battery_label = curio_view_label(screen, 314, 24, 98, &lv_font_montserrat_14, CURIO_COLOR_MUTED);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *gear = header_button(screen, 412, 56, settings_open);
    lv_obj_t *gear_icon = curio_view_label(gear, 0, 0, 32, &lv_font_montserrat_18, CURIO_COLOR_FG);
    lv_label_set_text(gear_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_align(gear_icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(gear_icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *mute_buttons[] = {sound_button, vibration_button};
    for (size_t i = 0; i < 2; i++) {
        lv_obj_set_style_text_color(mute_buttons[i], lv_color_hex(CURIO_COLOR_MUTED), 0);
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
