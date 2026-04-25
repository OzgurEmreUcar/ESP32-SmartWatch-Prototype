/**
 * @file power_init.h
 * @brief System power rail initialization.
 *
 * Configures the SYS_EN GPIO to latch the main power regulator on,
 * preventing the device from shutting down after a momentary button press.
 */
#pragma once

/** @brief GPIO pin controlling the main power enable latch. */
#define SYS_EN 41

/** @brief Configure and latch the system power rail. */
extern void app_power_init(void);