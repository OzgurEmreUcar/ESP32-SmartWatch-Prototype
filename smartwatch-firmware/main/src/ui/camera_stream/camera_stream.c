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
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_websocket_client.h"
#include "jpeg_decoder.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"

#include "include/init/lcd_init.h"
#include "include/init/touch_init.h"
#include "include/services/wifi_service.h"
#include "include/services/power_service.h"
#include "include/ui/settings/settings.h"

static const char *TAG = "WEB_CAM_CLIENT";

/* ── Configuration ────────────────────────────────────────────── */
#define WS_SERVER_URL    "ws://192.168.1.22/ws"
#define LCD_W            240
#define LCD_H            240
#define MAX_FRAME_SIZE   (60 * 1024)
#define WS_RECV_BUF_SIZE 4096

/** @brief Message passed from the WS handler to the display task. */
typedef struct {
    size_t len;         /**< JPEG payload size in bytes  */
    int    buf_idx;     /**< Index into decode_buffer[]  */
} frame_msg_t;

/* ── Module State ─────────────────────────────────────────────── */
static volatile bool stream_active = false;
static esp_websocket_client_handle_t ws_client = NULL;
static TaskHandle_t display_task_handle = NULL;

/* ── Double-Buffered Frame Pipeline ───────────────────────────── */
static uint16_t *frame_buffer[2]  = {NULL, NULL};   /**< Decoded RGB565 frames  */
static uint8_t  *decode_buffer[2] = {NULL, NULL};   /**< Raw JPEG input buffers */
static uint8_t  *assembling_buffer = NULL;           /**< WS fragment assembler  */
static size_t    current_len       = 0;
static int       active_decode_buf = 0;
static QueueHandle_t frame_queue   = NULL;

/* ── LVGL Canvas ──────────────────────────────────────────────── */
static lv_obj_t  *g_canvas   = NULL;
static uint8_t   *canvas_buf = NULL;
static volatile bool is_processing = false;

/* ═══════════════════════════════════════════════════════════════
 *  JPEG Decode & Display Task
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief FreeRTOS task – decode JPEG frames and blit to the LVGL canvas.
 *
 * Runs on core 1 to avoid contention with the WiFi/LWIP stack.
 * A 4 KB internal-RAM scratchpad is allocated for the tjpgd decoder.
 */
