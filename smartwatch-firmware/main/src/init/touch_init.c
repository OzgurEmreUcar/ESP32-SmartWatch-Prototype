/**
 * @file touch_init.c
 * @brief CST816S capacitive touch controller initialization.
 *
 * Sets up the shared I2C bus via the i2cdev component, initializes
 * the CST816S touch panel driver, configures GPIO wakeup for
 * light-sleep recovery, and registers the LVGL input device.
 */

#include "include/init/touch_init.h"
#include "include/init/lcd_init.h"
#include "include/services/power_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_sleep.h"
#include "i2cdev.h"

static const char *TAG = "Touch Init";

/* ── Module State ─────────────────────────────────────────────── */
esp_lcd_touch_handle_t   tp = NULL;
static SemaphoreHandle_t touch_sema = NULL;
static i2c_master_bus_handle_t i2c_bus_handle;

/** @brief Return the shared I2C bus handle for other peripherals (RTC, etc.). */
i2c_master_bus_handle_t app_get_i2c_bus(void)
{
    return i2c_bus_handle;
}

/* ═══════════════════════════════════════════════════════════════
 *  Touch Interrupt Handler
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief GPIO ISR for touch interrupt (IRAM-resident).
 *
 * Resets the power-service inactivity timer and signals the
 * touch semaphore so the LVGL read callback can process the event.
 */
static void IRAM_ATTR touch_isr_handler(void *arg)
{
    power_reset_inactivity();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (touch_sema) {
        xSemaphoreGiveFromISR(touch_sema, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  LVGL Input Read Callback
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief LVGL input-device read callback for the touch panel.
 *
 * Suppresses all touch events during the wakeup recovery window
 * to prevent LVGL from issuing SPI draw calls into a
 * half-initialized display pipeline (see power_is_waking_up()).
 */
void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch_handle = lv_indev_get_user_data(indev);
    static bool is_pressed = false;

    /* Discard events while the display is recovering from sleep */
    if (power_is_waking_up()) {
        data->state = LV_INDEV_STATE_RELEASED;
        is_pressed = false;
        return;
    }

    if (xSemaphoreTake(touch_sema, 0) == pdTRUE || is_pressed) {
        esp_lcd_touch_point_data_t point_data;
        uint8_t touch_cnt = 0;

        esp_err_t ret = esp_lcd_touch_read_data(touch_handle);

        if (ret == ESP_OK) {
            ret = esp_lcd_touch_get_data(touch_handle, &point_data, &touch_cnt, 1);

            if (ret == ESP_OK && touch_cnt > 0) {
                data->point.x = point_data.x;
                data->point.y = point_data.y;
                data->state   = LV_INDEV_STATE_PRESSED;
                is_pressed = true;
            } else {
                data->state = LV_INDEV_STATE_RELEASED;
                is_pressed  = false;
            }
        } else {
            data->state = is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Hardware Initialization
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize the CST816S touch controller and I2C bus.
 *
 * Sequence:
 *   1. Hardware reset of the CST816S
 *   2. i2cdev shared bus initialization
 *   3. LCD panel IO attachment over I2C
 *   4. CST816S driver instantiation
 *   5. Semaphore + ISR configuration (with GPIO wakeup for light sleep)
 */
void app_touch_init(void)
{
    ESP_LOGI(TAG, "Initialize Hardware Pins");

    /* Step 1 – Manual reset (CST816S requires ≥ 5 ms low pulse) */
    gpio_config_t rst_gpio_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_TOUCH_RST),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_gpio_conf);
    gpio_set_level(PIN_NUM_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_NUM_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Step 2 – Shared I2C bus via i2cdev component */
    ESP_ERROR_CHECK(i2cdev_init());

    static i2c_dev_t dummy_dev = {
        .port = TOUCH_HOST,
        .addr = 0x15,                       /* CST816S default address */
        .cfg  = {
            .sda_io_num    = PIN_NUM_TOUCH_SDA,
            .scl_io_num    = PIN_NUM_TOUCH_SCL,
            .sda_pullup_en = true,
            .scl_pullup_en = true,
            .master.clk_speed = 400000,
        }
    };
    ESP_ERROR_CHECK(i2c_dev_create_mutex(&dummy_dev));
    i2c_dev_check_present(&dummy_dev);      /* Triggers internal bus creation */

    ESP_ERROR_CHECK(i2cdev_get_shared_handle(TOUCH_HOST, (void **)&i2c_bus_handle));

    /* Step 3 – Panel IO over I2C */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    tp_io_config.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_handle, &tp_io_config, &tp_io_handle));

    /* Step 4 – CST816S touch driver */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max        = LCD_H_RES,
        .y_max        = LCD_V_RES,
        .rst_gpio_num = PIN_NUM_TOUCH_RST,
        .int_gpio_num = PIN_NUM_TOUCH_INT,
        .levels       = { .reset = 0, .interrupt = 0 },
    };

    ESP_LOGI(TAG, "Initialize CST816S touch driver");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp));

    /* Step 5 – Interrupt and semaphore */
    touch_sema = xSemaphoreCreateBinary();

    gpio_config_t int_io_conf = {
        .intr_type    = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = (1ULL << PIN_NUM_TOUCH_INT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = 1,
    };
    gpio_config(&int_io_conf);

    /* Enable GPIO-level wakeup so a touch can exit light sleep */
    gpio_wakeup_enable(PIN_NUM_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_NUM_TOUCH_INT, touch_isr_handler, NULL);
}

/* ═══════════════════════════════════════════════════════════════
 *  LVGL Input Device Registration
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Create and register the LVGL pointer input device. */
void app_lvgl_touch_init(void)
{
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_touch);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_display(indev, lvgl_disp);
}

/* ═══════════════════════════════════════════════════════════════
 *  Post-Sleep Touch Controller Reset
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Hardware-reset the CST816S to force capacitive recalibration.
 *
 * During a long light-sleep the touch controller's internal capacitive
 * baseline drifts (finger oils evaporate, temperature changes, etc.).
 * On wakeup the stale baseline causes inaccurate touch coordinates
 * until the controller slowly self-corrects — typically 5-10 seconds.
 *
 * A clean reset pulse forces the CST816S to re-run its power-on
 * calibration sequence (~50 ms), after which touch accuracy is
 * immediately restored.  We also drain any residual interrupt event
 * so the first real touch isn't lost.
 */
void touch_reset_controller(void)
{
    /* Pulse RST low for ≥ 5 ms (CST816S datasheet minimum) */
    gpio_set_level(PIN_NUM_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_NUM_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));   /* Wait for calibration to finish */

    /* Flush any stale touch data generated during reset */
    if (tp) {
        esp_lcd_touch_read_data(tp);
    }

    ESP_LOGI(TAG, "CST816S reset complete — baseline recalibrated");
}