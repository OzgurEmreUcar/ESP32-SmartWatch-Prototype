/**
 * @file camera_init.h
 * @brief Public interface for OV2640 camera initialization.
 *
 * Provides the camera bring-up routine used during the application
 * boot sequence. The implementation targets the AI-Thinker ESP32-CAM
 * board with PSRAM-backed frame buffers.
 */

#pragma once

#include "esp_camera.h"

/**
 * @brief Initialize the OV2640 camera with the AI-Thinker GPIO pinout.
 *
 * Configures the camera peripheral for JPEG capture at 240×240 resolution
 * and applies default sensor tuning parameters.
 *
 * @return ESP_OK on success, or an esp_err_t error code on failure.
 */
esp_err_t camera_init(void);
