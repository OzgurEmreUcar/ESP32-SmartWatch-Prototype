#include "include/services/camera_service.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_websocket_client.h"
#include "jpeg_decoder.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "CAMERA_SERVICE";

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
static camera_frame_cb_t g_frame_cb = NULL;

/* ── Double-Buffered Frame Pipeline ───────────────────────────── */
static uint16_t *frame_buffer[2]  = {NULL, NULL};   /**< Decoded RGB565 frames  */
static uint8_t  *decode_buffer[2] = {NULL, NULL};   /**< Raw JPEG input buffers */
static uint8_t  *assembling_buffer = NULL;           /**< WS fragment assembler  */
static size_t    current_len       = 0;
static int       active_decode_buf = 0;
static QueueHandle_t frame_queue   = NULL;
static volatile bool is_processing = false;

/* ═══════════════════════════════════════════════════════════════
 *  JPEG Decode Task
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief FreeRTOS task – decode JPEG frames.
 *
 * Runs on core 1 to avoid contention with the WiFi/LWIP stack.
 * A 4 KB internal-RAM scratchpad is allocated for the tjpgd decoder.
 */
static void display_task(void *pvParameters)
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
            if (g_frame_cb) {
                g_frame_cb(frame_buffer[current_render_idx], LCD_W, LCD_H);
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

void camera_service_start(camera_frame_cb_t frame_cb)
{
    if (stream_active) return;

    stream_active = true;
    is_processing = false;
    current_len   = 0;
    g_frame_cb    = frame_cb;

    /* All large buffers are allocated in PSRAM */
    frame_buffer[0]   = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    frame_buffer[1]   = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    decode_buffer[0]  = heap_caps_malloc(MAX_FRAME_SIZE,    MALLOC_CAP_SPIRAM);
    decode_buffer[1]  = heap_caps_malloc(MAX_FRAME_SIZE,    MALLOC_CAP_SPIRAM);
    assembling_buffer = heap_caps_malloc(MAX_FRAME_SIZE,    MALLOC_CAP_SPIRAM);

    if (!frame_buffer[0] || !assembling_buffer) {
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

void camera_service_stop(void)
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
    
    if (frame_buffer[0]) heap_caps_free(frame_buffer[0]);
    if (frame_buffer[1]) heap_caps_free(frame_buffer[1]);
    if (decode_buffer[0]) heap_caps_free(decode_buffer[0]);
    if (decode_buffer[1]) heap_caps_free(decode_buffer[1]);
    if (assembling_buffer) heap_caps_free(assembling_buffer);

    g_frame_cb = NULL;
    ESP_LOGI(TAG, "Stream stopped.");
}

bool camera_service_is_active(void)
{
    return stream_active;
}
