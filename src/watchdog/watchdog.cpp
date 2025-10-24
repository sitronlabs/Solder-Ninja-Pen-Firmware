/* Self header */
#include "watchdog.h"

/* Project headers */
#include "errors/errors.h"
#include "log/log.h"

/* Arduino headers */
#include <Arduino.h>

/* RP2040 specific headers */
#include <hardware/watchdog.h>

/* C/C++ libraries */
#include <stdint.h>

/* Local variables */
static uint32_t m_watchdog_pet_last_ms;
static uint32_t m_temperature_read_last_ms;
static bool m_onboard_temperature_safe = true;

/**
 * @brief Initialize the watchdog system
 *
 * Sets up the hardware watchdog timer.
 * The temperature sensor is automatically handled by Arduino-Pico analogReadTemp().
 * Must be called before any watchdog functions are used.
 *
 * @return 0 on success, negative error code on failure
 */
int watchdog_setup(void) {

    /* Configure hardware watchdog timer */
    watchdog_enable(CONFIG_WATCHDOG_TIMEOUT_MS, 1);
    log_i("Hardware watchdog enabled with %dms timeout", CONFIG_WATCHDOG_TIMEOUT_MS);

    /* Return success */
    return 0;
}

/**
 * @brief Check if the onboard temperature is safe
 *
 * Checks if the onboard temperature is within the safe operating temperature range.
 *
 * @return 1 if the onboard temperature is safe, 0 otherwise
 */
int watchdog_onboard_temperature_safe_get(void) {
    return m_onboard_temperature_safe;
}

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
int watchdog_task(void) {

    /* Periodically pet the hardware watchdog */
    if ((millis() - m_watchdog_pet_last_ms) >= CONFIG_WATCHDOG_PET_INTERVAL) {
        m_watchdog_pet_last_ms = millis();
        watchdog_update();
    }

    /* Periodically read the internal temperature reported by the RP2040 */
    if ((millis() - m_temperature_read_last_ms) >= CONFIG_WATCHDOG_TEMPERATURE_ONBOARD_MONITOR_PERIOD) {
        m_temperature_read_last_ms = millis();
        float temperature = analogReadTemp();
        if (temperature > CONFIG_WATCHDOG_TEMPERATURE_ONBOARD_LIMIT) {
            m_onboard_temperature_safe = false;
            log_e("Onboard temperature limit exceeded: %.1f°C > %.1f°C", temperature, (float)CONFIG_WATCHDOG_TEMPERATURE_ONBOARD_LIMIT);
            // TODO: Disable everything?
            // TODO: Report critical error to the user in the interface?
        }
    }

    /* Return success */
    return 0;
}
