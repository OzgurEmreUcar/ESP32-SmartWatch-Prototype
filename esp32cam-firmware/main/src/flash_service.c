/**
 * @file flash_service.c
 * @brief Onboard flash LED driver for the AI-Thinker ESP32-CAM.
 *
 * Provides a simple GPIO-based interface to control the high-power
 * white LED connected to GPIO 4. Used by the HTTP server to expose
 * remote flash-on / flash-off endpoints.
 */

#include "include/flash_service.h"
#include "driver/gpio.h"

/** @brief GPIO pin connected to the onboard flash LED. */
#define FLASH_GPIO 4

/**
 * @brief Initialize the flash LED GPIO.
 *
 * Resets the pin to its default state, configures it as a push-pull
 * output, and ensures the LED starts in the OFF position.
 */
void flash_service_init(void) {
    gpio_reset_pin(FLASH_GPIO);
    gpio_set_direction(FLASH_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASH_GPIO, 0);
}

/**
 * @brief Set the flash LED to the requested state.
 *
 * @param state  true to turn the LED on, false to turn it off.
 */
void flash_service_set(bool state) {
    gpio_set_level(FLASH_GPIO, state ? 1 : 0);
}
