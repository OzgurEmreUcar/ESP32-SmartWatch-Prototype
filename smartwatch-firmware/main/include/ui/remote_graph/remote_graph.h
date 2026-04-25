/**
 * @file remote_graph.h
 * @brief Remote sensor graph tab interface.
 *
 * Renders a live-scrolling line chart of sensor data received
 * from an external ESP32 device via WebSocket, with a toggle
 * switch to enable/disable the data stream.
 */
#pragma once

#include "lvgl.h"

/**
 * @brief Build the Remote Graph tab contents.
 * @param parent   Pointer to the LVGL tab object.
 * @param tabview  Pointer to the parent tabview (used by the swipe gesture handler).
 */
void remote_graph_tab(lv_obj_t *parent, lv_obj_t *tabview);
