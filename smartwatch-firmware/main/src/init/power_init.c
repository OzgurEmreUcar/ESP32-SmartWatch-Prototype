/**
 * @file power_init.c
 * @brief System power rail initialization.
 *
 * Drives the SYS_EN GPIO high and latches it via gpio_hold_en()
 * so the main regulator stays active after the power button is released.
 * This must be the very first init call in app_main().
 */

#include "driver/gpio.h"
#include "include/init/power_init.h"

void app_power_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask   = (1ULL << SYS_EN),
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(SYS_EN, 1);
    gpio_hold_en(SYS_EN);          /* Latch high through sleep cycles */
}