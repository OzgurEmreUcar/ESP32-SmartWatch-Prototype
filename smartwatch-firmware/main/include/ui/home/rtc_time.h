/**
 * @file rtc_time.h
 * @brief PCF85063 RTC time-keeping interface.
 *
 * Communicates with the PCF85063 real-time clock over I2C to
 * read and set the current time, and periodically updates an
 * LVGL label on the home screen.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** @brief PCF85063 7-bit I2C slave address. */
#define PCF85063_ADDR 0x51

/* ── Public API ───────────────────────────────────────────────── */

/**
 * @brief Initialize the PCF85063 RTC on the given I2C bus.
 * @param bus  Shared I2C master bus handle.
 * @return ESP_OK on success.
 */
extern esp_err_t rtc_time_init(i2c_master_bus_handle_t bus);

/**
 * @brief Write a new time to the RTC registers.
 * @param hours    0–23
 * @param minutes  0–59
 * @param seconds  0–59
 * @return ESP_OK on success.
 */
extern esp_err_t rtc_set_time(uint8_t hours, uint8_t minutes, uint8_t seconds);

/** @brief Bind an LVGL label to receive periodic clock updates. */
extern void rtc_set_label(lv_obj_t *label);

/** @brief FreeRTOS task that reads the RTC every second and refreshes the label. */
extern void rtc_time_task(void *arg);
