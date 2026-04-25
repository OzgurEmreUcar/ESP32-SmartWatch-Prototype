/**
 * @file settings.c
 * @brief Settings screen tab – device configuration UI.
 *
 * Provides user controls for:
 *   - LCD backlight brightness (slider)
 *   - WiFi on/off toggle with async state feedback
 *   - "Always Awake" power policy toggle
 *   - RTC time configuration via hour/minute/second rollers
 */

#include "include/ui/settings/settings.h"
#include "include/ui/home/rtc_time.h"
#include "include/init/lcd_init.h"
#include "include/services/power_service.h"
#include "include/services/wifi_service.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>

/* ── Static Widget Pointers (for async UI updates) ────────────── */
static lv_obj_t *roller_h;
static lv_obj_t *roller_m;
static lv_obj_t *roller_s;
static lv_obj_t *wifi_btn;
static lv_obj_t *wifi_label;
static lv_obj_t *sleep_btn;
static lv_obj_t *sleep_label;

/* ═══════════════════════════════════════════════════════════════
 *  Async WiFi State Callbacks
 *  (called from the WiFi event handler via lv_async_call)
 * ═══════════════════════════════════════════════════════════════ */

void ui_update_wifi_connected(void *p)
{
    if (!wifi_label || !wifi_btn) return;
    lv_label_set_text(wifi_label, "WiFi: Connected");
    lv_obj_add_state(wifi_btn, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(wifi_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_style_bg_color(wifi_btn, lv_palette_main(LV_PALETTE_GREEN), LV_STATE_CHECKED);
}

void ui_update_wifi_disconnected(void *p)
{
    if (!wifi_label || !wifi_btn) return;
    lv_label_set_text(wifi_label, "WiFi: OFF");
    lv_obj_clear_state(wifi_btn, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(wifi_btn, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_color(wifi_btn, lv_palette_main(LV_PALETTE_GREY), LV_STATE_CHECKED);
}

void ui_update_wifi_connecting(void *p)
{
    if (!wifi_label || !wifi_btn) return;
    lv_label_set_text(wifi_label, "WiFi: Connecting...");
    lv_obj_add_state(wifi_btn, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(wifi_btn, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_color(wifi_btn, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_CHECKED);
}

/* ═══════════════════════════════════════════════════════════════
 *  Sleep Policy UI
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Synchronize the "Always Awake" button with current power policy. */
void ui_update_sleep_ui(void)
{
    if (!sleep_btn || !sleep_label) return;

    if (!power_is_sleep_enabled()) {
        lv_obj_add_state(sleep_btn, LV_STATE_CHECKED);
        lv_label_set_text(sleep_label, "Always Awake: ON");
        lv_obj_set_style_bg_color(sleep_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
        lv_obj_set_style_bg_color(sleep_btn, lv_palette_main(LV_PALETTE_GREEN), LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sleep_btn, LV_STATE_CHECKED);
        lv_label_set_text(sleep_label, "Always Awake: OFF");
        lv_obj_set_style_bg_color(sleep_btn, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_bg_color(sleep_btn, lv_palette_main(LV_PALETTE_GREY), LV_STATE_CHECKED);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Event Callbacks
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Toggle WiFi on/off when the button is clicked. */
static void wifi_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    /* Ignore if the touch resolved as a swipe gesture */
    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;

    bool was_on = lv_obj_has_state(btn, LV_STATE_CHECKED);

    if (!was_on) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        lv_label_set_text(wifi_label, "WiFi: Connecting...");
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_CHECKED);
        wifi_service_set_enabled(true);
        esp_wifi_start();
        esp_wifi_connect();
    } else {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
        wifi_service_set_enabled(false);
        esp_wifi_stop();
        ui_update_wifi_disconnected(NULL);
    }
}

/** @brief Toggle the "Always Awake" sleep policy. */
static void sleep_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;

    bool was_on = lv_obj_has_state(btn, LV_STATE_CHECKED);
    bool will_be_on = !was_on;

    /* Inverted: "Always Awake ON" means sleep is DISABLED */
    power_set_sleep_enabled(!will_be_on);
    ui_update_sleep_ui();
}

/** @brief Write the selected roller values to the RTC. */
static void save_btn_event_cb(lv_event_t *e)
{
    uint8_t h = lv_roller_get_selected(roller_h);
    uint8_t m = lv_roller_get_selected(roller_m);
    uint8_t s = lv_roller_get_selected(roller_s);
    rtc_set_time(h, m, s);
}

/** @brief Forward slider value to the LCD backlight driver. */
static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    set_lcd_brightness((uint8_t)lv_slider_get_value(slider));
}

/* ═══════════════════════════════════════════════════════════════
 *  Tab Construction
 * ═══════════════════════════════════════════════════════════════ */

void settings(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(tab, LV_DIR_VER);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(tab, 20, 0);
    lv_obj_set_style_pad_ver(tab, 20, 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_AUTO);

    /* ── Brightness Slider ── */
    lv_obj_t *lbl_bright = lv_label_create(tab);
    lv_label_set_text(lbl_bright, "Brightness");

    lv_obj_t *slider = lv_slider_create(tab);
    lv_obj_set_size(slider, 200, 20);
    lv_slider_set_range(slider, 10, 255);
    lv_slider_set_value(slider, 255, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── WiFi Toggle ── */
    lv_obj_t *lbl_wifi = lv_label_create(tab);
    lv_label_set_text(lbl_wifi, "Network Settings");

    wifi_btn = lv_btn_create(tab);
    lv_obj_set_size(wifi_btn, 180, 45);
    lv_obj_add_event_cb(wifi_btn, wifi_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, "WiFi: OFF");
    lv_obj_center(wifi_label);

    if (wifi_service_is_enabled()) {
        ui_update_wifi_connected(NULL);
    } else {
        ui_update_wifi_disconnected(NULL);
    }

    /* ── Sleep / Always Awake Toggle ── */
    lv_obj_t *lbl_sleep = lv_label_create(tab);
    lv_label_set_text(lbl_sleep, "Power Management");

    sleep_btn = lv_btn_create(tab);
    lv_obj_set_size(sleep_btn, 180, 45);
    lv_obj_add_event_cb(sleep_btn, sleep_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    sleep_label = lv_label_create(sleep_btn);
    lv_obj_center(sleep_label);
    ui_update_sleep_ui();

    /* ── Time Configuration Rollers ── */
    lv_obj_t *lbl_time = lv_label_create(tab);
    lv_label_set_text(lbl_time, "System Time");

    lv_obj_t *cont = lv_obj_create(tab);
    lv_obj_set_size(cont, 220, 100);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_color(cont, lv_palette_main(LV_PALETTE_GREY), 0);

    /* Build roller option strings */
    char opts_h[150] = "";
    for (int i = 0; i < 24; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d%s", i, (i == 23 ? "" : "\n"));
        strcat(opts_h, buf);
    }

    char opts_ms[300] = "";
    for (int i = 0; i < 60; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d%s", i, (i == 59 ? "" : "\n"));
        strcat(opts_ms, buf);
    }

    roller_h = lv_roller_create(cont);
    lv_roller_set_options(roller_h, opts_h, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_h, 2);
    lv_obj_set_width(roller_h, 50);

    roller_m = lv_roller_create(cont);
    lv_roller_set_options(roller_m, opts_ms, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_m, 2);
    lv_obj_set_width(roller_m, 50);

    roller_s = lv_roller_create(cont);
    lv_roller_set_options(roller_s, opts_ms, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller_s, 2);
    lv_obj_set_width(roller_s, 50);

    /* ── Save Button ── */
    lv_obj_t *btn = lv_btn_create(tab);
    lv_obj_set_size(btn, 180, 45);
    lv_obj_add_event_cb(btn, save_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Save Time");
    lv_obj_center(btn_label);
}