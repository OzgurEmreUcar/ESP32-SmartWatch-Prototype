/**
 * @file power_service.c
 * @brief Tiered inactivity-based power management service.
 *
 * Monitors user activity via a touch-reset timestamp and progressively
 * reduces power consumption through three display tiers:
 *
 *   ACTIVE → DIM (backlight at 25 %) → OFF (display sleep) → Light Sleep
 *
 * Coordinates with system locks (camera, IMU, WiFi, etc.) and the
 * "Always Awake" toggle to decide whether power saving is permitted.
 *
 * The sleep/wake sequence carefully orders display, backlight,
 * and GPIO operations to minimize visible wake latency while
 * preserving reliable touch controller recovery.
 */

#include "include/services/power_service.h"
#include "include/services/wifi_service.h"
#include "include/init/lcd_init.h"
#include "include/init/touch_init.h"
#include "include/ui/home/battery.h"
#include "include/ui/home/rtc_time.h"
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
#define THRESH_DIM_MS        10000   /**< Inactivity before dimming (ms)        */
#define THRESH_SCREEN_OFF_MS 15000   /**< Inactivity before display sleep (ms)  */
#define THRESH_SLEEP_MS      20000   /**< Inactivity before light sleep (ms)    */
#define DIM_BRIGHTNESS_DIV   4       /**< Divisor for dim brightness (25 %)     */

/* ── Display Tier State ──────────────────────────────────────── */
typedef enum {
    DISPLAY_ACTIVE,  /**< Full brightness, all tasks running        */
    DISPLAY_DIM,     /**< Reduced brightness, tasks still running   */
    DISPLAY_OFF,     /**< Display in sleep mode, tasks paused       */
} display_tier_t;

/* ── Module State ─────────────────────────────────────────────── */
static bool              s_sleep_enabled  = true;   /**< User "Always Awake" toggle (inverted) */
static bool              s_screen_on      = true;
static uint32_t          s_lock_mask      = 0;      /**< Active SYS_LOCK_* bitmask             */
static SemaphoreHandle_t s_lock_mux       = NULL;
static volatile int64_t  last_touch_time  = 0;
static volatile bool     s_waking_up      = false;
static display_tier_t    s_display_state  = DISPLAY_ACTIVE;
static uint8_t           s_saved_brightness = 255;  /**< Brightness before dim/off             */

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

/* ── Sleep / Display Policy Accessors ────────────────────────── */

void power_set_sleep_enabled(bool enabled) { s_sleep_enabled = enabled; }
bool power_is_sleep_enabled(void)          { return s_sleep_enabled; }
bool power_is_waking_up(void)              { return s_waking_up; }

bool power_is_display_showing(void)
{
    return (s_display_state == DISPLAY_ACTIVE || s_display_state == DISPLAY_DIM);
}

/* ═══════════════════════════════════════════════════════════════
 *  Display Tier Transitions
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Transition from ACTIVE → DIM.
 * Saves current brightness and reduces it to 25 %.
 */
static void enter_dim(void)
{
    s_saved_brightness = get_lcd_brightness();
    uint8_t dim = s_saved_brightness / DIM_BRIGHTNESS_DIV;
    if (dim < 10) dim = 10;  /* Minimum visible level */
    set_lcd_brightness(dim);
    s_display_state = DISPLAY_DIM;
    ESP_LOGI(TAG, "Display → DIM (%d → %d)", s_saved_brightness, dim);
}

/**
 * @brief Transition from DIM → OFF.
 * Turns backlight off and puts the LCD controller into sleep mode.
 */
static void enter_screen_off(void)
{
    set_lcd_brightness(0);
    esp_lcd_panel_disp_on_off(lcd_panel, false);
    s_display_state = DISPLAY_OFF;
    s_screen_on = false;
    ESP_LOGI(TAG, "Display → OFF");
}
 
/**
 * @brief Restore display from DIM or OFF back to ACTIVE.
 * Called when touch activity is detected in a reduced power tier.
 *
 * Optimised for minimal perceived latency: the screen becomes visible
 * and touch-responsive as fast as possible.  The touch controller
 * recalibration runs AFTER touch events are already accepted — the
 * first few touches may have slightly imprecise coordinates but the
 * device feels instantly responsive.
 */
static void restore_display_active(void)
{
    if (s_display_state == DISPLAY_OFF) {
        /*
         * Brief suppression while the LCD wakes.
         * We clear this flag as soon as the backlight is on.
         */
        s_waking_up = true;

        /* Wake the LCD controller from sleep */
        esp_lcd_panel_disp_on_off(lcd_panel, true);
        s_screen_on = true;

        /* Force a full redraw since the display was off */
        lvgl_port_lock(-1);
        if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);
        lvgl_port_unlock();

        /* Restore backlight — screen is now visible */
        set_lcd_brightness(s_saved_brightness);

        /*
         * Accept touch events NOW.  The touch controller has been
         * running in standby and can usually respond to I2C reads
         * immediately.  The recalibration below improves accuracy
         * but is not required for basic responsiveness.
         */
        s_waking_up = false;

        /* Background recalibration — non-blocking for UX */
        touch_reset_controller();
    } else {
        /* DIM → ACTIVE: just restore brightness */
        set_lcd_brightness(s_saved_brightness);
    }

    s_display_state = DISPLAY_ACTIVE;
    ESP_LOGI(TAG, "Display → ACTIVE (brightness %d)", s_saved_brightness);
}

