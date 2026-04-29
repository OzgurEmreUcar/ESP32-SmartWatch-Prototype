/**
 * @file rtc_time.c
 * @brief PCF85063 real-time clock driver and display task.
 *
 * Communicates with the PCF85063 RTC over I2C to read/write time
 * in BCD format.  A FreeRTOS task reads the clock every second
 * and updates the LVGL label on the home screen.
 */

#include "include/ui/home/rtc_time.h"
#include "include/services/power_service.h"

static const char *TAG = "rtc";

/* ── Module State ─────────────────────────────────────────────── */
static i2c_master_dev_handle_t rtc_dev;
static lv_obj_t *time_label;

/* ═══════════════════════════════════════════════════════════════
 *  BCD Conversion Helpers
 * ═══════════════════════════════════════════════════════════════ */

/** @brief Convert a BCD-encoded byte to decimal. */
static uint8_t bcd_to_dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0F);
}

/** @brief Convert a decimal value to BCD encoding. */
static uint8_t dec_to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

/** @brief Write a single byte to a PCF85063 register. */
static esp_err_t rtc_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(rtc_dev, buf, 2, 100);
}

/* ═══════════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize the PCF85063 RTC on the shared I2C bus.
 *
 * Clears the STOP bit to start the oscillator and checks the
 * OS (oscillator-stopped) flag to detect whether the time was
 * lost due to a battery disconnect.
 */
esp_err_t rtc_time_init(i2c_master_bus_handle_t bus)
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

/* ═══════════════════════════════════════════════════════════════
 *  Time Read / Write
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t rtc_set_time(uint8_t hours, uint8_t minutes, uint8_t seconds)
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

void rtc_set_label(lv_obj_t *label)
{
    time_label = label;
}

/**
 * @brief Read hours, minutes, and seconds from the RTC.
 *
 * Registers 0x04–0x06 are read in a single burst; each byte
 * is masked to strip the OS flag and 12/24-hour mode bit.
 */
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
 *  Display Update Task
 * ═══════════════════════════════════════════════════════════════ */

/** @brief FreeRTOS task – reads the RTC every second and refreshes the clock label. */
void rtc_time_task(void *arg)
{
    uint8_t h, m, s;
    char buf[12];

    while (1) {
        /* Skip I2C reads when display is off to save power */
        if (power_is_display_showing()) {
            if (rtc_read_time(&h, &m, &s) == ESP_OK) {
                snprintf(buf, sizeof(buf), "%02d\n%02d\n%02d", h, m, s);
            } else {
                snprintf(buf, sizeof(buf), "--\n--\n--");
            }

            lvgl_port_lock(0);
            if (time_label) {
                lv_label_set_text(time_label, buf);
            }
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void rtc_force_update(void)
{
    uint8_t h, m, s;
    char buf[12];

    if (rtc_read_time(&h, &m, &s) == ESP_OK) {
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

    ESP_LOGI(TAG, "RTC forced update: %02d:%02d:%02d", h, m, s);
}
