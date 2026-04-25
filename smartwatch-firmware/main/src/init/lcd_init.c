/**
 * @file lcd_init.c
 * @brief ST7789 LCD display driver initialization and control.
 *
 * Configures the SPI bus, LCD panel IO, ST7789 driver, LEDC-based
 * backlight PWM, and the LVGL display binding.  Also provides
 * sleep/wake helpers used by the power management service.
 */

#include "include/init/lcd_init.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ── Shared Handles ───────────────────────────────────────────── */
esp_lcd_panel_io_handle_t lcd_io    = NULL;
esp_lcd_panel_handle_t    lcd_panel = NULL;
lv_display_t             *lvgl_disp;
static const char        *TAG = "LCD Init";

/* ═══════════════════════════════════════════════════════════════
 *  SPI Bus
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Initialize the SPI bus used by the LCD panel. */
static esp_err_t lcd_spi_bus_init(void)
{
    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num   = LCD_GPIO_SCLK,
        .mosi_io_num   = LCD_GPIO_MOSI,
        .miso_io_num   = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    return spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO);
}

/* ═══════════════════════════════════════════════════════════════
 *  Panel IO
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Attach the LCD panel IO layer over SPI. */
static esp_err_t lcd_panel_io_init(void)
{
    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num       = LCD_GPIO_DC,
        .cs_gpio_num       = LCD_GPIO_CS,
        .pclk_hz           = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits      = LCD_CMD_BITS,
        .lcd_param_bits    = LCD_PARAM_BITS,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io);
}

/* ═══════════════════════════════════════════════════════════════
 *  Panel Driver (ST7789)
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Create, reset, and configure the ST7789 LCD panel driver. */
static esp_err_t lcd_panel_driver_init(void)
{
    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
    };
    esp_err_t ret = esp_lcd_new_panel_st7789(lcd_io, &panel_config, &lcd_panel);
    if (ret != ESP_OK) return ret;

    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_disp_on_off(lcd_panel, true);
    esp_lcd_panel_set_gap(lcd_panel, 0, 20);        /* Vertical offset for 240×280 panel */
    esp_lcd_panel_invert_color(lcd_panel, true);     /* Required for correct ST7789 colors */

    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 *  Backlight Control
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t current_brightness = 255;

void set_lcd_brightness(uint8_t brightness)
{
    current_brightness = brightness;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

uint8_t get_lcd_brightness(void)
{
    return current_brightness;
}

/* ── Sleep / Wake Backlight Helpers ───────────────────────────── */

esp_err_t lcd_enter_sleep(void)
{
    if (lcd_panel) {
        return esp_lcd_panel_disp_sleep(lcd_panel, true);
    }
    return ESP_ERR_INVALID_STATE;
}

/**
 * @brief Full display recovery after light sleep.
 *
 * Performs a hardware reset followed by the ST7789-mandated 120 ms
 * stabilization delay.  The ESP-IDF driver's built-in ~10 ms wait
 * is insufficient after prolonged sleep — the internal oscillator
 * needs the full datasheet-specified period or init commands are
 * corrupted, causing visual artifacts.
 */
esp_err_t lcd_wake_up(void)
{
    if (lcd_panel) {
        esp_lcd_panel_reset(lcd_panel);
        vTaskDelay(pdMS_TO_TICKS(120));             /* ST7789 datasheet: 120 ms post-reset */

        esp_lcd_panel_init(lcd_panel);
        esp_lcd_panel_invert_color(lcd_panel, true);
        esp_lcd_panel_set_gap(lcd_panel, 0, 20);

        return esp_lcd_panel_disp_on_off(lcd_panel, true);
    }
    return ESP_ERR_INVALID_STATE;
}

/** @brief Pull backlight GPIO low before entering light sleep. */
void lcd_backlight_prepare_sleep(void)
{
    gpio_set_level(LCD_GPIO_BL, 0);
}

/** @brief Restore backlight GPIO high after waking from light sleep. */
void lcd_backlight_reinit(void)
{
    gpio_set_level(LCD_GPIO_BL, 1);
}

/* ═══════════════════════════════════════════════════════════════
 *  Backlight LEDC PWM Initialization
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Configure LEDC timer and channel for backlight brightness control. */
static void lcd_backlight_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = LCD_GPIO_BL,
        .duty       = 255,                          /* Default to full brightness */
        .hpoint     = 0
    };
    ledc_channel_config(&ledc_channel);
}

/* ═══════════════════════════════════════════════════════════════
 *  LVGL Display Binding
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Initialize LVGL port and register the LCD as a display device. */
static void lvgl_init_add_display(void)
{
    ESP_LOGD(TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority    = 4,
        .task_stack       = 8192,
        .task_affinity    = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms  = 5
    };
    lvgl_port_init(&lvgl_cfg);

    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size  = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres         = LCD_H_RES,
        .vres         = LCD_V_RES,
        .monochrome   = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma   = true,
            .swap_bytes = true,
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
}

/* ═══════════════════════════════════════════════════════════════
 *  Public Initialization Entry Point
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize the complete LCD subsystem.
 *
 * Calls sub-initializers in order: SPI bus → panel IO → panel driver →
 * LEDC backlight → LVGL display.  On failure, cleans up all resources.
 */
esp_err_t app_lcd_init(void)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_ERROR(lcd_spi_bus_init(), TAG, "SPI init failed");
    ESP_GOTO_ON_ERROR(lcd_panel_io_init(), err, TAG, "New panel IO failed");
    ESP_GOTO_ON_ERROR(lcd_panel_driver_init(), err, TAG, "New panel failed");
    lcd_backlight_init();
    lvgl_init_add_display();
    return ret;

err:
    if (lcd_panel) esp_lcd_panel_del(lcd_panel);
    if (lcd_io) esp_lcd_panel_io_del(lcd_io);
    spi_bus_free(LCD_SPI_NUM);
    return ret;
}