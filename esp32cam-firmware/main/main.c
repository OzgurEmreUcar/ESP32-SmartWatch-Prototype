/**
 * @file main.c
 * @brief Application entry point for the ESP32-CAM streaming server.
 *
 * Orchestrates the boot sequence: NVS initialization, hardware service
 * bring-up, Wi-Fi connection, and camera/HTTP server startup.
 */

#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "include/camera_init.h"
#include "include/flash_service.h"
#include "include/server_service.h"

/** @brief Wi-Fi station initialization (implemented in wifi_init.c). */
void wifi_init(void);

/**
 * @brief Application entry point.
 *
 * Executes the following boot sequence:
 *  1. Initialize Non-Volatile Storage (NVS) for Wi-Fi credential persistence.
 *  2. Configure the onboard flash LED hardware.
 *  3. Connect to the configured Wi-Fi access point.
 *  4. Initialize the OV2640 camera and start the HTTP/WebSocket server.
 */
void app_main(void) {
    /* Initialize NVS — required by the Wi-Fi driver for credential storage. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Configure the flash LED GPIO before any server endpoint can toggle it. */
    flash_service_init();

    /* Bring up Wi-Fi in station mode. */
    wifi_init();

    /* Allow the Wi-Fi stack time to associate and obtain an IP address. */
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Start the camera sensor and, on success, launch the HTTP server. */
    if (camera_init() == ESP_OK) {
        server_service_start();
    } else {
        ESP_LOGE("MAIN", "Camera failed to start!");
    }
}