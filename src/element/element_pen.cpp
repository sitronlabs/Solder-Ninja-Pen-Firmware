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
#define CONFIG_TIP_TIME_COOLDOWN 10       //!< Amount of time, in milliseconds, to wait before measuring temperature.
#define CONFIG_TIP_TIME_CYCLE_LIMIT 1000  //!< Maximum amount of time, in milliseconds, that a measuring plus heating cycle should take.
#define ELEMENT_READINGS_CONSECUTIVE 10

/* Peripherals */
static max31855 m_thermocouple_afe;

/* PID library
 * As a base starting point for the coefficients, we can make the following approximation
 * For a mass of 2g of steel, a change of 1°C requires 0.93J
 * @see https://www.omnicalculator.com/physics/specific-heat */
static double m_pid_input = 0;
static double m_pid_target = 0;
static double m_pid_output = 0;
static PID m_pid(&m_pid_input, &m_pid_output, &m_pid_target, 1, 0.1, 0, P_ON_E, DIRECT);

/* Other local variables */
static float m_power_limit;
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

    /* If tip is not connected, return an error */
    if (m_element_connected != true) {
        return -ERROR_GENERIC;  // TODO More specific error code
    }

    /* If we are currently heating
     * return an an estimate of the temperature */
    if (m_heating_enabled == true && m_heating_duration > 0) {

        /* Get time ellapsed */
        uint32_t time_ellapsed = millis() - m_temperature_measured_timestamp;
        const float m = 0.002;
        const float c = 466;
        float energy = m_power_limit * (time_ellapsed / 1000.0);
        float temperature_increase = energy / (m * c);
        temperature_c = m_temperature_measured_c + temperature_increase;
        // log_t("")
    }

    /* Otherwise
     * return the last measured temperature */
    else {
        temperature_c = m_temperature_measured_c;
    }

    /* Return success */
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
        STATE_0,
        STATE_1,
        STATE_2,
        STATE_3,
        STATE_ERROR,
    } m_sm;
    switch (m_sm) {

        case STATE_0: {

            /* Because temperature readings can be affected by electrically noisy environments,
             * we average a few */
            m_element_connected = true;
            RunningMedian filter = RunningMedian(ELEMENT_READINGS_CONSECUTIVE);
            uint32_t t1 = millis();
            for (unsigned int i = 0; i < ELEMENT_READINGS_CONSECUTIVE;) {

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

                /* Compensate temperature
                 * Probably because we got the type of thermocouple wrong */
                temperature_thermocouple_c = 2.3482 * temperature_thermocouple_c - 47.426;

                /* Ensure tip is connected */
                if (is_shorted_vcc || is_open) {
                    m_element_connected = false;
                    log_t("Not connected");
                    break;
                }

                /* Discard abnormal values */
                if (temperature_thermocouple_c < 0 || temperature_thermocouple_c > 500) {
                    log_t("Read %4.0f invalid", temperature_thermocouple_c);
                    continue;
                }

                /* Log */
                log_t("Read %4.0f valid", temperature_thermocouple_c);

                /* Run temperature through filter */
                filter.add(temperature_thermocouple_c);

                /* Increment the number of valid readings */
                i++;
            }

            /* Retrieve mean */
            float temperature_thermocouple_c = filter.getMedian();

            /* Log */
            log_t("Filt %4.0f, took %u ms", temperature_thermocouple_c, (millis() - t1));

            /* Store temperature */
            m_temperature_measured_c = temperature_thermocouple_c;
            m_temperature_measured_timestamp = millis();

            /* Move on to heating if enabled */
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
            res = power_negotiated_power_limit_get(&m_power_limit);
            if (res < 0) {
                m_sm = STATE_0;
                break;
            }

            /* Compute maximum amount of energy we can use this cycle */
            float energy_limit = ((CONFIG_TIP_TIME_CYCLE_LIMIT - CONFIG_TIP_TIME_COOLDOWN) / 1000.0) * m_power_limit;  // 10.56 J

            /* Compute amount of energy needed this cycle */
            m_pid_input = m_temperature_measured_c;
            m_pid_target = m_temperature_target_c;
            m_pid.SetSampleTime((millis() - m_timestamp_last_cycle) - 1);
            m_pid.SetOutputLimits(0, energy_limit);
            m_pid.Compute();

            /* Convert back energy into time */
            m_heating_duration = 1000.0 * (m_pid_output / m_power_limit);

            /* Log */
            log_t("Pid  %4.0f / %4.0f -> %5.2f J (%u ms)", m_pid_input, m_pid_target, m_pid_output, m_heating_duration);

            /* */
            if (m_heating_duration <= 0) {
                m_timestamp_last_cycle = millis();
                m_sm = STATE_0;
                break;
            }

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
