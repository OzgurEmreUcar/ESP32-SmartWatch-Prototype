#ifndef RTC_SERVICE_H
#define RTC_SERVICE_H

#include "driver/i2c_master.h"
#include <stdint.h>

/**
 * @brief Initialize the PCF85063 RTC on the shared I2C bus.
 */
esp_err_t rtc_service_init(i2c_master_bus_handle_t bus);

/**
 * @brief FreeRTOS task – reads the RTC periodically.
 */
void rtc_service_task(void *arg);

/**
 * @brief Register a callback to receive time updates.
 */
void rtc_service_register_cb(void (*cb)(uint8_t h, uint8_t m, uint8_t s, bool valid));

/**
 * @brief Set the RTC time.
 */
esp_err_t rtc_service_set_time(uint8_t hours, uint8_t minutes, uint8_t seconds);

/**
 * @brief Force a time read and trigger the callback.
 */
void rtc_service_force_update(void);

#endif // RTC_SERVICE_H
