/**
 * @file battery.h
 * @brief Battery voltage monitoring interface.
 *
 * Reads LiPo battery voltage through an ADC channel connected
 * to a resistive voltage divider, and updates an LVGL label
 * with the computed voltage and charge percentage.
 */
#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"

/* ── ADC Configuration ────────────────────────────────────────── */
#define ADC_CHANNEL ADC_CHANNEL_0   /**< GPIO pin wired to the voltage divider midpoint */

/* ── Voltage Divider Parameters ───────────────────────────────── */
#define VREF 3.3f                   /**< ESP32 ADC reference voltage (V) */
#define R1   200000.0f              /**< Top resistor of the divider (Ω) */
#define R2   100000.0f              /**< Bottom resistor of the divider (Ω) */

/* ── Battery Voltage Thresholds ───────────────────────────────── */
#define BATTERY_VOLT_FULL  3.8f     /**< Voltage considered 100 % charge */
#define BATTERY_VOLT_EMPTY 3.2f     /**< Voltage considered 0 % charge */

/* ── Public API ───────────────────────────────────────────────── */

/** @brief Initialize ADC oneshot unit and configure the battery channel. */
extern void battery_init(void);

/** @brief Bind an LVGL label to receive battery status updates. */
extern void battery_set_label(lv_obj_t *label);

/** @brief FreeRTOS task that periodically reads battery voltage and updates the label. */
extern void battery_task(void *arg);

/**
 * @brief Discard stale ADC samples and force a fresh battery reading.
 *
 * After a long sleep the ADC's internal sample-and-hold capacitor
 * retains a stale charge.  This function performs several throwaway
 * reads to flush that charge, then triggers an immediate label
 * update so the displayed percentage is accurate on the first frame.
 */
extern void battery_adc_warmup(void);
