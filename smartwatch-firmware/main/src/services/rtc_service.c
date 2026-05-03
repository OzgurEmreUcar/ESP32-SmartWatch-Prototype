#include "include/services/rtc_service.h"
#include "include/services/power_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RTC_SERVICE";
#define PCF85063_ADDR 0x51

/* ── Module State ─────────────────────────────────────────────── */
static i2c_master_dev_handle_t rtc_dev;
static void (*s_rtc_cb)(uint8_t h, uint8_t m, uint8_t s, bool valid) = NULL;

/* ═══════════════════════════════════════════════════════════════
 *  BCD Conversion Helpers
 * ═══════════════════════════════════════════════════════════════ */

static uint8_t bcd_to_dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0F);
}

static uint8_t dec_to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static esp_err_t rtc_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(rtc_dev, buf, 2, 100);
}

/* ═══════════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t rtc_service_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PCF85063_ADDR,
        .scl_speed_hz    = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &rtc_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add PCF85063: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Clear STOP bit in Control_1 (reg 0x00) to start oscillator */
    ret = rtc_write_reg(0x00, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start oscillator: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Check OS flag in seconds register (bit 7 of reg 0x04) */
    uint8_t reg = 0x04;
    uint8_t sec;
    ret = i2c_master_transmit_receive(rtc_dev, &reg, 1, &sec, 1, 100);
    if (ret == ESP_OK && (sec & 0x80)) {
        ESP_LOGW(TAG, "OS flag set — clock was stopped, time is invalid");
        rtc_write_reg(0x04, sec & 0x7F);    /* Clear OS flag */
    }

    ESP_LOGI(TAG, "PCF85063 initialized, oscillator running");
    return ESP_OK;
}

void rtc_service_register_cb(void (*cb)(uint8_t h, uint8_t m, uint8_t s, bool valid))
{
    s_rtc_cb = cb;
}

/* ═══════════════════════════════════════════════════════════════
 *  Time Read / Write
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t rtc_service_set_time(uint8_t hours, uint8_t minutes, uint8_t seconds)
{
    uint8_t buf[4] = {
        0x04,                        /* Starting register address            */
        dec_to_bcd(seconds),         /* Seconds (OS flag cleared by default) */
        dec_to_bcd(minutes),
        dec_to_bcd(hours),
    };
    esp_err_t ret = i2c_master_transmit(rtc_dev, buf, 4, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set time: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Time set to %02d:%02d:%02d", hours, minutes, seconds);
    }
    return ret;
}

static esp_err_t rtc_read_time(uint8_t *h, uint8_t *m, uint8_t *s)
{
    uint8_t reg = 0x04;
    uint8_t data[3];

    esp_err_t ret = i2c_master_transmit_receive(rtc_dev, &reg, 1, data, 3, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    *s = bcd_to_dec(data[0] & 0x7F);   /* Mask OS flag   */
    *m = bcd_to_dec(data[1] & 0x7F);
    *h = bcd_to_dec(data[2] & 0x3F);   /* Mask 12/24 bit */
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 *  Periodic Task & Updates
 * ═══════════════════════════════════════════════════════════════ */

static void update_rtc_logic(void)
{
    uint8_t h, m, s;
    if (rtc_read_time(&h, &m, &s) == ESP_OK) {
        if (s_rtc_cb) s_rtc_cb(h, m, s, true);
    } else {
        if (s_rtc_cb) s_rtc_cb(0, 0, 0, false);
    }
}

void rtc_service_task(void *arg)
{
    while (1) {
        /* Skip I2C reads when display is off to save power */
        if (power_is_display_showing()) {
            update_rtc_logic();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void rtc_service_force_update(void)
{
    update_rtc_logic();
}
