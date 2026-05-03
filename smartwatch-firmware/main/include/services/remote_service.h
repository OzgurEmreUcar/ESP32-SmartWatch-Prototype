/**
 * @file remote_service.h
 * @brief Remote sensor data stream service interface.
 *
 * Connects to an external ESP32 device via WebSocket to receive
 * real-time sensor values for graphing on the local display.
 * Acquires a system power lock while the stream is active.
 */
#pragma once

#include <stdbool.h>

/**
 * @brief Get the latest value received from the remote sensor.
 * @return The most recent floating-point sensor reading.
 */
float remote_service_get_value(void);

/**
 * @brief Enable or disable the WebSocket sensor stream.
 * @param enable  true to connect and start receiving, false to disconnect.
 */
void remote_service_set_enable(bool enable);

/**
 * @brief Check whether the remote sensor stream is currently active.
 * @return true if streaming is enabled.
 */
bool remote_service_is_enabled(void);
