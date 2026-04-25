/**
 * @file server_service.c
 * @brief HTTP / WebSocket server for camera streaming and sensor data.
 *
 * Exposes four endpoints:
 *  - /ws          – WebSocket: live JPEG camera stream (binary frames).
 *  - /sensor      – WebSocket: periodic sine-wave sensor data (text frames).
 *  - /flash/on    – HTTP GET: turns the onboard flash LED on.
 *  - /flash/off   – HTTP GET: turns the onboard flash LED off.
 *
 * Each WebSocket channel supports a single active client at a time;
 * reconnection gracefully tears down the previous session.
 */

#include "include/server_service.h"
#include "include/flash_service.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "lwip/sockets.h"

/** @brief Log tag for server service messages. */
static const char *TAG = "SERVER_SVC";

#ifndef M_PI
/** @brief Pi constant fallback if not provided by the math library. */
#define M_PI 3.14159265358979323846
#endif

/* ── Stream Context ─────────────────────────────────────────────────────── */

/**
 * @brief Tracks the state of a single WebSocket streaming session.
 *
 * Each channel (camera / sensor) maintains one of these contexts.
 * Access is serialized through @ref g_ctx_mutex.
 */
typedef struct {
    httpd_handle_t hd;      /**< HTTP server handle owning this socket. */
    int            fd;      /**< Raw socket file descriptor.            */
    bool           active;  /**< True while the session is valid.       */
} stream_ctx_t;

/** @brief Context for the camera JPEG stream (/ws).   */
static stream_ctx_t g_stream_ctx = { .hd = NULL, .fd = -1, .active = false };

/** @brief Context for the sensor data stream (/sensor). */
static stream_ctx_t g_sensor_ctx = { .hd = NULL, .fd = -1, .active = false };

/** @brief Mutex protecting concurrent access to stream contexts. */
static SemaphoreHandle_t g_ctx_mutex = NULL;

/* ── Low-Level WebSocket Utilities ──────────────────────────────────────── */

/**
 * @brief Send data over a socket with automatic retry on transient errors.
 *
 * Retries up to 5 times on EAGAIN / EWOULDBLOCK before giving up.
 *
 * @param fd   Socket file descriptor.
 * @param buf  Pointer to the data buffer.
 * @param n    Number of bytes to send.
 * @return     Number of bytes sent on success, -1 on failure.
 */
static int safe_send(int fd, const void *buf, size_t n) {
    int retry = 5;
    while (retry > 0) {
        int sent = send(fd, buf, n, 0);
        if (sent >= 0) return sent;
        if (errno == EAGAIN || errno == 119) {
            vTaskDelay(pdMS_TO_TICKS(5));
            retry--;
            continue;
        }
        return -1;
    }
    return -1;
}

/**
 * @brief Construct and transmit a complete WebSocket frame.
 *
 * Builds a minimal (unmasked, server-to-client) frame header and sends
 * the payload in a loop until all bytes are delivered.
 *
 * @param fd      Socket file descriptor.
 * @param data    Payload bytes.
 * @param len     Payload length.
 * @param binary  True for a binary (opcode 0x82) frame, false for text (0x81).
 * @return        ESP_OK on success, ESP_FAIL on any send error.
 */
static esp_err_t send_ws_frame(int fd, uint8_t *data, size_t len, bool binary) {
    if (fd < 0) return ESP_FAIL;

    uint8_t header[10];
    size_t  header_len = 2;

    header[0] = binary ? 0x82 : 0x81;
    if (len < 126) {
        header[1] = (uint8_t)len;
    } else if (len <= 65535) {
        header[1] = 126;
        header[2] = (uint8_t)((len >> 8) & 0xFF);
        header[3] = (uint8_t)(len & 0xFF);
        header_len = 4;
    }

    if (safe_send(fd, header, header_len) < 0) return ESP_FAIL;

    size_t total_sent = 0;
    while (total_sent < len) {
        int sent = safe_send(fd, data + total_sent, len - total_sent);
        if (sent < 0) return ESP_FAIL;
        total_sent += sent;
    }
    return ESP_OK;
}