/* ═══════════════════════════════════════════════════════════════
 *  Main Power Task
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Background task managing the 3-tier display power and light sleep.
 *
 * Tier progression (requires no system locks and sleep enabled):
 *   1. ACTIVE → DIM      after THRESH_DIM_MS of inactivity
 *   2. DIM    → OFF       after THRESH_SCREEN_OFF_MS
 *   3. OFF    → Light Sleep after THRESH_SLEEP_MS
 *
 * Any touch resets the inactivity timer and restores ACTIVE state.
 *
 * Light-sleep wake sequence (optimized for minimal black-screen time):
 *   1. Release GPIO holds, send DISPON, redraw, restore backlight
 *   2. THEN reset touch controller (screen already visible)
 *   3. Flush stale ADC / RTC data for accurate first-frame readings
 */
static void power_task(void *arg)
{
    (void)arg;

    while (1) {
        int64_t now        = esp_timer_get_time();
        int64_t elapsed_us = now - last_touch_time;

        /* Locks and "Always Awake" block all tier transitions */
        bool tier_blocked  = locks_active() || !s_sleep_enabled;
        /* WiFi busy additionally blocks light sleep entry */
        bool sleep_blocked = tier_blocked || wifi_service_is_busy();

        /* ── Restore on new touch activity ── */
        if (s_display_state != DISPLAY_ACTIVE &&
            elapsed_us < (THRESH_DIM_MS * 1000LL))
        {
            restore_display_active();
        }

        /* ── Tier 1: ACTIVE → DIM ── */
        if (!tier_blocked &&
            s_display_state == DISPLAY_ACTIVE &&
            elapsed_us > (THRESH_DIM_MS * 1000LL))
        {
            enter_dim();
        }

        /* ── Tier 2: DIM → OFF ── */
        if (!tier_blocked &&
            s_display_state == DISPLAY_DIM &&
            elapsed_us > (THRESH_SCREEN_OFF_MS * 1000LL))
        {
            enter_screen_off();
        }

        /* ── Tier 3: OFF → Light Sleep ── */
        if (!sleep_blocked &&
            s_display_state == DISPLAY_OFF &&
            elapsed_us > (THRESH_SLEEP_MS * 1000LL))
        {
            ESP_LOGI(TAG, "Entering Light Sleep...");
            s_waking_up = true;

            /* Freeze LVGL frame buffer before sleep */
            lvgl_port_lock(-1);
            lv_refr_now(NULL);
            lvgl_port_unlock();

            /* Hold LCD control GPIOs to prevent floating during sleep */
            gpio_hold_en(LCD_GPIO_RST);
            gpio_hold_en(LCD_GPIO_CS);

            /* ── Enter light sleep ── */
            esp_light_sleep_start();

            /* ── Post-wake recovery (display-first for fast visual response) ── */
            ESP_LOGI(TAG, "Waking up...");

            gpio_hold_dis(LCD_GPIO_RST);
            gpio_hold_dis(LCD_GPIO_CS);

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

            /* Turn display on (DISPON command, not a full reset) */
            esp_lcd_panel_disp_on_off(lcd_panel, true);

            /* Force a full screen redraw */
            if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
            lv_refr_now(NULL);

            last_touch_time = esp_timer_get_time();
            s_screen_on     = true;
            s_display_state = DISPLAY_ACTIVE;
            lvgl_port_unlock();

            /* Restore backlight — screen is now visible to the user */
            set_lcd_brightness(s_saved_brightness);

            /*
             * Screen is visible and drawn — accept touch events NOW.
             * The user can interact immediately.  Touch coordinates
             * may be slightly imprecise for ~100 ms until the
             * controller recalibrates, but the device feels instant.
             */
            s_waking_up = false;

            /*
             * Background housekeeping — runs while the user can
             * already see and touch the screen.
             */
            touch_reset_controller();
            rtc_force_update();
            battery_adc_warmup();
        }

        /*
         * Adaptive polling interval:
         *   ACTIVE → 500 ms  (no urgency, 10 s until dim)
         *   DIM    → 200 ms  (5 s until screen off)
         *   OFF    → 100 ms  (detect touch-to-wake quickly)
         */
        uint32_t poll_ms;
        switch (s_display_state) {
            case DISPLAY_ACTIVE: poll_ms = 500; break;
            case DISPLAY_DIM:    poll_ms = 200; break;
            case DISPLAY_OFF:    poll_ms = 50;  break;  /* Fast polling for instant touch-to-wake */
            default:             poll_ms = 200; break;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
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

    ESP_LOGI(TAG, "Power service initialised  [Dim=%ds  Off=%ds  Sleep=%ds]",
             THRESH_DIM_MS / 1000, THRESH_SCREEN_OFF_MS / 1000, THRESH_SLEEP_MS / 1000);
}
