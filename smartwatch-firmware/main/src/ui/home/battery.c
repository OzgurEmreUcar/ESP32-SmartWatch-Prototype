/**
 * @file battery.c
 * @brief Battery voltage monitoring UI.
 */

#include "include/ui/home/battery.h"
#include "include/services/battery_service.h"
#include <stdio.h>

static lv_obj_t *battery_label = NULL;

static void ui_battery_update_cb(float voltage, float pct)
{
    if (voltage < 0.0f) return;

    char buf[24];
    snprintf(buf, sizeof(buf), "%.1fV  %.0f%%", voltage, pct);

    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        if (battery_label) {
            lv_label_set_text(battery_label, buf);
        }
        lvgl_port_unlock();
    }
}

void battery_init(void)
{
    battery_service_register_cb(ui_battery_update_cb);
    battery_service_init();
}

void battery_set_label(lv_obj_t *label)
{
    battery_label = label;
}

void battery_task(void *arg)
{
    battery_service_task(arg);
}

void battery_adc_warmup(void)
{
    battery_service_warmup();
}
