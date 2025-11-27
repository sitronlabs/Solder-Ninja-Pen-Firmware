/**
 * @file watchdog.h
 * @brief Watchdog implementation for RP2040 temperature monitoring and hardware watchdog
 *
 * This module provides essential watchdog functionality including:
 * - Internal temperature monitoring of the RP2040 microcontroller
 * - Hardware watchdog timer utilization for heating element safety
 *
 * The purpose of monitoring the onboard temperature is to detect potential chip damage
 * caused by ESD (Electrostatic Discharge) or overvoltage events. When a chip is damaged
 * by such events, the internal circuitry might consume more power and
 * generate more heat than normal operation. By monitoring the internal temperature,
 * we can detect these abnormal conditions and trigger protective measures.
 *
 * The hardware watchdog serves as a critical safety mechanism for the heating element.
 * Since we're driving a heating element, firmware failure could lead to dangerous
 * thermal runaway situations. The hardware watchdog ensures that even firmware
 * failures won't end up causing a fire by automatically resetting the system.
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

/* Config */
#include "../../cfg/config.h"
#ifndef CONFIG_WATCHDOG_TEMPERATURE_ONBOARD_LIMIT
#define CONFIG_WATCHDOG_TEMPERATURE_ONBOARD_LIMIT 85.0f  //!< Maximum safe operating temperature for RP2040 (in degrees Celsius).
#endif
#ifndef CONFIG_WATCHDOG_TEMPERATURE_ONBOARD_MONITOR_PERIOD
#define CONFIG_WATCHDOG_TEMPERATURE_ONBOARD_MONITOR_PERIOD 100  //!< Interval between temperature readings (in milliseconds).
#endif
#ifndef CONFIG_WATCHDOG_TIMEOUT_MS
#define CONFIG_WATCHDOG_TIMEOUT_MS 8000  //!< Hardware watchdog timeout period (in milliseconds).
#endif
#ifndef CONFIG_WATCHDOG_PET_INTERVAL
#define CONFIG_WATCHDOG_PET_INTERVAL 500  //!< Interval between hardware watchdog feeds (in milliseconds).
#endif

/**
 * @brief Initialize the watchdog system
 *
 * Sets up the hardware watchdog timer for heating element safety protection.
 * The temperature sensor is automatically handled by Arduino-Pico analogReadTemp().
 * Must be called before any watchdog functions are used.
 *
 * @return 0 on success, negative error code on failure
 */
int watchdog_setup(void);

/**
 * @brief Check if the onboard temperature is safe
 *
 * Checks if the onboard temperature is within the safe operating temperature range.
 *
 * @return 1 if the onboard temperature is safe, 0 otherwise
 */
int watchdog_onboard_temperature_safe_get(void);

/**
 * @brief Main watchdog task
 *
 * This is the watchdog function that should be called periodically from the main application loop.
 * It handles:
 * - Petting the hardware watchdog
 * - Monitoring the onboard temperature
 *
 * @return 0 on success, negative error code on failure
 */
int watchdog_task(void);

#endif
