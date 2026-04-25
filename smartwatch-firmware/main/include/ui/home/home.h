/**
 * @file home.h
 * @brief Home screen tab interface.
 *
 * Renders the home screen with a wallpaper background,
 * battery status widget, and real-time clock display.
 */
#pragma once

#include "esp_lvgl_port.h"

/**
 * @brief Build the Home tab contents inside the given parent.
 * @param tab  Pointer to the LVGL tab object created by the tabview.
 */
extern void home(lv_obj_t *tab);