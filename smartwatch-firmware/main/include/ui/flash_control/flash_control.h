/**
 * @file flash_control.h
 * @brief Flash Control tab – public API.
 *
 * Renders a UI tab with a toggle button that controls a remote
 * ESP32-CAM flash LED via fire-and-forget HTTP requests.
 */
#pragma once

#include "esp_lvgl_port.h"

/**
 * @brief Build the Flash Control tab contents inside the given parent.
 * @param tab  Pointer to the LVGL tab object created by the tabview.
 */
extern void flash_control(lv_obj_t *tab);
