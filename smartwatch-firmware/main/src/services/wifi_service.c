/**
 * @file wifi_service.c
 * @brief WiFi station-mode connectivity service.
 *
 * Manages the full WiFi lifecycle: NVS storage, station connection,
 * exponential-backoff retries (up to MAX_RETRY), and async UI updates.
 * Exposes a "busy" flag that the power service queries before allowing
 * light sleep, ensuring the chip never sleeps mid-handshake.
 */

#include "include/services/wifi_service.h"
#include "include/ui/settings/settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lvgl.h"

/* ── Configuration ────────────────────────────────────────────── */
static const char *TAG = "WiFiService";
#define MAX_RETRY      6            /**< Maximum consecutive retry attempts */
#define BASE_BACKOFF_S 2            /**< Initial backoff interval (seconds) */

/* ── Internal State Machine ───────────────────────────────────── */
typedef enum {
    WIFI_STATE_OFF,                 /**< WiFi stack is stopped              */
    WIFI_STATE_DISCONNECTED,        /**< Started but no active connection   */
    WIFI_STATE_CONNECTING,          /**< Handshake / association in progress*/
    WIFI_STATE_CONNECTED,           /**< Stable connection with IP assigned */
} wifi_state_t;

static bool         s_wifi_module_enabled = false;
static wifi_state_t s_current_state       = WIFI_STATE_OFF;
static int          s_retry_count         = 0;

/* ═══════════════════════════════════════════════════════════════
 *  Audit API
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Returns true while a WiFi handshake is in progress (blocks sleep). */
bool wifi_service_is_busy(void)
{
    return (s_current_state == WIFI_STATE_CONNECTING);
}

/* ═══════════════════════════════════════════════════════════════
 *  Exponential-Backoff Retry
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief One-shot FreeRTOS task that waits for the backoff period
 *        and then re-initiates a WiFi connection attempt.
 */
static void wifi_retry_task(void *pvParameters)
{
    uint32_t delay_s = (uint32_t)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(1000 * delay_s));

    if (s_wifi_module_enabled && s_current_state == WIFI_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "Retry sequence triggering connection attempt...");
        s_current_state = WIFI_STATE_CONNECTING;
        lv_async_call(ui_update_wifi_connecting, NULL);
        esp_wifi_connect();
    }
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════
 *  WiFi / IP Event Handler
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Unified event handler for WIFI_EVENT and IP_EVENT.
 *
 * Handles three key transitions:
 *   - STA_START  → initiate first connection attempt
 *   - STA_DISCONNECTED → schedule backoff retry (up to MAX_RETRY)
 *   - GOT_IP    → mark as connected, reset retry counter
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi Stack Started -> Initializing handshake");
        s_current_state = WIFI_STATE_CONNECTING;
        lv_async_call(ui_update_wifi_connecting, NULL);
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected (Reason: %d)", disc->reason);

        s_current_state = WIFI_STATE_DISCONNECTED;
        lv_async_call(ui_update_wifi_disconnected, NULL);

        if (s_wifi_module_enabled) {
            if (s_retry_count < MAX_RETRY) {
                s_retry_count++;
                uint32_t delay_s = BASE_BACKOFF_S << (s_retry_count - 1);
                if (delay_s > 64) delay_s = 64;    /* Cap at 64 s */

                ESP_LOGI(TAG, "Backing off: Retry %d/%d in %lu seconds",
                         s_retry_count, MAX_RETRY, delay_s);
                xTaskCreate(wifi_retry_task, "wifi_retry", 2048, (void *)delay_s, 5, NULL);
            } else {
                ESP_LOGE(TAG, "Max retries reached. Module remains IDLE until manual toggle.");
                s_retry_count = 0;
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count   = 0;
        s_current_state = WIFI_STATE_CONNECTED;
        ESP_LOGI(TAG, "Connection Stable (IP Obtained)");
        lv_async_call(ui_update_wifi_connected, NULL);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Public Service API
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Enable or disable the WiFi radio.
 *
 * TX power is capped at ~13 dBm after start to prevent LiPo voltage
 * sags that could reset the device during peak current draw.
 */
void wifi_service_set_enabled(bool enabled)
{
    if (s_wifi_module_enabled == enabled) return;

    s_wifi_module_enabled = enabled;
    if (enabled) {
        ESP_LOGI(TAG, "User enabled WiFi module");
        esp_wifi_start();
        esp_wifi_set_max_tx_power(52);  /* ~13 dBm – safe for small LiPo cells */
    } else {
        ESP_LOGI(TAG, "User disabled WiFi module");
        esp_wifi_stop();
        s_current_state = WIFI_STATE_OFF;
        s_retry_count   = 0;
    }
}

bool wifi_service_is_enabled(void)
{
    return s_wifi_module_enabled;
}

/* ═══════════════════════════════════════════════════════════════
 *  System Initialization
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief One-time WiFi subsystem initialization.
 *
 * Sequence:
 *   1. NVS flash (with auto-erase on version mismatch)
 *   2. Network interface and default event loop
 *   3. Event handler registration (WiFi + IP events)
 *   4. WiFi driver init + station config from Kconfig
 *   5. Power-save mode set to MIN_MODEM for light-sleep compatibility
 */
void app_wifi_init(void)
{
    /* 1 – NVS storage */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2 – Network stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* 3 – Event handlers */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    /* 4 – WiFi driver configuration */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid                = CONFIG_WIFI_SSID,
            .password            = CONFIG_WIFI_PASS,
            .threshold.rssi      = -127,
            .threshold.authmode  = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* 5 – MIN_MODEM allows sleeping between AP beacons without dropping */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

    ESP_LOGI(TAG, "WiFi Service Initialized [Power Save: MIN_MODEM]");
}