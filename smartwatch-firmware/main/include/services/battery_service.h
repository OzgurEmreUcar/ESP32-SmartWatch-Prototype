#ifndef BATTERY_SERVICE_H
#define BATTERY_SERVICE_H

/**
 * @brief Initialize the battery service (ADC).
 */
void battery_service_init(void);

/**
 * @brief FreeRTOS task – reads battery periodically.
 */
void battery_service_task(void *arg);

/**
 * @brief Register a callback to be called when the battery is read.
 */
void battery_service_register_cb(void (*cb)(float voltage, float pct));

/**
 * @brief Post-Sleep ADC Warmup
 */
void battery_service_warmup(void);

#endif // BATTERY_SERVICE_H
