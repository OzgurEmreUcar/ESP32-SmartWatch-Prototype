#ifndef FLASH_SERVICE_H
#define FLASH_SERVICE_H

#include <stdbool.h>

/**
 * @brief Set the state of the external ESP32-CAM flash.
 * @param state true to turn on, false to turn off
 */
void flash_service_set_state(bool state);

#endif // FLASH_SERVICE_H
