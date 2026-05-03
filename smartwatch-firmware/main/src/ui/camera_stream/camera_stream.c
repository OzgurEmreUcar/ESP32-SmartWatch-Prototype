/**
 * @file camera_stream.c
 * @brief Camera stream tab – live MJPEG viewer over WebSocket.
 *
 * Connects to a remote ESP32-CAM via WebSocket, receives binary
 * JPEG frames, decodes them in a dedicated FreeRTOS task, and
 * renders the result on an LVGL canvas using double-buffered
 * PSRAM frame buffers for tear-free display.
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "include/init/lcd_init.h"
#include "include/init/touch_init.h"
#include "include/services/power_service.h"
#include "include/services/camera_service.h"
#include "include/ui/camera_stream/camera_stream.h"

static const char *TAG = "WEB_CAM_UI";

#define LCD_W 240
#define LCD_H 240

/* ── LVGL Canvas ──────────────────────────────────────────────── */
static lv_obj_t  *g_canvas   = NULL;
static uint8_t   *canvas_buf = NULL;

/* ═══════════════════════════════════════════════════════════════
 *  Callback for New Frames
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Called by camera service when a new frame is decoded.
 */
static void ui_camera_frame_cb(uint16_t *frame_data, size_t width, size_t height)
{
    if (lvgl_port_lock(pdMS_TO_TICKS(50))) {
        if (g_canvas && canvas_buf) {
            memcpy(canvas_buf, frame_data, width * height * 2);
            lv_canvas_set_buffer(g_canvas, canvas_buf, width, height, LV_COLOR_FORMAT_RGB565);
            lv_obj_invalidate(g_canvas);
        }
        lvgl_port_unlock();
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  UI Event Callbacks
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Start/stop the camera stream when the button is clicked. */
static void stream_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);

    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;

    bool was_on = lv_obj_has_state(btn, LV_STATE_CHECKED);
    lv_obj_t *label = lv_obj_get_child(btn, 0);

    if (was_on) {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
        lv_label_set_text(label, "START STREAM");
        
        camera_service_stop();
        
        if (canvas_buf) {
            heap_caps_free(canvas_buf);
            canvas_buf = NULL;
        }

        power_lock_clear(SYS_LOCK_CAMERA, "CAMERA_STREAM_OFF");
    } else {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        lv_label_set_text(label, "STOP STREAM");
        
        if (!canvas_buf) {
            canvas_buf = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
            if (!canvas_buf) {
                ESP_LOGE(TAG, "Failed to allocate canvas buffer");
                return;
            }
        }
        
        camera_service_start(ui_camera_frame_cb);
        power_lock_set(SYS_LOCK_CAMERA, "CAMERA_STREAM_ON");
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Tab Construction
 * ═══════════════════════════════════════════════════════════════ */

void camera_stream_tab(lv_obj_t *tab)
{
    /* Vertical flex: canvas on top, button fills remaining space */
    lv_obj_set_layout(tab, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_pad_row(tab, 0, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);

    /* Video canvas */
    g_canvas = lv_canvas_create(tab);
    lv_obj_set_size(g_canvas, LCD_W, LCD_H);
    lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);

    /* Start/Stop button (fills all remaining vertical space) */
    lv_obj_t *btn = lv_button_create(tab);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_add_event_cb(btn, stream_switch_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_radius(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "START STREAM");
    lv_obj_center(label);
}

bool camera_stream_is_active(void)
{
    return camera_service_is_active();
}