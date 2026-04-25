/**
 * @file lcd_init.h
 * @brief LCD display initialization and control interface.
 *
 * Provides hardware configuration for the ST7789 240×280 TFT display
 * connected via SPI, along with backlight control and sleep/wake APIs.
 */
#pragma once

#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"

/* ── Display Resolution ───────────────────────────────────────── */
#define LCD_H_RES (240)
#define LCD_V_RES (280)

/* ── SPI Bus Configuration ────────────────────────────────────── */
#define LCD_SPI_NUM        (SPI2_HOST)
#define LCD_PIXEL_CLK_HZ   (40 * 1000 * 1000)
#define LCD_CMD_BITS       (8)
#define LCD_PARAM_BITS     (8)
#define LCD_COLOR_SPACE    (ESP_LCD_COLOR_SPACE_RGB)
#define LCD_BITS_PER_PIXEL (16)
#define LCD_DRAW_BUFF_DOUBLE (1)
#define LCD_DRAW_BUFF_HEIGHT (50)
#define LCD_BL_ON_LEVEL    (1)

/* ── GPIO Pin Assignments ─────────────────────────────────────── */
#define LCD_GPIO_SCLK (GPIO_NUM_6)
#define LCD_GPIO_MOSI (GPIO_NUM_7)
#define LCD_GPIO_RST  (GPIO_NUM_8)
#define LCD_GPIO_DC   (GPIO_NUM_4)
#define LCD_GPIO_CS   (GPIO_NUM_5)
#define LCD_GPIO_BL   (GPIO_NUM_15)

/* ── Shared Handles (defined in lcd_init.c) ───────────────────── */
extern esp_lcd_panel_io_handle_t lcd_io;
extern esp_lcd_panel_handle_t    lcd_panel;

/* ── Public API ───────────────────────────────────────────────── */

/** @brief Initialize SPI bus, LCD panel, backlight, and LVGL display. */
extern esp_err_t app_lcd_init(void);

/** @brief Put the LCD controller into hardware sleep mode. */
extern esp_err_t lcd_enter_sleep(void);

/** @brief Wake the LCD controller and restore panel configuration. */
extern esp_err_t lcd_wake_up(void);

/** @brief Set backlight brightness (0–255, PWM via LEDC). */
extern void set_lcd_brightness(uint8_t brightness);

/** @brief Get current backlight brightness level. */
extern uint8_t get_lcd_brightness(void);

/** @brief Safely turn off backlight before entering light sleep. */
extern void lcd_backlight_prepare_sleep(void);

/** @brief Restore backlight output after waking from light sleep. */
extern void lcd_backlight_reinit(void);
