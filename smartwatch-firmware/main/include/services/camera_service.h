#ifndef CAMERA_SERVICE_H
#define CAMERA_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Callback for when a frame is decoded.
 * @param frame_data pointer to RGB565 frame data
 * @param width width of frame
 * @param height height of frame
 */
typedef void (*camera_frame_cb_t)(uint16_t *frame_data, size_t width, size_t height);

/**
 * @brief Start the camera stream and processing task.
 * @param frame_cb callback to call when a new frame is decoded.
 */
void camera_service_start(camera_frame_cb_t frame_cb);

/**
 * @brief Stop the camera stream and processing task.
 */
void camera_service_stop(void);

/**
 * @brief Check if camera stream is active.
 * @return true if streaming, false otherwise
 */
bool camera_service_is_active(void);

#endif // CAMERA_SERVICE_H
