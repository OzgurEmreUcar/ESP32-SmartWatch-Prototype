#ifndef CAMERA_STREAM_H
#define CAMERA_STREAM_H

#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Create the Camera Stream tab UI components
 * @param tab The tab object to draw inside
 */
void camera_stream_tab(lv_obj_t *tab);

/**
 * @brief Check if the camera stream is currently active
 * @return true if streaming, false otherwise
 */
bool camera_stream_is_active(void);

#endif // CAMERA_STREAM_H
