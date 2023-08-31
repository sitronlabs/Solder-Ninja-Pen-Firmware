/* Self header */
#include "element.h"

/* Project */
#include "../errors/errors.h"
#include "../log/log.h"
#include "app/power/power.h"

/* Arduino libraries */
#include <Arduino.h>
#include <PID_v1.h>
#include <RunningMedian.h>
#include <max31855.h>

/* Config */
#include "../../cfg/config.h"
#define CONFIG_TIP_TIME_COOLDOWN 120      //!< Amount of time, in milliseconds, to wait before measuring temperature.
#define CONFIG_TIP_TIME_CYCLE_LIMIT 1000  //!< Maximum amount of time, in milliseconds, that a measuring plus heating cycle should take.

/* Peripherals */
static max31855 m_thermocouple_afe;

/* Filter library */
static RunningMedian m_temperature_filter = RunningMedian(255);

/* PID library */
static double m_pid_input = 0;
static double m_pid_target = 0;
static double m_pid_output = 0;
// static PID m_pid(&m_pid_input, &m_pid_output, &m_pid_target, 1, 0, 0, P_ON_M, DIRECT);
static PID m_pid(&m_pid_input, &m_pid_output, &m_pid_target, 1, 0, 0, P_ON_E, DIRECT);

/* Other local variables */
static float m_temperature_measured_c;
static float m_temperature_target_c;
static uint32_t m_temperature_measured_timestamp;
static bool m_element_connected;
static bool m_heating_enabled;
static uint32_t m_timestamp;
static uint32_t m_timestamp_last_cycle;
static uint32_t m_heating_duration;

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

    /* Setup dc-dc */
    pinMode(PC14, OUTPUT);
    digitalWrite(PC14, LOW);

#elif R8A

    /* Setup thermocouple analog front end */
    res = m_thermocouple_afe.setup(SPI1, 5000000, 25);
    if (res < 0) {
        log_e("Failed to setup afe!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Setup dc-dc */
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);

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

/**
 * @brief
 * @param temperature_c
 * @return
 */
int element_temperature_target_set(const float temperature_c) {
    // TODO Maybe some range checking
    m_temperature_target_c = temperature_c;
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int element_heating_enable(void) {
    m_heating_enabled = true;
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int element_heating_disable(void) {
    m_heating_enabled = false;
    return 0;
}

/**
 *
 */
int element_task(void) {
    int res;

    /* State machine */
    static enum {
        STATE_0,
        STATE_1,
        STATE_2,
        STATE_3,
        STATE_ERROR,
    } m_sm;
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

            // TODO Mean of a few measurements

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
                m_pid.SetMode(AUTOMATIC);
                m_sm = STATE_1;
            } else {
                m_pid.SetMode(MANUAL);
            }
            break;
        }

        case STATE_1: {  // Start heating if possible

            /* Ask negotiator how much power we can draw */
            float power_limit = 0;
            res = power_negotiated_power_limit_get(&power_limit);
            if (res < 0) {
                m_sm = STATE_0;
                break;
            }

            /* Compute maximum amount of energy we can use this cycle */
            float energy_limit = ((CONFIG_TIP_TIME_CYCLE_LIMIT - CONFIG_TIP_TIME_COOLDOWN) / 1000.0) * power_limit;  // 10.56 J

            /* Compute amount of energy needed this cycle */
            m_pid_input = m_temperature_measured_c;
            m_pid_target = m_temperature_target_c;
            m_pid.SetSampleTime((millis() - m_timestamp_last_cycle) - 1);
            m_pid.SetOutputLimits(0, energy_limit);
            m_pid.Compute();

            /* Convert back energy into time */
            m_heating_duration = 1000.0 * (m_pid_output / power_limit);

            /* Log */
            log_t("Pid %f / %f -> %f J (%d ms)", m_pid_input, m_pid_target, m_pid_output, m_heating_duration);

/* Turn on dc-dc */
#if R4J
            digitalWrite(PC14, HIGH);
#elif R8A
            digitalWrite(3, HIGH);
#endif

            /* Move on */
            m_timestamp_last_cycle = millis();
            m_timestamp = millis();
            m_sm = STATE_2;
            break;
        }

        case STATE_2: {

            /* Wait for the end of the heating cycle */
            if ((millis() - m_timestamp) < m_heating_duration) {
                break;
            }

            /* Turn off dc-dc */
#if R4J
            digitalWrite(PC14, LOW);
#elif R8A
            digitalWrite(3, LOW);
#endif

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