/* ── Context Lifecycle Management ───────────────────────────────────────── */

/**
 * @brief Gracefully tear down a streaming context.
 *
 * Marks the context inactive, clears its fields under the mutex, and
 * triggers an HTTP session close so the socket is properly released.
 *
 * @param ctx     Pointer to the stream context to tear down.
 * @param reason  Human-readable reason string for the log message.
 */
static void teardown_ctx(stream_ctx_t *ctx, const char *reason) {
    xSemaphoreTake(g_ctx_mutex, portMAX_DELAY);
    httpd_handle_t hd = ctx->hd;
    int fd = ctx->fd;
    ctx->active = false;
    ctx->hd = NULL;
    ctx->fd = -1;
    xSemaphoreGive(g_ctx_mutex);
    if (hd != NULL && fd >= 0) {
        httpd_sess_trigger_close(hd, fd);
        ESP_LOGW(TAG, "Context teardown: %s (fd=%d)", reason, fd);
    }
}

/**
 * @brief Tear down all active WebSocket sessions.
 *
 * Called on Wi-Fi disconnect to ensure stale sockets are cleaned up
 * before a reconnection attempt.
 */
void server_service_teardown_all(void) {
    teardown_ctx(&g_stream_ctx, "global teardown");
    teardown_ctx(&g_sensor_ctx, "global teardown");
}

/* ── Background Streaming Tasks ─────────────────────────────────────────── */

/**
 * @brief Continuous camera frame streaming task.
 *
 * Polls for an active camera WebSocket client; when one is connected,
 * captures JPEG frames from the OV2640 and pushes them as binary
 * WebSocket frames. Runs indefinitely on CPU core 0.
 *
 * @param arg  Unused task parameter.
 */