void display_task(void *pvParameters)
{
    frame_msg_t msg;
    int current_render_idx = 0;

    size_t work_size = 4096;
    void *work_buf = heap_caps_malloc(work_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG, "Display task started");

    while (stream_active) {
        if (xQueueReceive(frame_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        esp_jpeg_image_cfg_t dec_cfg = {
            .indata      = decode_buffer[msg.buf_idx],
            .indata_size = msg.len,
            .outbuf      = (uint8_t *)frame_buffer[current_render_idx],
            .outbuf_size = LCD_W * LCD_H * 2,
            .out_format  = JPEG_IMAGE_FORMAT_RGB565,
            .out_scale   = JPEG_IMAGE_SCALE_0,
            .flags       = { .swap_color_bytes = 0 },
            .advanced    = {
                .working_buffer      = work_buf,
                .working_buffer_size = work_size,
            }
        };

        esp_jpeg_image_output_t out_info;
        esp_err_t ret = esp_jpeg_decode(&dec_cfg, &out_info);

        if (ret == ESP_OK) {
            if (lvgl_port_lock(pdMS_TO_TICKS(50))) {
                if (g_canvas && canvas_buf) {
                    memcpy(canvas_buf, frame_buffer[current_render_idx], LCD_W * LCD_H * 2);
                    lv_canvas_set_buffer(g_canvas, canvas_buf, LCD_W, LCD_H, LV_COLOR_FORMAT_RGB565);
                    lv_obj_invalidate(g_canvas);
                }
                lvgl_port_unlock();
            }
        } else {
            ESP_LOGE(TAG, "JPEG Decode failed: %s", esp_err_to_name(ret));
        }

        current_render_idx = !current_render_idx;
        is_processing = false;
    }

    if (work_buf) heap_caps_free(work_buf);
    display_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════
 *  WebSocket Event Handler
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Assemble fragmented binary WS frames into complete JPEG images.
 *
 * WebSocket frames may arrive in multiple data events. Fragments are
 * accumulated in assembling_buffer until payload_len is reached, then
 * validated (SOI marker 0xFFD8) and dispatched to the decode queue.
 */
static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    if (!stream_active) return;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    /* Only process binary frames (opcode 0x02) */
    if (event_id != WEBSOCKET_EVENT_DATA || data->op_code != 0x02) return;

    /* Start of a new frame */
    if (data->payload_offset == 0) {
        current_len = 0;
        if (is_processing) return;
    }

    /* Accumulate fragments */
    if (!is_processing && (current_len + data->data_len <= MAX_FRAME_SIZE)) {
        memcpy(assembling_buffer + current_len, data->data_ptr, data->data_len);
        current_len += data->data_len;
    }

    /* Full frame received – validate JPEG SOI and dispatch */
    if (data->payload_len > 0 && current_len >= data->payload_len) {
        if (assembling_buffer[0] == 0xFF && assembling_buffer[1] == 0xD8) {
            is_processing = true;
            memcpy(decode_buffer[active_decode_buf], assembling_buffer, current_len);

            frame_msg_t msg = { .len = current_len, .buf_idx = active_decode_buf };
            active_decode_buf = !active_decode_buf;
            xQueueSend(frame_queue, &msg, 0);
        }
        current_len = 0;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Stream Lifecycle
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Allocate buffers, start the decode task, and connect WebSocket. */
static void start_camera_stream(void)
{
    if (stream_active) return;

    stream_active = true;
    is_processing = false;
    current_len   = 0;

    /* All large buffers are allocated in PSRAM */
    frame_buffer[0]   = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    frame_buffer[1]   = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    decode_buffer[0]  = heap_caps_malloc(MAX_FRAME_SIZE,    MALLOC_CAP_SPIRAM);
    decode_buffer[1]  = heap_caps_malloc(MAX_FRAME_SIZE,    MALLOC_CAP_SPIRAM);
    assembling_buffer = heap_caps_malloc(MAX_FRAME_SIZE,    MALLOC_CAP_SPIRAM);
    canvas_buf        = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);

    if (!frame_buffer[0] || !canvas_buf || !assembling_buffer) {
        ESP_LOGE(TAG, "Buffer allocation failed");
        return;
    }

    if (!frame_queue) frame_queue = xQueueCreate(2, sizeof(frame_msg_t));

    /* Pin decode task to core 1 to avoid WiFi/LWIP contention */
    xTaskCreatePinnedToCore(display_task, "display_task", 8192, NULL, 5,
                            &display_task_handle, 1);

    const esp_websocket_client_config_t ws_cfg = {
        .uri        = WS_SERVER_URL,
        .buffer_size = WS_RECV_BUF_SIZE,
        .task_stack  = 8192,
    };

    ws_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    esp_websocket_client_start(ws_client);
}

/** @brief Disconnect WebSocket, wait for task exit, and free all buffers. */
static void stop_camera_stream(void)
{
    if (!stream_active) return;
    stream_active = false;

    if (ws_client) {
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        ws_client = NULL;
    }

    /* Wait for the display task to self-delete */
    while (display_task_handle != NULL) vTaskDelay(pdMS_TO_TICKS(10));

    if (frame_queue) { vQueueDelete(frame_queue); frame_queue = NULL; }
    heap_caps_free(frame_buffer[0]);
    heap_caps_free(frame_buffer[1]);
    heap_caps_free(decode_buffer[0]);
    heap_caps_free(decode_buffer[1]);
    heap_caps_free(assembling_buffer);
    heap_caps_free(canvas_buf);

    ESP_LOGI(TAG, "Stream stopped.");
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
        stop_camera_stream();
        power_lock_clear(SYS_LOCK_CAMERA, "CAMERA_STREAM_OFF");
    } else {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        lv_label_set_text(label, "STOP STREAM");
        start_camera_stream();
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
    return stream_active;
}