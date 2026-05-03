/**
 * @file remote_service.c
 * @brief Remote sensor data stream via WebSocket.
 *
 * Connects to an external ESP32's WebSocket endpoint to receive
 * real-time sensor values as ASCII text frames.  The latest value
 * is cached for consumption by the remote graph UI tab.
 * A system power lock is held while the stream is active.
 */

#include "include/services/remote_service.h"
#include "include/services/power_service.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "REMOTE_SVC";

/** @brief WebSocket URI of the remote sensor endpoint. */
#define SENSOR_WS_URL "ws://192.168.1.22/sensor"

/* ── Module State ─────────────────────────────────────────────── */
static float                        s_remote_value   = 0.0f;
static bool                         s_remote_enabled = false;
static esp_websocket_client_handle_t s_ws_client     = NULL;

/* ═══════════════════════════════════════════════════════════════
 *  WebSocket Event Handler
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Parse incoming text frames and cache the sensor value.
 *
 * Only text frames (opcode 0x01) are processed; the payload is
 * expected to be a plain ASCII floating-point number.
 */
static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    if (event_id == WEBSOCKET_EVENT_DATA) {
        if (data->op_code == 0x01 && data->data_len > 0) {
            char buf[32];
            int len = data->data_len > 31 ? 31 : data->data_len;
            memcpy(buf, data->data_ptr, len);
            buf[len] = 0;
            s_remote_value = strtof(buf, NULL);
        }
    } else if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "Sensor WebSocket Disconnected");
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════ */

float remote_service_get_value(void)
{
    return s_remote_value;
}

void remote_service_set_enable(bool enable)
{
    if (s_remote_enabled == enable) return;
    s_remote_enabled = enable;

    if (enable) {
        ESP_LOGI(TAG, "Enabling Remote Sensor Stream...");
        const esp_websocket_client_config_t ws_cfg = {
            .uri = SENSOR_WS_URL,
        };
        s_ws_client = esp_websocket_client_init(&ws_cfg);
        esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
        esp_websocket_client_start(s_ws_client);

        power_lock_set(SYS_LOCK_REMOTE, "Remote Graph WebSocket Active");
    } else {
        ESP_LOGI(TAG, "Disabling Remote Sensor Stream...");
        if (s_ws_client) {
            esp_websocket_client_stop(s_ws_client);
            esp_websocket_client_destroy(s_ws_client);
            s_ws_client = NULL;
        }
        power_lock_clear(SYS_LOCK_REMOTE, "Remote Graph WebSocket Inactive");
        s_remote_value = 0.0f;
    }
}

bool remote_service_is_enabled(void)
{
    return s_remote_enabled;
}
