/**
 * @file wifi_service.h
 * @brief WiFi connectivity service interface.
 *
 * Provides station-mode WiFi lifecycle management with NVS storage,
 * exponential-backoff retry logic, and a busy-state query for
 * coordinating with the power management subsystem.
 */
#pragma once

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

/** @brief Initialize NVS, network stack, and WiFi driver (does not start). */
extern void app_wifi_init(void);

/** @brief Check whether the WiFi module is user-enabled. */
extern bool wifi_service_is_enabled(void);

/** @brief Enable or disable the WiFi module (starts/stops the radio). */
extern void wifi_service_set_enabled(bool enabled);

/** @brief Returns true while WiFi is actively connecting (blocks sleep). */
extern bool wifi_service_is_busy(void);