/**
 * @file imu_service.c
 * @brief QMI8658C 6-axis IMU sensor polling service.
 *
 * Reads accelerometer data at 50 Hz in a dedicated FreeRTOS task
 * and computes roll/pitch orientation angles.  When enabled, holds
 * a system power lock to prevent the device from sleeping.
 */

#include "include/services/imu_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "qmi8658c.h"
#include "include/services/power_service.h"
#include <math.h>

static const char *TAG = "IMU_SERVICE";

/* ── I2C Pin Configuration ────────────────────────────────────── */
#define I2C_MASTER_SCL  10
#define I2C_MASTER_SDA  11
#define I2C_MASTER_PORT 0

/** @brief Radians-to-degrees conversion factor. */
#define RAD_TO_DEG 57.295779513082320876798154814105

/* ── Module State ─────────────────────────────────────────────── */
static i2c_dev_t       s_dev         = {0};
static volatile bool   s_imu_enabled = false;
static float           s_latest_roll  = 0.0f;
static float           s_latest_pitch = 0.0f;

/* ═══════════════════════════════════════════════════════════════
 *  IMU Polling Task
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Continuously read accelerometer data and compute orientation.
 *
 * Roll is computed from the Y/Z acceleration components;
 * Pitch uses X vs. the combined Y+Z magnitude for improved stability
 * across all tilt angles.
 */
static void imu_task(void *arg)
{
    qmi8658c_data_t data;
    float roll = 0.0, pitch = 0.0;

    while (1) {
        if (s_imu_enabled) {
            if (qmi8658c_read_data(&s_dev, &data) == ESP_OK) {
                /* Roll: rotation around the X-axis */
                roll = -atan2(data.acc.y, -data.acc.z) * RAD_TO_DEG;

                /* Pitch: rotation around the Y-axis (atan2 with magnitude for stability) */
                pitch = atan2(-data.acc.x,
                              sqrt(data.acc.y * data.acc.y + data.acc.z * data.acc.z)) * RAD_TO_DEG;

                s_latest_roll  = roll;
                s_latest_pitch = pitch;
            } else {
                ESP_LOGE(TAG, "Failed to read IMU data");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));          /* 50 Hz polling rate */
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════════ */

void imu_service_init(void)
{
    ESP_LOGI(TAG, "Initializing IMU service...");

    /* Initialize i2cdev (safe to call multiple times) */
    esp_err_t ret = i2cdev_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2cdev_init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Create I2C device descriptor */
    ret = qmi8658c_init_desc(&s_dev, DEFAUL_QMI8658C_ADDR,
                             I2C_MASTER_PORT, I2C_MASTER_SDA, I2C_MASTER_SCL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "qmi8658c_init_desc failed");
        return;
    }

    /* Configure sensor: dual mode (accel + gyro) */
    qmi8658c_config_t config = {
        .mode       = QMI8658C_MODE_DUAL,
        .acc_scale  = QMI8658C_ACC_SCALE_4G,
        .acc_odr    = QMI8658C_ACC_ODR_1000,
        .gyro_scale = QMI8658C_GYRO_SCALE_512DPS,
        .gyro_odr   = QMI8658C_GYRO_ODR_1000,
    };

    if (qmi8658c_setup(&s_dev, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup QMI8658C");
        return;
    }

    xTaskCreate(imu_task, "imu_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "IMU service initialized successfully");
}

/* ═══════════════════════════════════════════════════════════════
 *  Runtime Control
 * ═══════════════════════════════════════════════════════════════ */

void imu_service_set_enable(bool enable)
{
    s_imu_enabled = enable;
    if (enable) {
        ESP_LOGI(TAG, "IMU streaming enabled");
        power_lock_set(SYS_LOCK_IMU, "IMU_ON");
    } else {
        ESP_LOGI(TAG, "IMU streaming disabled");
        power_lock_clear(SYS_LOCK_IMU, "IMU_OFF");
        s_latest_roll  = 0.0f;
        s_latest_pitch = 0.0f;
    }
}

bool imu_service_get_orientation(float *roll, float *pitch)
{
    if (roll)  *roll  = s_latest_roll;
    if (pitch) *pitch = s_latest_pitch;
    return s_imu_enabled;
}