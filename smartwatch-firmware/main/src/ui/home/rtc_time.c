/**
 * @file rtc_time.c
 * @brief PCF85063 real-time clock UI wrapper.
 */

#include "include/ui/home/rtc_time.h"
#include "include/services/rtc_service.h"
#include <stdio.h>

static lv_obj_t *time_label = NULL;

static void ui_rtc_update_cb(uint8_t h, uint8_t m, uint8_t s, bool valid)
{
    char buf[12];
    if (valid) {
        snprintf(buf, sizeof(buf), "%02d\n%02d\n%02d", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "--\n--\n--");
    }

    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        if (time_label) {
            lv_label_set_text(time_label, buf);
        }
        lvgl_port_unlock();
    }
}

esp_err_t rtc_time_init(i2c_master_bus_handle_t bus)
{
    rtc_service_register_cb(ui_rtc_update_cb);
    return rtc_service_init(bus);
}

esp_err_t rtc_set_time(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    return rtc_service_set_time(hours, minutes, seconds);
}

void rtc_set_label(lv_obj_t *label)
{
    time_label = label;
}

void rtc_time_task(void *arg)
{
    rtc_service_task(arg);
}

void rtc_force_update(void)
{
    rtc_service_force_update();
}
