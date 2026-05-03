#include "include/services/flash_service.h"
#include "esp_log.h"
#include "esp_http_client.h"

static const char *TAG = "FLASH_SERVICE";
#define ESP32_CAM_IP "192.168.1.22"

static esp_http_client_handle_t s_http_client = NULL;

void flash_service_set_state(bool state)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s/flash/%s", ESP32_CAM_IP, state ? "on" : "off");

    if (s_http_client == NULL) {
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 2000,
            .keep_alive_enable = true,
        };
        s_http_client = esp_http_client_init(&config);
        if (s_http_client == NULL) {
            ESP_LOGE(TAG, "Failed to initialize HTTP client");
            return;
        }
    } else {
        esp_http_client_set_url(s_http_client, url);
        esp_http_client_set_method(s_http_client, HTTP_METHOD_GET);
    }

    ESP_LOGI(TAG, "Sending HTTP GET %s", url);
    esp_err_t err = esp_http_client_perform(s_http_client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d", esp_http_client_get_status_code(s_http_client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
    }
}
