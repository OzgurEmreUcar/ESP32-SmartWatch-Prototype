/**
 * @file ui_init.h
 * @brief Top-level UI initialization and tab navigation.
 *
 * Creates the LVGL tabview, registers all application tabs,
 * and provides the global swipe gesture handler for tab switching.
 */
#pragma once

#include "esp_lvgl_port.h"
#include "lvgl.h"

/** @brief Build the full UI (tabview + all tabs) on the active screen. */
extern void app_ui_init(void);

/** @brief Global swipe gesture callback for left/right tab navigation. */
extern void swipe_event_cb(lv_event_t *e);