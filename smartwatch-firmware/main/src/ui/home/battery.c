/**
 * @file battery.c
 * @brief Battery voltage monitoring via ADC.
 *
 * Reads the LiPo battery voltage through a resistive divider
 * connected to ADC1, converts the raw reading to actual voltage,
 * computes a linear charge percentage, and updates an LVGL label.
 *
 * Uses multi-sample averaging per-read for display stability.
 * Provides an aggressive warmup routine to flush stale ADC data
 * after light sleep.
 */

#include "include/ui/home/battery.h"
#include "include/services/power_service.h"

static const char *TAG = "battery";

/* ── Configuration ───────────────────────────────────────────── */
#define ADC_SAMPLE_COUNT     16  /**< Samples per reading (averaged)          */
#define ADC_WARMUP_DISCARD   30  /**< Throwaway reads after sleep             */
#define ADC_WARMUP_DELAY_MS  5   /**< Delay between warmup discards (ms)      */

/* ── Module State ─────────────────────────────────────────────── */
static adc_oneshot_unit_handle_t adc_handle;
static lv_obj_t *battery_label;

/* ═══════════════════════════════════════════════════════════════
 *  ADC Initialization
 * ═══════════════════════════════════════════════════════════════ */

void battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten   = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    ESP_LOGI(TAG, "ADC initialized on channel %d", ADC_CHANNEL);
}

/* ═══════════════════════════════════════════════════════════════
 *  Voltage Reading
 * ═══════════════════════════════════════════════════════════════ */

static float s_ema_voltage = -1.0f;

static float read_battery_voltage(void)
{
    int raw_sum = 0;
    int valid_samples = 0;

    for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
        int raw;
        if (adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw) == ESP_OK) {
            raw_sum += raw;
            valid_samples++;
        }
    }

    if (valid_samples == 0) {
        return -1.0f; // Return error indicator
    }

    float raw_avg = (float)raw_sum / valid_samples;
    float adc_voltage = (raw_avg / 4095.0f) * VREF;
    return adc_voltage * ((R1 + R2) / R2);
}

/**
 * @brief Compute battery percentage from raw voltage and update label.
 */
static void update_battery_label(void)
{
    float voltage = read_battery_voltage();
    if (voltage < 0.0f) {
        return; // Ignore read errors, wait for next cycle
    }

    /* Apply Exponential Moving Average (EMA) to smooth out fluctuations */
    if (s_ema_voltage < 0.0f) {
        s_ema_voltage = voltage; // Initialize EMA on first successful read
    } else {
        s_ema_voltage = (s_ema_voltage * 0.8f) + (voltage * 0.2f);
    }
    
    voltage = s_ema_voltage;

    /* Clamp to valid range */
    if (voltage > BATTERY_VOLT_FULL)  voltage = BATTERY_VOLT_FULL;
    if (voltage < BATTERY_VOLT_EMPTY) voltage = BATTERY_VOLT_EMPTY;

    float pct = ((voltage - BATTERY_VOLT_EMPTY) /
                 (BATTERY_VOLT_FULL - BATTERY_VOLT_EMPTY)) * 100.0f;

    /* Display absolute raw voltage and percentage */
    char buf[24];
    snprintf(buf, sizeof(buf), "%.1fV  %.0f%%", voltage, pct);

    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        lv_label_set_text(battery_label, buf);
        lvgl_port_unlock();
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Periodic Update Task
 * ═══════════════════════════════════════════════════════════════ */

/** @brief FreeRTOS task – reads battery every 5 s and updates the label. */
void battery_task(void *arg)
{
    while (1) {
        /* Skip reads when display is off to save ADC + CPU power */
        if (power_is_display_showing()) {
            update_battery_label();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void battery_set_label(lv_obj_t *label)
{
    battery_label = label;
}

/* ═══════════════════════════════════════════════════════════════
 *  Post-Sleep ADC Warmup
 * ═══════════════════════════════════════════════════════════════ */

void battery_adc_warmup(void)
{
    /* Flush stale internal capacitor charge after sleep */
    for (int i = 0; i < ADC_WARMUP_DISCARD; i++) {
        int raw;
        adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
        vTaskDelay(pdMS_TO_TICKS(ADC_WARMUP_DELAY_MS));
    }

    update_battery_label();
}
