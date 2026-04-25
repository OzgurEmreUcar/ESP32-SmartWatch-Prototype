/**
 * @file wifi_init.c
 * @brief Wi-Fi station mode initialization and event handling.
 *
 * Configures the ESP32 as a Wi-Fi station using credentials defined in
 * Kconfig (CONFIG_WIFI_SSID / CONFIG_WIFI_PASS). Handles connection,
 * reconnection on disconnect, and logs the assigned IP address.
 */

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "include/server_service.h"

/** @brief Log tag for Wi-Fi module messages. */
static const char *TAG = "WIFI_INIT";

/**
 * @brief Unified event handler for Wi-Fi and IP events.
 *
 * @param arg   User-supplied argument (unused).
 * @param base  Event base (WIFI_EVENT or IP_EVENT).
 * @param id    Specific event identifier.
 * @param data  Event-specific payload.
 *
 * Handles three scenarios:
 *  - STA_START:        Initiates the first connection attempt.
 *  - STA_DISCONNECTED: Tears down active streams and reconnects.
 *  - GOT_IP:           Logs the newly assigned IP address.
 */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        server_service_teardown_all();
        ESP_LOGW(TAG, "WiFi lost, reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief Initialize and start the Wi-Fi driver in station mode.
 *
 * Performs the full Wi-Fi bring-up sequence:
 *  1. Initialize the TCP/IP adapter and default event loop.
 *  2. Create the default station network interface.
 *  3. Register event handlers for connection lifecycle events.
 *  4. Apply SSID/password from Kconfig and start the driver.
 */
void wifi_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}
