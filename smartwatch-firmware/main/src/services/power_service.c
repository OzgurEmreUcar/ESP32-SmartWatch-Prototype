/**
 * @file power_service.c
 * @brief Inactivity-based sleep management service.
 *
 * Monitors user activity via a touch-reset timestamp and enters
 * ESP32 light sleep after a configurable timeout.  Coordinates with
 * system locks (camera, IMU, WiFi, etc.) and the "Always Awake"
 * toggle to decide whether sleep is permitted.
 *
 * The sleep/wake sequence carefully orders display, backlight,
 * and GPIO operations to prevent visible flicker or corruption.
 */

#include "include/services/power_service.h"
#include "include/services/wifi_service.h"
#include "include/init/lcd_init.h"
#include "include/init/touch_init.h"
#include "include/ui/home/battery.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern esp_lcd_touch_handle_t tp;

static const char *TAG = "PowerSvc";

/* ── Configuration ────────────────────────────────────────────── */
#define THRESH_SLEEP_MS    20000    /**< Inactivity threshold before light sleep (ms) */

/* ── Module State ─────────────────────────────────────────────── */
static bool              s_sleep_enabled = true;    /**< User "Always Awake" toggle (inverted) */
static bool              s_screen_on     = true;
static uint32_t          s_lock_mask     = 0;       /**< Active SYS_LOCK_* bitmask             */
static SemaphoreHandle_t s_lock_mux      = NULL;
static volatile int64_t  last_touch_time = 0;
static volatile bool     s_waking_up     = false;

/* ═══════════════════════════════════════════════════════════════
 *  System Lock Management
 * ═══════════════════════════════════════════════════════════════ */

void power_lock_set(uint32_t lock_bit, const char *reason)
{
    if (!s_lock_mux) return;
    xSemaphoreTake(s_lock_mux, portMAX_DELAY);
    s_lock_mask |= lock_bit;
    ESP_LOGI(TAG, "LOCK SET   [0x%02lX] %s", (unsigned long)lock_bit, reason ? reason : "");
    xSemaphoreGive(s_lock_mux);
}

void power_lock_clear(uint32_t lock_bit, const char *reason)
{
    if (!s_lock_mux) return;
    xSemaphoreTake(s_lock_mux, portMAX_DELAY);
    s_lock_mask &= ~lock_bit;
    ESP_LOGI(TAG, "LOCK CLEAR [0x%02lX] %s", (unsigned long)lock_bit, reason ? reason : "");
    xSemaphoreGive(s_lock_mux);
}

/** @brief Check whether any system lock is currently held. */
static bool locks_active(void)
{
    bool locked = false;
    if (s_lock_mux) {
        xSemaphoreTake(s_lock_mux, portMAX_DELAY);
        locked = (s_lock_mask != 0);
        xSemaphoreGive(s_lock_mux);
    }
    return locked;
}

/* ═══════════════════════════════════════════════════════════════
 *  Inactivity Timer
 * ═══════════════════════════════════════════════════════════════ */

void IRAM_ATTR power_reset_inactivity(void)
{
    last_touch_time = esp_timer_get_time();
}

/* ── Sleep Policy Accessors ───────────────────────────────────── */

void power_set_sleep_enabled(bool enabled) { s_sleep_enabled = enabled; }
bool power_is_sleep_enabled(void)          { return s_sleep_enabled; }
bool power_is_waking_up(void)              { return s_waking_up; }

/* ═══════════════════════════════════════════════════════════════
 *  Main Power Task
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Background task that monitors inactivity and manages light sleep.
 *
 * When sleep is allowed and the inactivity threshold is reached:
 *   1. Freeze the current LVGL frame, turn off display and backlight.
 *   2. Hold LCD reset and CS GPIOs to prevent SPI glitches during sleep.
 *   3. Enter ESP32 light sleep (woken by touch GPIO).
 *   4. On wake: release GPIO holds, flush stale touch data, reset LVGL
 *      timers and input devices, restore display, and relight backlight.
 */
static void power_task(void *arg)
{
    (void)arg;

    while (1) {
        int64_t now = esp_timer_get_time();
        bool blocked = wifi_service_is_busy() || locks_active() || !s_sleep_enabled;

        if (!blocked && s_screen_on && (now - last_touch_time) > (THRESH_SLEEP_MS * 1000LL)) {
            ESP_LOGI(TAG, "Entering Light Sleep...");
            s_waking_up = true;

            /* ── Pre-sleep: freeze display ── */
            lvgl_port_lock(-1);
            lv_refr_now(NULL);
            lcd_backlight_prepare_sleep();
            esp_lcd_panel_disp_on_off(lcd_panel, false);
            lvgl_port_unlock();

            /* Hold LCD control GPIOs to prevent floating during sleep */
            gpio_hold_en(LCD_GPIO_RST);
            gpio_hold_en(LCD_GPIO_CS);
            s_screen_on = false;

            /* ── Enter light sleep ── */
            esp_light_sleep_start();

            /* ── Post-wake recovery ── */
            ESP_LOGI(TAG, "Waking up...");

            gpio_hold_dis(LCD_GPIO_RST);
            gpio_hold_dis(LCD_GPIO_CS);

            /* Hardware-reset touch controller so it recalibrates its baseline */
            touch_reset_controller();

            /* Discard stale ADC charge so battery percentage is accurate instantly */
            battery_adc_warmup();

            /* Flush stale touch data generated by the wakeup interrupt */
            if (tp) {
                esp_lcd_touch_read_data(tp);
            }

            lvgl_port_lock(-1);

            /* Reset all LVGL timers to avoid a burst of queued callbacks */
            lv_timer_t *timer = lv_timer_get_next(NULL);
            while (timer != NULL) {
                lv_timer_reset(timer);
                timer = lv_timer_get_next(timer);
            }

            /* Clear stale input state across the sleep boundary */
            lv_indev_t *indev = lv_indev_get_next(NULL);
            while (indev != NULL) {
                lv_indev_reset(indev, NULL);
                indev = lv_indev_get_next(indev);
            }

            /* Wait for ST7789 to stabilize after display-on */
            esp_lcd_panel_disp_on_off(lcd_panel, true);
            vTaskDelay(pdMS_TO_TICKS(120));

            /* Force a full screen redraw */
            if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
            lv_refr_now(NULL);

            last_touch_time = esp_timer_get_time();
            s_screen_on = true;
            lvgl_port_unlock();

            /* Brief settle, then restore backlight */
            vTaskDelay(pdMS_TO_TICKS(20));
            lcd_backlight_reinit();
            s_waking_up = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════════ */

void power_service_init(void)
{
    s_lock_mux = xSemaphoreCreateMutex();
    configASSERT(s_lock_mux);

    last_touch_time = esp_timer_get_time();

    BaseType_t ok = xTaskCreate(power_task, "pwr_task", 8192, NULL, 5, NULL);
    configASSERT(ok == pdPASS);

    ESP_LOGI(TAG, "Power service initialised  [Sleep=%ds]", THRESH_SLEEP_MS / 1000);
}
