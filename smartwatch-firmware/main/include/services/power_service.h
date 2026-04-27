/**
 * @file power_service.h
 * @brief Tiered Power Management Service — Public API.
 *
 * Provides:
 *   - 3-stage inactivity display control (Active → Dim → Off)
 *   - Coordinated light-sleep entry/exit with hardware sequencing
 *   - System-lock bitmask to block sleep from other subsystems
 *   - Touch-suppression flag during wake recovery
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── Lifecycle ─────────────────────────────────────────────────── */

/**
 * @brief Start the power management task.
 * Must be called after LCD, touch, and LVGL are fully initialized.
 */
void power_service_init(void);

/* ── Inactivity Tracking ───────────────────────────────────────── */

/**
 * @brief Force-reset the hardware inactivity timer.
 * Safe to call from ISR context (IRAM_ATTR).
 */
void power_reset_inactivity(void);

/* ── Sleep Policy ──────────────────────────────────────────────── */

/**
 * @brief Enable or disable the automatic sleep procedure.
 * When disabled ("Always Awake"), the device never enters light sleep
 * and the display never dims or turns off from inactivity.
 */
void power_set_sleep_enabled(bool enabled);

/** @brief Returns true if automatic sleep is enabled. */
bool power_is_sleep_enabled(void);

/* ── System Locks ──────────────────────────────────────────────── */

/** Lock bits — any set bit blocks sleep entry. */
#define SYS_LOCK_WIFI    (1 << 0)
#define SYS_LOCK_CAMERA  (1 << 1)
#define SYS_LOCK_USER    (1 << 2)
#define SYS_LOCK_IMU     (1 << 3)
#define SYS_LOCK_CONNECT (1 << 4)
#define SYS_LOCK_REMOTE  (1 << 5)

/**
 * @brief Set a system lock bit (blocks sleep).
 * @param lock_bit  One of the SYS_LOCK_* defines.
 * @param reason    Human-readable string for log output.
 */
void power_lock_set(uint32_t lock_bit, const char *reason);

/**
 * @brief Clear a system lock bit (allows sleep if no others set).
 * @param lock_bit  One of the SYS_LOCK_* defines.
 * @param reason    Human-readable string for log output.
 */
void power_lock_clear(uint32_t lock_bit, const char *reason);

/* ── Wake Recovery Guard ───────────────────────────────────────── */

/**
 * @brief Returns true while the display is being re-initialized
 * after a light-sleep wakeup.
 *
 * Touch input drivers MUST check this and discard events when true.
 * This prevents LVGL render calls from hitting half-initialized SPI
 * or display hardware, which causes visible flickering/corruption.
 */
bool power_is_waking_up(void);

/* ── Display State Query ───────────────────────────────────────── */

/**
 * @brief Returns true when the display is showing content (ACTIVE or DIM).
 *
 * Background tasks (battery, RTC) should skip UI updates and sensor
 * reads when this returns false to save CPU and I2C power.
 */
bool power_is_display_showing(void);
