/**
 * @file imu_dashboard.c
 * @brief IMU data visualization tab.
 *
 * Displays real-time roll and pitch orientation from the QMI8658C
 * using symmetrical bar widgets and numeric labels.  An on/off
 * switch controls the IMU service and its power lock.
 */

#include "include/ui/imu_dashboard/imu_dashboard.h"
#include "include/services/imu_service.h"
#include <stdio.h>

/* ── Static Widget References ─────────────────────────────────── */
static lv_obj_t    *s_imu_switch      = NULL;
static lv_obj_t    *s_roll_bar        = NULL;
static lv_obj_t    *s_pitch_bar       = NULL;
static lv_obj_t    *s_roll_val_label  = NULL;
static lv_obj_t    *s_pitch_val_label = NULL;
static lv_timer_t  *s_dashboard_timer = NULL;

/* ═══════════════════════════════════════════════════════════════
 *  Event Callbacks
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Enable/disable the IMU service when the switch is toggled. */
static void imu_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool pressed = lv_obj_has_state(sw, LV_STATE_CHECKED);
    imu_service_set_enable(pressed);
}

/**
 * @brief LVGL timer callback – refresh bar values and labels at 20 Hz.
 *
 * Uses integer formatting with manual decimal extraction to avoid
 * floating-point printf issues on this platform.
 */
static void imu_dashboard_update_cb(lv_timer_t *timer)
{
    float roll = 0, pitch = 0;
    bool active = imu_service_get_orientation(&roll, &pitch);

    if (active) {
        lv_bar_set_value(s_roll_bar,  (int32_t)roll,  LV_ANIM_ON);
        lv_bar_set_value(s_pitch_bar, (int32_t)pitch, LV_ANIM_ON);

        /* Manual decimal formatting (avoids trailing 'f' on some toolchains) */
        int r_int = (int)roll;
        int r_dec = abs((int)(roll * 10) % 10);
        int p_int = (int)pitch;
        int p_dec = abs((int)(pitch * 10) % 10);

        lv_label_set_text_fmt(s_roll_val_label,  "Roll: %s%d.%d°",
                              (roll  < 0 && r_int == 0) ? "-" : "", r_int, r_dec);
        lv_label_set_text_fmt(s_pitch_val_label, "Pitch: %s%d.%d°",
                              (pitch < 0 && p_int == 0) ? "-" : "", p_int, p_dec);
    } else {
        lv_label_set_text(s_roll_val_label,  "Roll: OFF");
        lv_label_set_text(s_pitch_val_label, "Pitch: OFF");
        lv_bar_set_value(s_roll_bar,  0, LV_ANIM_OFF);
        lv_bar_set_value(s_pitch_bar, 0, LV_ANIM_OFF);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Tab Construction
 * ═══════════════════════════════════════════════════════════════ */

void imu_dashboard(lv_obj_t *tab)
{
    printf("IMU Dashboard tab initialized\n");

    /* Allow gesture events to bubble up for tab switching */
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(tab, 15, 0);

    /* ── Header Row (Label + Switch) ── */
    lv_obj_t *header_cont = lv_obj_create(tab);
    lv_obj_set_width(header_cont, lv_pct(90));
    lv_obj_set_height(header_cont, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(header_cont, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_opa(header_cont, 40, 0);
    lv_obj_set_style_radius(header_cont, 8, 0);
    lv_obj_set_style_border_width(header_cont, 0, 0);
    lv_obj_set_style_pad_all(header_cont, 8, 0);

    lv_obj_t *sw_label = lv_label_create(header_cont);
    lv_label_set_text(sw_label, "System IMU");
    lv_obj_set_style_text_font(sw_label, &lv_font_montserrat_14, 0);

    s_imu_switch = lv_switch_create(header_cont);
    lv_obj_set_size(s_imu_switch, 60, 32);
    lv_obj_add_event_cb(s_imu_switch, imu_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Roll Axis Bar ── */
    s_roll_val_label = lv_label_create(tab);
    lv_label_set_text(s_roll_val_label, "Roll: 0.0°");
    lv_obj_set_style_text_font(s_roll_val_label, &lv_font_montserrat_14, 0);

    s_roll_bar = lv_bar_create(tab);
    lv_obj_clear_flag(s_roll_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_roll_bar, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(s_roll_bar, 210, 24);
    lv_bar_set_range(s_roll_bar, -180, 180);
    lv_bar_set_mode(s_roll_bar, LV_BAR_MODE_SYMMETRICAL);
    lv_obj_set_style_bg_color(s_roll_bar, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);

    /* ── Pitch Axis Bar ── */
    s_pitch_val_label = lv_label_create(tab);
    lv_label_set_text(s_pitch_val_label, "Pitch: 0.0°");
    lv_obj_set_style_text_font(s_pitch_val_label, &lv_font_montserrat_14, 0);

    s_pitch_bar = lv_bar_create(tab);
    lv_obj_clear_flag(s_pitch_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_pitch_bar, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(s_pitch_bar, 210, 24);
    lv_bar_set_range(s_pitch_bar, -90, 90);
    lv_bar_set_mode(s_pitch_bar, LV_BAR_MODE_SYMMETRICAL);
    lv_obj_set_style_bg_color(s_pitch_bar, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);

    /* ── Periodic Update Timer (20 Hz) ── */
    s_dashboard_timer = lv_timer_create(imu_dashboard_update_cb, 50, NULL);
}
