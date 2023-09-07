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
#define TIP_CYCLE_TIME_LIMIT 1000  //!< Maximum amount of time that a measuring plus heating cycle should take (in milliseconds).
#define TIP_COEFFICIENT_C 466      //!< Specific heatt capacity of the heating element (in J / (kg * K)).
#define TIP_COEFFICIENT_M 0.002    //!< Mass of the heating element (in kg).

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
static PID m_pid(&m_pid_input, &m_pid_output, &m_pid_target, TIP_COEFFICIENT_C *TIP_COEFFICIENT_M, 0, 0, P_ON_E, DIRECT);

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
    if (temperature_c <= 0 || temperature_c > 400) {
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
            if (is_shorted_vcc == true || is_open == true) {
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
            if (millis() - m_timestamp_tip_connected < 100) {
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
            if (millis() - m_timestamp_temperature_read < 35) {
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
            if (is_shorted_vcc == true || is_open == true) {
                m_sm = STATE_0_DISCONNECTED;
                break;
            }

            /* Report tip as connected */
            m_element_connected = true;

            /* Compensate temperature,
             * probably because we got the type of thermocouple wrong */
            temperature_thermocouple_c = 2.3482 * temperature_thermocouple_c - 47.426;

            /* Discard abnormal values, because temperature readings:
             * 1) are not accurate right after heating,
             * 1) can be affected by electrically noisy environments */
            if (temperature_thermocouple_c < 0 || temperature_thermocouple_c > 500) {
                log_t("Read %4.0f invalid", temperature_thermocouple_c);
                break;
            }

            /* Save the value we just read */
            m_timestamp_temperature_read = millis();
            m_temperature_measured_c = temperature_thermocouple_c;
            m_temperature_filter.add(m_temperature_measured_c);
            m_temperature_filter_last_addition = millis();

            /* Log */
            log_t("Read %4.0f valid", temperature_thermocouple_c);

            /* If heating is not enabled, restart a cycle */
            if (m_heating_enabled != true) {
                m_sm = STATE_2_DISABLED;
                break;
            }

            /* Ask usb power negotiator how much power we are allowed to draw */
            res = power_negotiated_power_limit_get(&m_power_limit);
            if (res < 0) {
                break;
            }

            /* Ensure we have still time to heat in this cycle */
            if ((millis() - m_timestamp_cycle_start) >= TIP_CYCLE_TIME_LIMIT) {
                m_sm = STATE_3_START;
                break;
            }

            /* Compute maximum amount of energy we can use this cycle */
            float energy_limit = ((TIP_CYCLE_TIME_LIMIT - (millis() - m_timestamp_cycle_start)) / 1000.0) * m_power_limit;

            /* Compute amount of energy needed this cycle
             * Note, we are constantly readjusting the sample time of the pid which is probably not ideal as it will lead to imprecision over time, but it's ok for now */
            m_pid_input = m_temperature_measured_c;
            m_pid_target = m_temperature_target_c;
            m_pid.SetMode(AUTOMATIC);
            m_pid.SetSampleTime((millis() - m_timestamp_pid_computed) - 1);
            m_pid.SetOutputLimits(0, energy_limit);
            m_pid.Compute();
            m_timestamp_pid_computed = millis();

            /* Convert back energy into time */
            m_heating_duration = 1000.0 * (m_pid_output / m_power_limit);
            if (m_heating_duration > TIP_CYCLE_TIME_LIMIT) {
                m_heating_duration = TIP_CYCLE_TIME_LIMIT;
            }

            /* Log */
            log_t("Pid  %4.0f / %4.0f -> %5.2f J (%u ms)", m_pid_input, m_pid_target, m_pid_output, m_heating_duration);

            /* If we don't need to heat, restart a cycle */
            if (m_heating_duration <= 0) {
                m_sm = STATE_3_START;
                break;
            }

#if R4J
            /* Turn on dc-dc */
            digitalWrite(PC14, HIGH);
#elif R8A
            /* Turn on dc-dc */
            digitalWrite(3, HIGH);
#endif

            /* Move on */
            m_sm = STATE_5_HEAT;
            break;
        }

        case STATE_5_HEAT: {

            /* Compute an estimate of the temperature while we are heating */
            uint32_t time_ellapsed = millis() - m_timestamp_temperature_read;
            float energy = m_power_limit * (time_ellapsed / 1000.0);
            float temperature_increase = energy / (TIP_COEFFICIENT_M * TIP_COEFFICIENT_C);
            float temperature_estimate = m_temperature_measured_c + temperature_increase;

            /* Add the value to the running filter */
            if (millis() - m_temperature_filter_last_addition >= 100) {
                m_temperature_filter.add(temperature_estimate);
                m_temperature_filter_last_addition = millis();
            }

            /* Wait for the end of the heating cycle */
            if ((millis() - m_timestamp_pid_computed) < m_heating_duration) {
                break;
            }

#if R4J
            /* Turn off dc-dc */
            digitalWrite(PC14, LOW);
#elif R8A
            /* Turn off dc-dc */
            digitalWrite(3, LOW);
#endif

            /* Move on */
            m_sm = STATE_3_START;
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
