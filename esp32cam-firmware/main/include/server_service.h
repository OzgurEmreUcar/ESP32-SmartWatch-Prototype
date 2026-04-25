/**
 * @file server_service.h
 * @brief Public interface for the HTTP / WebSocket server.
 *
 * Manages a lightweight HTTPD that serves camera JPEG frames and
 * synthetic sensor data over WebSocket, and provides REST endpoints
 * for controlling the onboard flash LED.
 */

#pragma once

#include "esp_http_server.h"

/**
 * @brief Start the HTTP/WebSocket server and register all endpoints.
 *
 * Launches background streaming tasks and binds the following URIs:
 *  - /ws          – Live camera JPEG stream   (WebSocket, binary).
 *  - /sensor      – Sine-wave sensor data     (WebSocket, text).
 *  - /flash/on    – Turn the flash LED on     (HTTP GET).
 *  - /flash/off   – Turn the flash LED off    (HTTP GET).
 */
void server_service_start(void);

/**
 * @brief Tear down all active WebSocket streaming sessions.
 *
 * Should be called on Wi-Fi disconnect to release stale sockets
 * and allow clean reconnection.
 */
void server_service_teardown_all(void);
