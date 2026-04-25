/**
 * @file imu_service.h
 * @brief QMI8658C IMU sensor service interface.
 *
 * Manages the 6-axis IMU (accelerometer + gyroscope) via I2C,
 * computing roll/pitch orientation at 50 Hz in a dedicated task.
 */
#pragma once

#include <stdbool.h>

/** @brief Initialize I2C descriptor, configure QMI8658C, and start polling task. */
void imu_service_init(void);

/**
 * @brief Enable or disable continuous IMU data acquisition.
 * @param enable  true to start streaming, false to stop.
 *
 * When enabled, acquires a system power lock to prevent sleep.
 */
void imu_service_set_enable(bool enable);

/**
 * @brief Retrieve the latest computed roll and pitch angles.
 * @param[out] roll   Pointer to receive roll angle in degrees (may be NULL).
 * @param[out] pitch  Pointer to receive pitch angle in degrees (may be NULL).
 * @return true if the IMU is actively streaming data.
 */
bool imu_service_get_orientation(float *roll, float *pitch);
