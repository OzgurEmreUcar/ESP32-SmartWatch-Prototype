/**
 * @file touch_init.h
 * @brief CST816S capacitive touch controller initialization.
 *
 * Configures I2C communication, touch interrupt handling, and
 * GPIO wakeup for light-sleep recovery via the touch panel.
 */
#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch_cst816s.h"

/* ── I2C / Touch GPIO Assignments ─────────────────────────────── */
#define TOUCH_HOST         I2C_NUM_0
#define PIN_NUM_TOUCH_SCL  (GPIO_NUM_10)
#define PIN_NUM_TOUCH_SDA  (GPIO_NUM_11)
#define PIN_NUM_TOUCH_RST  (GPIO_NUM_13)
#define PIN_NUM_TOUCH_INT  (GPIO_NUM_14)

/* ── Shared LVGL Display Handle ───────────────────────────────── */
extern lv_display_t *lvgl_disp;

/* ── Public API ───────────────────────────────────────────────── */

/** @brief Initialize I2C bus, CST816S driver, and touch interrupt. */
extern void app_touch_init(void);

/** @brief Register the touch input device with LVGL. */
extern void app_lvgl_touch_init(void);

/** @brief Retrieve the shared I2C bus handle for other peripherals. */
extern i2c_master_bus_handle_t app_get_i2c_bus(void);

/**
 * @brief Hardware-reset the CST816S touch controller.
 *
 * Issues a reset pulse on the RST GPIO and waits for the controller
 * to recalibrate.  Must be called after waking from light sleep so
 * the capacitive baseline is re-established immediately instead of
 * drifting back to accuracy over several seconds.
 */
extern void touch_reset_controller(void);