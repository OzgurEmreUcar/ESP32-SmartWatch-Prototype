/**
 * @file flash_service.h
 * @brief Public interface for the onboard flash LED driver.
 *
 * Exposes initialization and control functions for the high-power
 * white LED on GPIO 4 of the AI-Thinker ESP32-CAM board.
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Initialize the flash LED GPIO as a push-pull output.
 *
 * Must be called once during boot before any call to flash_service_set().
 */
void flash_service_init(void);

/**
 * @brief Set the flash LED to the requested state.
 *
 * @param state  true to turn the LED on, false to turn it off.
 */
void flash_service_set(bool state);
