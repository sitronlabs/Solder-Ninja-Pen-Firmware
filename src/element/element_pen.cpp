/* Self header */
#include "element.h"

/* Project */
#include "errors/errors.h"
#include "log/log.h"
#include "power/power.h"

/* Arduino libraries */
#include <Arduino.h>
#include <PID_v1.h>
#include <RunningMedian.h>
#include <max31855.h>

/* Config */
#include "../cfg/config.h"

/* Peripherals */
static max31855 m_thermocouple_afe;

/* Filter library */
static RunningMedian m_temperature_filter(10);
static uint32_t m_temperature_filter_last_addition;

/* PID library
 * As a base starting point for the coefficients, we can make the following approximation
 * For a mass of 2g of steel, a change of 1°C requires 0.93J
 * @see https://www.omnicalculator.com/physics/specific-heat */
static double m_pid_input = 0;
static double m_pid_target = 0;
static double m_pid_output = 0;
static PID m_pid(&m_pid_input, &m_pid_output, &m_pid_target, CONFIG_TIP_COEFFICIENT_C *CONFIG_TIP_COEFFICIENT_M, 0, 0, P_ON_E, DIRECT);

/* Other local variables */
static float m_power_limit;
static float m_temperature_measured_c;
static float m_temperature_target_c;
static bool m_element_connected;
static bool m_heating_enabled;
static uint32_t m_timestamp_tip_connected;
static uint32_t m_timestamp_cycle_start;
static uint32_t m_timestamp_temperature_read;
static uint32_t m_timestamp_pid_computed;
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
#elif R8A

    /* Setup thermocouple analog front end */
    res = m_thermocouple_afe.setup(SPI1, 5000000, 25);
    if (res < 0) {
        log_e("Failed to setup afe!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }
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
 */
int element_temperature_measured_get(float &temperature_c) {

    /* If tip is not connected, return an error */
    if (m_element_connected != true) {
        return -ERROR_GENERIC;  // TODO More specific error code
    }

    /* Return success */
    temperature_c = m_temperature_filter.getMedian();
    return 0;
}

/**
 * @brief
 * @param temperature_c
 * @return
 */
int element_temperature_target_set(const float temperature_c) {

    /* Ensure temperature is within bounds */
    if ((temperature_c <= CONFIG_CONTROLLER_TARGET_MIN) || (temperature_c > CONFIG_CONTROLLER_TARGET_MAX_BOOST)) {
        return -1;
    }

    /* Store new temperature */
    m_temperature_target_c = temperature_c;

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int element_heating_enable(void) {

    // TODO Maybe decline if no tip is connected?

    /* Save request */
    m_heating_enabled = true;

    /* Return success */
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
        STATE_0_DISCONNECTED,
        STATE_1_DEBOUNCE,
        STATE_2_DISABLED,
        STATE_3_START,
        STATE_4_READ,
        STATE_5_HEAT,
        STATE_ERROR,
    } m_sm;
    switch (m_sm) {

        case STATE_0_DISCONNECTED: {

            /* Ensure pid is disabled */
            m_pid.SetMode(MANUAL);

            /* Report tip as disconnected for now */
            m_element_connected = false;

            /* Read information from thermocouple afe */
            float temperature_thermocouple_c = 0;
            float temperature_internal_c = 0;
            bool is_shorted_vcc = false;
            bool is_shorted_gnd = false;
            bool is_open = false;
            res = m_thermocouple_afe.read(temperature_thermocouple_c, temperature_internal_c, is_shorted_vcc, is_shorted_gnd, is_open);
            if (res < 0) {
                m_sm = STATE_ERROR;
                break;
            }

            /* Wait for tip to be connected */
            if ((is_shorted_vcc == true) || (is_open == true)) {
                m_sm = STATE_0_DISCONNECTED;
                break;
            }

            /* Move on */
            m_timestamp_tip_connected = millis();
            m_sm = STATE_1_DEBOUNCE;
            break;
        }

        case STATE_1_DEBOUNCE: {

            /* Wait a little bit after insertion */
            if ((millis() - m_timestamp_tip_connected) < CONFIG_TIP_DEBOUNCE_TIME) {
                break;
            }

            /* Move on */
            m_sm = STATE_2_DISABLED;
            break;
        }

        case STATE_2_DISABLED: {

            /* Ensure pid is disabled */
            m_pid.SetMode(MANUAL);

            /* Move on */
            m_sm = STATE_3_START;
            break;
        }

        case STATE_3_START: {

            /* Remember cycle start time */
            m_timestamp_cycle_start = millis();

            /* Move on */
            m_sm = STATE_4_READ;
            break;
        }

        case STATE_4_READ: {

            /* Don't read more often than half the sample rate of the afe */
            if ((millis() - m_timestamp_temperature_read) < CONFIG_TIP_READ_PERIOD) {
                break;
            }

            /* Read information from thermocouple afe */
            float temperature_thermocouple_c = 0;
            float temperature_internal_c = 0;
            bool is_shorted_vcc = false;
            bool is_shorted_gnd = false;
            bool is_open = false;
            res = m_thermocouple_afe.read(temperature_thermocouple_c, temperature_internal_c, is_shorted_vcc, is_shorted_gnd, is_open);
            if (res < 0) {
                m_sm = STATE_ERROR;
                return 0;
            }

            /* Ensure tip is connected */
            if ((is_shorted_vcc == true) || (is_open == true)) {
                m_sm = STATE_0_DISCONNECTED;
                break;
            }

            /* Compensate temperature,
             * probably because we got the type of thermocouple wrong */
            temperature_thermocouple_c = 2.3482 * temperature_thermocouple_c - 47.426;

            /* Discard abnormal values, because temperature readings:
             * 1) are not accurate right after heating,
             * 1) can be affected by electrically noisy environments
             * Note, this could be improved by rather looking at abnormal variations (sudden jumps from the running average) */
            if ((temperature_thermocouple_c < -50) || (temperature_thermocouple_c > 500)) {
                // log_t("Read %4.0f invalid", temperature_thermocouple_c);

                /* If no valid temperature has been read for a long time, present the tip is disconnected */
                if ((millis() - m_timestamp_temperature_read) >= CONFIG_TIP_READ_TIMEOUT) {
                    log_w("No valid temperature read for a while.");
                    m_sm = STATE_0_DISCONNECTED;
                }

                break;
            }

            /* Improve low temperature accuracy by averaging with the internal temperature */
            if ((temperature_thermocouple_c < 30) && (temperature_thermocouple_c < temperature_internal_c)) {
                temperature_thermocouple_c = (temperature_thermocouple_c + temperature_internal_c) / 2.0;
            }

            // /* To improve accuracy of estimated temperature when heating, we could compute the dtemperature/denergy
            //  * But for now the default one seems to work pretty well, so let's not overcomplicate things. */
            // if (m_dt_de_available == false ) {
            // }

            /* Report tip as connected */
            m_element_connected = true;

            /* Save the value we just read */
            m_timestamp_temperature_read = millis();
            m_temperature_measured_c = temperature_thermocouple_c;
            m_temperature_filter.add(m_temperature_measured_c);
            m_temperature_filter_last_addition = millis();

            // /* Log */
            // log_t("Read %4.0f valid", temperature_thermocouple_c);

            /* If heating is not enabled, restart a cycle */
            if (m_heating_enabled != true) {
                m_sm = STATE_2_DISABLED;
                break;
            }

            /* Ask usb power negotiator how much power we are allowed to draw */
            res = power_negotiated_power_limit_get(m_power_limit);
            if (res < 0) {
                break;
            }

            /* Ensure we have still time to heat in this cycle
             * which might no ne the case if we had to retry reading the temperature too many times */
            if ((millis() - m_timestamp_cycle_start) >= CONFIG_TIP_CYCLE_TIME_LIMIT) {
                m_sm = STATE_3_START;
                break;
            }

            /* Compute maximum amount of energy we can use this cycle */
            float energy_limit = ((CONFIG_TIP_CYCLE_TIME_LIMIT - (millis() - m_timestamp_cycle_start)) / 1000.0) * m_power_limit;

            /* Compute amount of energy needed this cycle
             * Note, we are constantly readjusting the sample time of the pid which is probably not ideal as it will lead to imprecision over time, but it's ok for now */
            m_pid_input = m_temperature_measured_c;
            m_pid_target = m_temperature_target_c;
            m_pid.SetMode(AUTOMATIC);
            m_pid.SetSampleTime((millis() - m_timestamp_pid_computed) - 1);
            m_pid.SetOutputLimits(0, energy_limit);
            m_pid.Compute();
            m_timestamp_pid_computed = millis();

            /* Convert back energy into time and cap time to not exceed the cycle */
            m_heating_duration = 1000.0 * (m_pid_output / m_power_limit);
            if (m_heating_duration > CONFIG_TIP_CYCLE_TIME_LIMIT) {
                m_heating_duration = CONFIG_TIP_CYCLE_TIME_LIMIT;
            }

            /* Log */
            // log_t("Pid  %4.0f / %4.0f -> %5.2f J (%u ms)", m_pid_input, m_pid_target, m_pid_output, m_heating_duration);

            /* If we don't need to heat, restart a cycle */
            if (m_heating_duration <= 0) {
                m_sm = STATE_3_START;
                break;
            }

            /* Turn on dc-dc */
            power_enabled_set(true);

            /* Move on */
            m_sm = STATE_5_HEAT;
            break;
        }

        case STATE_5_HEAT: {

            /* Compute an estimate of the temperature while we are heating */
            uint32_t time_ellapsed = millis() - m_timestamp_temperature_read;
            float energy = m_power_limit * (time_ellapsed / 1000.0);
            float temperature_increase = energy / (CONFIG_TIP_COEFFICIENT_M * CONFIG_TIP_COEFFICIENT_C);
            float temperature_estimate = m_temperature_measured_c + temperature_increase;

            /* Add the value to the running filter */
            if ((millis() - m_temperature_filter_last_addition) >= 100) {
                m_temperature_filter.add(temperature_estimate);
                m_temperature_filter_last_addition = millis();
            }

            /* Wait for the end of the heating cycle */
            if ((millis() - m_timestamp_pid_computed) < m_heating_duration) {
                break;
            }

            /* Turn off dc-dc */
            power_enabled_set(false);

            /* Move on */
            m_sm = STATE_3_START;
            break;
        }

        case STATE_ERROR: {
            // TODO Ensure heating is disabled and wait a bit before retrying?
            break;
        }
    }

    /* Return success */
    return 0;
}
