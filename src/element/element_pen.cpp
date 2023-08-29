/* Self header */
#include "element.h"

/* Project */
#include "../errors/errors.h"
#include "../log/log.h"

/* Arduino libraries */
#include <Arduino.h>
#include <RunningMedian.h>
#include <dac5311.h>
#include <max31855.h>

/* Config */
#include "../../cfg/config.h"
#define CONFIG_TIP_TIME_COOLDOWN 120      //!< Amount of time, in milliseconds, to wait before measuring temperature.
#define CONFIG_TIP_TIME_CYCLE_LIMIT 2000  //!< Maximum amount of time, in milliseconds, that a measuring plus heating cycle should take.

/* Local variables */
static max31855 m_thermocouple_afe;
static dac5311 m_dac;
static RunningMedian m_temperature_filter = RunningMedian(255);
static float m_temperature_measured_c = 0;
static uint32_t m_temperature_measured_timestamp = 0;
static bool m_element_connected = false;
static bool m_heating_enabled = false;
static uint32_t m_timestamp = 0;
static uint32_t m_heating_duration = 0;
static enum {
    STATE_0,
    STATE_1,
    STATE_2,
    STATE_3,
    STATE_ERROR,
} m_sm;

/**
 *
 */
int element_setup(void) {
    int res;

#if R4J

    /* Setup thermocouple analog front end */
    res = m_thermocouple_afe.setup(SPI, 5000000, PA0);
    if (res < 0) {
        log_e("Failed to setup afe!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Setup dac for dc-dc regulation */
    res = m_dac.setup(SPI, 8000000, PA1, 3.3);
    if (res < 0) {
        log_e("Failed to setup dac!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Setup dc-dc */
    // TODO GPIO for enable

#elif R8A

    /* Setup thermocouple analog front end */
    res = m_thermocouple_afe.setup(SPI1, 5000000, 25);
    if (res < 0) {
        log_e("Failed to setup afe!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Setup dac for dc-dc regulation */
    res = m_dac.setup(SPI1, 8000000, 20, 3.3);
    if (res < 0) {
        log_e("Failed to setup dac!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Setup dc-dc */
    // TODO GPIO for enable

#else
#error Invalid hardware version
#endif

    /* Return success */
    return 0;
}

/**
 *
 */
int element_connected_get(void) {
    return m_element_connected;
}

/**
 *
 * @param[out] temperature_c
 * @return 0 in case of success, or a negative error code otherwise, in particular:
 * TODO
 */
int element_temperature_measured_get(float &temperature_c) {
    if (m_element_connected) {
        // TODO As an improvement, return an estimate of the temperature if the last measurement has been performed a while ago
        // use m_temperature_measured_timestamp and a coefficient?
        temperature_c = m_temperature_measured_c;
        return 0;
    } else {
        return -ERROR_GENERIC;  // TODO More specifi error code
    }
}

int element_heating_enable(void) {
    m_heating_enabled = true;
    return 0;
}

int element_heating_disable(void) {
    m_heating_enabled = false;
    return 0;
}

/**
 *
 */
int element_task(void) {
    int res;

    /* Periodically read thermocouple sensor */
    switch (m_sm) {

        case STATE_0: {

            /* Read information from thermocouple afe */
            float temperature_thermocouple_c = 0;
            float temperature_internal_c = 0;
            bool is_shorted_vcc = false;
            bool is_shorted_gnd = false;
            bool is_open = false;
            res = m_thermocouple_afe.read(temperature_thermocouple_c, temperature_internal_c, is_shorted_vcc, is_shorted_gnd, is_open);
            if (res < 0) {
                m_element_connected = false;
                m_sm = STATE_ERROR;
                return 0;
            }

            /* Ensure tip is connected */
            if (is_shorted_vcc || is_open) {
                m_element_connected = false;
                break;
            } else {
                m_element_connected = true;
            }

            /* Compensate and filter temperature
             * The use of a mean filter helps with electronically noisy environments */
            temperature_thermocouple_c = 2.3482 * temperature_thermocouple_c - 47.426;
            m_temperature_filter.add(temperature_thermocouple_c);
            temperature_thermocouple_c = m_temperature_filter.getMedian();

            /* Store temperature */
            m_temperature_measured_c = temperature_thermocouple_c;
            m_temperature_measured_timestamp = millis();

            /* Move on to heating if enabled*/
            if (m_heating_enabled) {
                m_sm = STATE_1;
            }
            break;
        }

        case STATE_1: {  // Start heating if possible

            /* Ask power negotiator how much we can draw */
            // Set m_heating_duration

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_2;
            break;
        }

        case STATE_2: {

            /* If available power has changed, terminate the heating cycle early */
            // TODO

            /* Wait for the end of the heating cycle */
            if ((millis() - m_timestamp) < m_heating_duration) {
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_3;
            break;
        }

        case STATE_3: {  // Wait after heating

            /* Wait for the current to disappear in the tip before measuring again */
            if ((millis() - m_timestamp) < CONFIG_TIP_TIME_COOLDOWN) {
                break;
            }

            /* Move on */
            m_sm = STATE_0;
            break;
        }

        case STATE_ERROR: {
            // Ensure heating is disabled and wait a bit before retrying
            break;
        }
    }

    /* Return success */
    return 0;
}
