/**
 * @file main.c
 * @brief Application entry point for the ESP32-S3 smart display.
 *
 * Initializes all hardware subsystems and services in the required
 * dependency order: power rail → touch/I2C → LCD/SPI → LVGL input →
 * WiFi → UI → background services.
 */

#include <stdio.h>
#include "include/init/lcd_init.h"
#include "include/init/touch_init.h"
#include "include/init/ui_init.h"
#include "include/init/power_init.h"
#include "include/services/power_service.h"
#include "include/services/wifi_service.h"
#include "include/services/imu_service.h"
#include "include/services/remote_service.h"

void app_main(void)
{
    /* Hardware initialization (order matters) */
    app_power_init();       /* Latch system power rail first           */
    app_touch_init();       /* I2C bus + CST816S touch controller      */
    app_lcd_init();         /* SPI bus + ST7789 LCD + LVGL display     */
    app_lvgl_touch_init();  /* Register touch input device with LVGL   */
    app_wifi_init();        /* NVS + network stack + WiFi driver       */

    /* UI and background services */
    app_ui_init();          /* Build tabview and all application tabs   */
    power_service_init();   /* Inactivity monitor and light-sleep task  */
    imu_service_init();     /* QMI8658C IMU polling task                */
    remote_service_init();  /* WebSocket sensor stream (on-demand)      */
}
