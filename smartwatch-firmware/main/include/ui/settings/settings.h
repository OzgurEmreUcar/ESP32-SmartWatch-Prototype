/**
 * @file settings.h
 * @brief Settings screen tab interface.
 *
 * Provides controls for display brightness, WiFi toggle,
 * sleep/always-awake policy, and manual RTC time configuration.
 * Also exposes async-safe UI update callbacks for WiFi state changes.
 */
#pragma once

#include "esp_lvgl_port.h"

/** @brief Build the Settings tab contents inside the given parent. */
extern void settings(lv_obj_t *tab);

/** @brief Async callback – update WiFi UI to "Connected" state. */
extern void ui_update_wifi_connected(void *p);

/** @brief Async callback – update WiFi UI to "Disconnected/OFF" state. */
extern void ui_update_wifi_disconnected(void *p);

/** @brief Async callback – update WiFi UI to "Connecting…" state. */
extern void ui_update_wifi_connecting(void *p);

/** @brief Refresh the Always-Awake button appearance from current setting. */
extern void ui_update_sleep_ui(void);