static void stream_task(void *arg) {
    while (true) {
        xSemaphoreTake(g_ctx_mutex, portMAX_DELAY);
        bool active = g_stream_ctx.active;
        int fd = g_stream_ctx.fd;
        xSemaphoreGive(g_ctx_mutex);

        if (!active || fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Stream frames as long as the client remains connected. */
        while (true) {
            xSemaphoreTake(g_ctx_mutex, portMAX_DELAY);
            bool still_active = g_stream_ctx.active && (g_stream_ctx.fd == fd);
            xSemaphoreGive(g_ctx_mutex);
            if (!still_active) break;

            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

            esp_err_t ret = send_ws_frame(fd, fb->buf, fb->len, true);
            esp_camera_fb_return(fb);

            if (ret != ESP_OK) { teardown_ctx(&g_stream_ctx, "camera sent fail"); break; }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/**
 * @brief Periodic sensor data streaming task.
 *
 * Generates a synthetic sine-wave signal (amplitude ±80) and sends each
 * sample as a text WebSocket frame to the connected client on the
 * /sensor endpoint. Used for demonstration and UI graph testing.
 *
 * @param arg  Unused task parameter.
 */
static void sensor_task(void *arg) {
    float angle = 0.0f;
    while (true) {
        xSemaphoreTake(g_ctx_mutex, portMAX_DELAY);
        bool active = g_sensor_ctx.active;
        int fd = g_sensor_ctx.fd;
        xSemaphoreGive(g_ctx_mutex);

        if (!active || fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Push sine-wave samples while the client remains connected. */
        while (true) {
            xSemaphoreTake(g_ctx_mutex, portMAX_DELAY);
            bool still_active = g_sensor_ctx.active && (g_sensor_ctx.fd == fd);
            xSemaphoreGive(g_ctx_mutex);
            if (!still_active) break;

            float value = sinf(angle) * 80.0f;
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "%.2f", value);

            if (send_ws_frame(fd, (uint8_t*)buf, len, false) != ESP_OK) {
                teardown_ctx(&g_sensor_ctx, "sensor send fail");
                break;
            }

            angle += 0.1f;
            if (angle > 2 * M_PI) angle -= 2 * M_PI;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ── HTTP / WebSocket URI Handlers ──────────────────────────────────────── */

/**
 * @brief WebSocket handler for the camera stream endpoint (/ws).
 *
 * Accepts a new client connection, tears down any previous session,
 * and activates the camera stream context so that @ref stream_task
 * begins pushing frames.
 *
 * @param req  Incoming HTTP request (upgraded to WebSocket).
 * @return     ESP_OK.
 */
static esp_err_t stream_handler(httpd_req_t *req) {
    int new_fd = httpd_req_to_sockfd(req);
    teardown_ctx(&g_stream_ctx, "reconnect");
    xSemaphoreTake(g_ctx_mutex, portMAX_DELAY);
    g_stream_ctx.hd = req->handle;
    g_stream_ctx.fd = new_fd;
    g_stream_ctx.active = true;
    xSemaphoreGive(g_ctx_mutex);
    return ESP_OK;
}

/**
 * @brief WebSocket handler for the sensor data endpoint (/sensor).
 *
 * Accepts a new client connection, tears down any previous session,
 * and activates the sensor context so that @ref sensor_task begins
 * sending sine-wave samples.
 *
 * @param req  Incoming HTTP request (upgraded to WebSocket).
 * @return     ESP_OK.
 */
static esp_err_t sensor_handler(httpd_req_t *req) {
    int new_fd = httpd_req_to_sockfd(req);
    teardown_ctx(&g_sensor_ctx, "reconnect");
    xSemaphoreTake(g_ctx_mutex, portMAX_DELAY);
    g_sensor_ctx.hd = req->handle;
    g_sensor_ctx.fd = new_fd;
    g_sensor_ctx.active = true;
    xSemaphoreGive(g_ctx_mutex);
    return ESP_OK;
}

/**
 * @brief HTTP GET handler to turn the flash LED on (/flash/on).
 *
 * @param req  Incoming HTTP request.
 * @return     ESP_OK after responding with "ON".
 */
static esp_err_t flash_on_handler(httpd_req_t *req) {
    flash_service_set(true);
    httpd_resp_send(req, "ON", 2);
    return ESP_OK;
}

/**
 * @brief HTTP GET handler to turn the flash LED off (/flash/off).
 *
 * @param req  Incoming HTTP request.
 * @return     ESP_OK after responding with "OFF".
 */
static esp_err_t flash_off_handler(httpd_req_t *req) {
    flash_service_set(false);
    httpd_resp_send(req, "OFF", 3);
    return ESP_OK;
}

/* ── Server Initialization ──────────────────────────────────────────────── */

/**
 * @brief Initialize and start the HTTP/WebSocket server.
 *
 * Creates the context mutex, launches background streaming tasks,
 * starts the HTTPD on the default port, and registers all URI handlers.
 */
void server_service_start(void) {
    g_ctx_mutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(stream_task, "stream_task", 4096, NULL, 5, NULL, 0);
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 5;

    if (httpd_start(&server, &config) == ESP_OK) {
        static const httpd_uri_t uris[] = {
            { .uri = "/ws",        .method = HTTP_GET, .handler = stream_handler,  .is_websocket = true },
            { .uri = "/sensor",    .method = HTTP_GET, .handler = sensor_handler,  .is_websocket = true },
            { .uri = "/flash/on",  .method = HTTP_GET, .handler = flash_on_handler },
            { .uri = "/flash/off", .method = HTTP_GET, .handler = flash_off_handler }
        };
        for (int i = 0; i < 4; i++) {
            httpd_register_uri_handler(server, &uris[i]);
        }
        ESP_LOGI(TAG, "Endpoints registered: /ws, /sensor, /flash/on, /flash/off");
    }
}
