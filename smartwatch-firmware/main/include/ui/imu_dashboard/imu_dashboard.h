/**
 * @file imu_dashboard.h
 * @brief IMU data visualization tab interface.
 *
 * Renders a real-time dashboard with symmetrical bar widgets
 * displaying roll and pitch orientation from the QMI8658C IMU,
 * along with an on/off toggle switch.
 */
#pragma once

#include "lvgl.h"

/**
 * @brief Build the IMU Dashboard tab contents inside the given parent.
 * @param parent  Pointer to the LVGL tab object created by the tabview.
 */
void imu_dashboard(lv_obj_t *parent);
