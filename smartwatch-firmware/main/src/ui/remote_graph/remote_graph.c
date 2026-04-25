/**
 * @file remote_graph.c
 * @brief Remote sensor data graph tab.
 *
 * Displays a live-scrolling line chart of float values received
 * from an external ESP32 via the remote_service WebSocket.
 * Includes a toggle switch and a direct swipe gesture handler on
 * the chart widget to maintain smooth tab navigation.
 */

#include "include/ui/remote_graph/remote_graph.h"
#include "include/services/remote_service.h"
#include "include/init/ui_init.h"
#include <stdio.h>

/* ── Static Widget References ─────────────────────────────────── */
static lv_obj_t          *s_remote_chart  = NULL;
static lv_chart_series_t *s_remote_ser    = NULL;
static lv_obj_t          *s_remote_label  = NULL;
static lv_timer_t        *s_remote_timer  = NULL;
static lv_obj_t          *s_remote_switch = NULL;

/* ═══════════════════════════════════════════════════════════════
 *  Event Callbacks
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Enable/disable the remote WebSocket stream. */
static void remote_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    remote_service_set_enable(enabled);
}

/** @brief LVGL timer callback – push the latest value into the chart. */
static void remote_graph_update_cb(lv_timer_t *timer)
{
    if (!remote_service_is_enabled()) {
        lv_label_set_text(s_remote_label, "Remote Data: OFF");
        return;
    }

    float val = remote_service_get_value();

    if (s_remote_chart && s_remote_ser) {
        lv_chart_set_next_value(s_remote_chart, s_remote_ser, (lv_coord_t)val);
        lv_chart_refresh(s_remote_chart);
        lv_label_set_text_fmt(s_remote_label, "Remote Value: %.2f", val);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Tab Construction
 * ═══════════════════════════════════════════════════════════════ */

void remote_graph_tab(lv_obj_t *tab, lv_obj_t *tabview)
{
    printf("Remote Graph tab initialized\n");

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
    lv_label_set_text(sw_label, "Remote Stream");
    lv_obj_set_style_text_font(sw_label, &lv_font_montserrat_14, 0);

    s_remote_switch = lv_switch_create(header_cont);
    lv_obj_set_size(s_remote_switch, 60, 32);
    if (remote_service_is_enabled()) {
        lv_obj_add_state(s_remote_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_remote_switch, remote_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Value Label ── */
    s_remote_label = lv_label_create(tab);
    lv_label_set_text(s_remote_label, "Value: 0.00");
    lv_obj_set_style_text_font(s_remote_label, &lv_font_montserrat_14, 0);

    /* ── Line Chart ── */
    s_remote_chart = lv_chart_create(tab);

    /* Chart needs to accept clicks for gesture detection but not scroll */
    lv_obj_add_flag(s_remote_chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_remote_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_remote_chart, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_remote_chart, swipe_event_cb, LV_EVENT_GESTURE, tabview);

    lv_obj_set_size(s_remote_chart, 220, 140);
    lv_chart_set_type(s_remote_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(s_remote_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_point_count(s_remote_chart, 50);
    lv_chart_set_range(s_remote_chart, LV_CHART_AXIS_PRIMARY_Y, -100, 100);
    lv_chart_set_div_line_count(s_remote_chart, 5, 5);

    s_remote_ser = lv_chart_add_series(s_remote_chart, lv_palette_main(LV_PALETTE_GREEN),
                                       LV_CHART_AXIS_PRIMARY_Y);

    /* ── Periodic Update Timer (10 Hz) ── */
    s_remote_timer = lv_timer_create(remote_graph_update_cb, 100, NULL);
}
