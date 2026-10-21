/* Self header */
#include "power.h"

/* Project headers */
#include "errors/errors.h"
#include "log/log.h"
#include "settings/settings.h"

/* Arduino headers */
#include <Adafruit_INA219.h>
#include <dac5311.h>
#include <fsusb43.h>
#include <fusb302.h>
#include <pi3usb9281c.h>

/* Config */
#define POWER_OPTIONS_LIMIT 10  //!< Maximum number of power options to track

/* Peripherals */
static fsusb43 m_fsusb43;         //!< USB mux controller
static fusb302 m_fusb302;         //!< USB Type-C and PD PHY controller
static pi3usb9281c m_pi3usb9281;  //!< USB charger detection IC
static Adafruit_INA219 m_ina219;  //!< INA219 current/voltage monitor for the power input path
static dac5311 m_dac;             //!< DAC for buck converter regulation
static float m_dac_voltage_v = 0.0f;  //!< Last DAC-controlled buck output voltage used for current-limited heating.
static float m_tip_resistance_unadjusted_ohms = CONFIG_TIP_RESISTANCE_DEFAULT_OHMS;
static float m_tip_resistance_ohms = CONFIG_TIP_RESISTANCE_DEFAULT_OHMS;
static bool m_tip_resistance_power_wait_completed;
static enum {
    TIP_RESISTANCE_MEASUREMENT_IDLE,
    TIP_RESISTANCE_MEASUREMENT_WAIT_POWER,
    TIP_RESISTANCE_MEASUREMENT_SETTLE,
    TIP_RESISTANCE_MEASUREMENT_SAMPLE,
} m_tip_resistance_measurement_state;
static uint32_t m_tip_resistance_measurement_timestamp;
static uint32_t m_tip_resistance_sample_timestamp;
static uint8_t m_tip_resistance_sample_count;
static float m_tip_resistance_input_power_sum_w;

static bool m_tip_resistance_offset_get(float &offset_ohms) {
    return settings_heating_tip_resistance_offset_get(offset_ohms) == 1;
}

static void m_tip_resistance_value_update(void) {
    float offset_ohms = CONFIG_TIP_RESISTANCE_OFFSET_INVALID_OHMS;
    if (m_tip_resistance_offset_get(offset_ohms) != true) {
        m_tip_resistance_ohms = CONFIG_TIP_RESISTANCE_DEFAULT_OHMS;
        return;
    }
    m_tip_resistance_ohms = m_tip_resistance_unadjusted_ohms + offset_ohms;
}

/* Power options management */
static struct {
    bool assigned;
    struct power_option option;
} m_options[POWER_OPTIONS_LIMIT];
static bool m_options_changed = false;

/* HVDCP pin assignments for high voltage charging negotiation */
static int m_qc_dn_h_pin = 16;  //!< HVDCP D- upper resistor pin
static int m_qc_dp_h_pin = 18;  //!< HVDCP D+ upper resistor pin
static int m_qc_dn_l_pin = 17;  //!< HVDCP D- lower resistor pin
static int m_qc_dp_l_pin = 19;  //!< HVDCP D+ lower resistor pin
static int m_qc_dn_m_pin = 29;  //!< HVDCP D- middle resistor pin
static int m_qc_dp_m_pin = 28;  //!< HVDCP D+ middle resistor pin

/**
 * @brief Computes the buck converter output voltage for a DAC voltage.
 *
 * Uses the feedback resistor network to convert the DAC output voltage into the
 * corresponding DC-DC converter output voltage.
 *
 * @param[in] vdac DAC output voltage in volts
 * @return Estimated buck converter output voltage in volts
 */
static float m_dac_output_voltage_compute(const float vdac) {
    const float rtop = 590.0f * 1000.0f;
    const float rbot = 63.4f * 1000.0f;
    const float rdac = 300.0f * 1000.0f;
    const float vref = 0.7f;
    return vref * (1.0f + rtop / rbot) + (vref - vdac) * (rtop / rdac);
}

/**
 * @brief DAC setting selected for a requested buck output voltage.
 */
struct buck_dac_setting {
    float dac_voltage_v;          //!< Quantized DAC output voltage in volts.
    float buck_voltage_v;         //!< Modeled buck output at the selected DAC code, in volts.
    bool minimum_voltage_reached;  //!< True when code 255 is required to reach the target.
};

/**
 * @brief Maps a requested buck voltage to the nearest DAC code that does not exceed it.
 *
 * The DAC transfer function is inverse and has a finite 8-bit range, so the
 * returned buck voltage is the modeled, quantized output rather than the request.
 *
 * @param[in] requested_buck_voltage_v Requested buck output voltage in volts
 * @return Selected DAC voltage, modeled buck voltage, and minimum-range status
 */
static struct buck_dac_setting m_buck_dac_setting_compute(const float requested_buck_voltage_v) {
    const float minimum_buck_voltage_v = m_dac_output_voltage_compute(3.3f);
    const float maximum_buck_voltage_v = m_dac_output_voltage_compute(0.0f);
    const float target_buck_voltage_v = (requested_buck_voltage_v < minimum_buck_voltage_v)
        ? minimum_buck_voltage_v
        : ((requested_buck_voltage_v > maximum_buck_voltage_v) ? maximum_buck_voltage_v : requested_buck_voltage_v);

    struct buck_dac_setting setting = {};
    for (uint16_t code = 0; code < 256; code++) {
        const float vdac = 3.3f * (code / 255.0f);
        const float vout = m_dac_output_voltage_compute(vdac);
        if ((vout <= target_buck_voltage_v) || (code == 255)) {
            setting.dac_voltage_v = vdac;
            setting.buck_voltage_v = vout;
            setting.minimum_voltage_reached = (code == 255);
            return setting;
        }
    }

    return setting;
}

/**
 * @brief Gets the user-configured heating current-limit factor.
 * @return Factor in the range 0.1 to 1.0, or the configured default
 */
static float m_heating_current_limit_factor_get(void) {
    float factor = CONFIG_TIP_CURRENT_LIMIT_FACTOR;
    settings_heating_current_limit_factor_get(factor);
    return factor;
}

static float m_buck_current_limit_factor_applied = -1.0f;

/**
 * @brief Adjusts the buck converter output voltage based on desired power limit
 *
 * This function calculates the required DAC voltage to achieve the target power output
 * by considering the buck converter efficiency and load resistance.
 *
 * @param[in] power_limit Desired output power in watts (must be > 0)
 * @return 0 on success, negative error code otherwise
 *
 * @note Current implementation uses fixed efficiency (80%) and the detected tip resistance.
 *       Future improvements could include:
 *       - Dynamic efficiency calculation based on Vin, Vout, Iout, and temperature
 *       - Temperature-dependent load resistance modeling using thermal coefficient
 *       - Closed-loop feedback using measured voltage/current for precise regulation
 */
static int m_adjust_buck(const float power_limit) {

    const float limited_power = power_limit * m_heating_current_limit_factor_get();

    /* Calculate required buck converter output voltage
     * Using power formula: P = V²/R, solving for V = sqrt(P * R)
    * Where R is the detected tip resistance and efficiency is considered */
    float buck_voltage = sqrt((limited_power * CONFIG_TIP_BUCK_EFFICIENCY) * m_tip_resistance_ohms);
    log_d("Using buck voltage of %.2fV.", buck_voltage);
    const struct buck_dac_setting setting = m_buck_dac_setting_compute(buck_voltage);
    if (setting.minimum_voltage_reached && (buck_voltage < setting.buck_voltage_v)) {
        log_w("Requested buck voltage %.2fV is below the modeled DAC range (minimum %.2fV)", buck_voltage, setting.buck_voltage_v);
    }

    log_d("Using vdac %.2fV for vout %.2fV.", setting.dac_voltage_v, setting.buck_voltage_v);
    const int res = m_dac.output_voltage_set(setting.dac_voltage_v);
    if (res < 0) {
        log_e("Failed to configure DAC!");
        return -1;
    }

    m_dac_voltage_v = setting.dac_voltage_v;
    m_buck_current_limit_factor_applied = m_heating_current_limit_factor_get();
    return 0;
}

/**
 * @brief Reads the current from the INA219 power monitor.
 *
 * Helper for future heater-control loops that want actual input-side current feedback.
 *
 * @param[out] current_a Current in amperes
 * @return 0 on success, negative error code otherwise
 */
int power_current_get(float &current_a) {
    /*
    * The INA219 reports the shunt voltage in millivolts. For the configured
    * shunt value, current is Vshunt_mV / Rshunt_milliohms, because:
    *  I = (Vshunt_mV / 1000) / (Rshunt_milliohms / 1000)
     * This avoids an unnecessary extra conversion and matches the actual hardware.
     */
    current_a = m_ina219.getShuntVoltage_mV() / CONFIG_TIP_SHUNT_MILLIOHMS;
    return 0;
}

/**
 * @brief Reduces buck output in response to measured USB input overcurrent.
 *
 * For a resistive tip, output power is proportional to buck voltage squared. The
 * measured current ratio estimates the required buck voltage, and the DAC is moved
 * in the inverse direction needed to lower that output. Corrections are step-limited.
 *
 * @param[in] current_a Measured current in amperes
 * @param[in] current_limit_a Maximum allowed current in amperes
 * @return 0 if no correction is needed or the correction succeeds; negative if
 *         the DAC write fails or current remains over limit at maximum DAC output
 */
int power_heating_current_limit_apply(const float current_a, const float current_limit_a) {
    if ((current_a <= 0.0f) || (current_limit_a <= 0.0f)) {
        return 0;
    }

    if (current_a <= current_limit_a) {
        return 0;
    }

    const float dac_max_voltage = 3.3f;
    const float dac_resolution_v = dac_max_voltage / 255.0f;
    if (m_dac_voltage_v >= (dac_max_voltage - dac_resolution_v)) {
        log_e("Current %.2fA remains above %.2fA with DAC at maximum; heating cannot meet the configured limit", current_a, current_limit_a);
        return -1;
    }

    /* Input power is proportional to input current; tip power is proportional to Vout^2. */
    const float current_buck_voltage = m_dac_output_voltage_compute(m_dac_voltage_v);
    const float target_buck_voltage = (current_buck_voltage > 0.0f)
        ? current_buck_voltage * sqrt(current_limit_a / current_a)
        : 0.0f;

    /* The shared mapping handles the inverse DAC transfer function. */
    struct buck_dac_setting setting = m_buck_dac_setting_compute(target_buck_voltage);

    if (setting.dac_voltage_v < m_dac_voltage_v) {
        setting.dac_voltage_v = m_dac_voltage_v;
    }

    /* Limit each correction step while moving in the direction that reduces buck output. */
    const float voltage_step_v = 0.05f;
    if ((setting.dac_voltage_v - m_dac_voltage_v) > voltage_step_v) {
        setting.dac_voltage_v = m_dac_voltage_v + voltage_step_v;
    }

    /* Update the DAC to reduce the buck output voltage. */
    int res = m_dac.output_voltage_set(setting.dac_voltage_v);
    if (res < 0) {
        log_w("Failed to increase DAC voltage to %.2fV while current was %.2fA > %.2fA", setting.dac_voltage_v, current_a, current_limit_a);
        return res;
    }

    m_dac_voltage_v = setting.dac_voltage_v;
    log_w("Increased DAC voltage to %.2fV toward %.2fA current limit (measured %.2fA)", setting.dac_voltage_v, current_limit_a, current_a);
    return 0;
}

/**
 * @brief Sets the buck output corresponding to a requested USB input-current limit.
 *
 * The requested current is converted to a buck voltage using the measured USB bus
 * voltage, configured efficiency, and active tip resistance, then quantized through
 * the shared DAC mapping.
 *
 * @param[in] current_limit_a Requested USB input-current limit in amperes; zero
 *            selects the minimum buck output
 * @param[out] applied_current_limit_a Modeled current limit after DAC quantization
 * @param[out] minimum_voltage_reached True when the maximum DAC code (255) was
 *             needed to reach the requested buck voltage
 * @return 0 on success, negative if input voltage/resistance is invalid or the DAC write fails
 */
int power_heating_current_limit_ramp_apply(const float current_limit_a, float &applied_current_limit_a, bool &minimum_voltage_reached) {
    float input_voltage_v = 0.0f;
    if ((power_voltage_get(input_voltage_v) < 0) || (input_voltage_v <= 0.0f) ||
        (m_tip_resistance_ohms <= 0.0f)) {
        return -1;
    }

    const float requested_buck_voltage = (current_limit_a > 0.0f)
        ? sqrt(current_limit_a * input_voltage_v * CONFIG_TIP_BUCK_EFFICIENCY * m_tip_resistance_ohms)
        : 0.0f;
    const struct buck_dac_setting setting = m_buck_dac_setting_compute(requested_buck_voltage);
    minimum_voltage_reached = setting.minimum_voltage_reached;
    applied_current_limit_a = (setting.buck_voltage_v * setting.buck_voltage_v) /
        (CONFIG_TIP_BUCK_EFFICIENCY * m_tip_resistance_ohms * input_voltage_v);

    const int res = m_dac.output_voltage_set(setting.dac_voltage_v);
    if (res < 0) {
        log_w("Failed to set soft-start DAC voltage to %.2fV", setting.dac_voltage_v);
        return res;
    }

    m_dac_voltage_v = setting.dac_voltage_v;
    return 0;
}

/**
 * @brief Reads the bus voltage from the INA219 power monitor.
 *
 * @param[out] voltage_v Voltage in volts
 * @return 0 on success, negative error code otherwise
 */
int power_voltage_get(float &voltage_v) {
    voltage_v = m_ina219.getBusVoltage_V();
    return 0;
}

/**
 * @brief Gets the tip resistance currently used for buck regulation.
 *
 * Returns the selected nominal resistance, or the configured default when a
 * valid measurement is unavailable.
 *
 * @param[out] resistance_ohms Current tip resistance in ohms
 * @return 0 on success
 */
int power_tip_resistance_get(float &resistance_ohms) {
    resistance_ohms = m_tip_resistance_ohms;
    return 0;
}

/**
 * @brief Computes the maximum power offered by the given power option
 *
 * Calculates the maximum power available from a power option based on its type.
 * For voltage/current limited options, power is calculated as V × I.
 * For fixed power options, the power_max value is returned directly.
 *
 * @param[in] option The power option to evaluate
 * @return Maximum power in watts, or 0 if option type is invalid
 */
static float m_option_power_max_compute(struct power_option &option) {
    switch (option.type) {
        case POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT:
        case POWER_TYPE_VARIABLE_VOLTAGE_FIXED_CURRENT: {
            return option.voltage_max * option.current_max;
        }
        case POWER_TYPE_VARIABLE_VOLTAGE_FIXED_POWER: {
            return option.power_max;
        }
        default: {
            return 0;
        }
    }
}

/**
 * @brief Adds a power option to the internal options list
 *
 * This function adds a new power option to the available options list and determines
 * if it offers more power than the currently available options. The function will
 * find the first available slot and store the option there.
 *
 * @param[in] option The power option to add to the list
 * @return 1 if the new option offers more power than existing options,
 *         0 if it offers the same or less power,
 *         -1 if no space is available in the options list
 *
 * @note This function automatically sets the m_options_changed flag when a new option is added
 */
static int m_options_add(struct power_option &option) {

    /* Find out the maximum amount of power offered by the options already in the list */
    float power_max = 0.0f;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
            }
        }
    }

    /* Find an available slot and add the new option */
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == false) {
            /* Copy the option data to the available slot */
            memcpy(&m_options[i].option, &option, sizeof(struct power_option));
            m_options[i].assigned = true;

            /* Mark that the options list has been modified */
            m_options_changed = true;

            /* Determine if this new option offers more power than existing ones */
            float new_option_power = m_option_power_max_compute(option);
            return (new_option_power > power_max) ? 1 : 0;
        }
    }

    /* Return no available slots found */
    return -1;
}

/**
 * @brief Clears all power options from the internal options list
 *
 * This function marks all power option slots as unassigned and sets the
 * options changed flag to trigger a re-evaluation of available power.
 *
 * @note This function is typically called when starting a new power negotiation or when resetting the power management system
 */
static void m_options_clear(void) {
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        m_options[i].assigned = false;
    }
    m_options_changed = true;
}

/**
 * @brief Gets the maximum power available from the assigned power options.
 * @return Maximum available power in watts, or 0 if no power option is assigned
 */
static float m_available_power_max_get(void) {
    float power_max = 0.0f;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
            }
        }
    }
    return power_max;
}

int power_tip_resistance_offset_apply(void) {
    m_tip_resistance_value_update();
    return m_adjust_buck(m_available_power_max_get());
}

/**
 * @brief Initializes the power management module and all associated peripherals
 *
 * This function sets up all the hardware components required for USB power negotiation:
 * - USB charger detection IC (PI3USB9281C)
 * - USB mux controller (FSUSB43)
 * - USB Type-C and PD PHY (FUSB302)
 * - DAC for buck converter regulation (DAC5311)
 * - Platform-specific DC-DC converter control
 *
 * @return 0 on success, negative error code otherwise
 * @retval -ERROR_PERIPHERAL_SETUP_ERROR if any peripheral fails to initialize
 */
int power_setup(void) {
    int res;

    /* Initialize INA219 current/voltage monitor for the DC-DC input path.
     * We intentionally do not use the library's default calibration because this
     * board's 0.010 Ω shunt is converted directly from measured shunt voltage. */
    if (!m_ina219.begin()) {
        log_e("Failed to setup INA219 current sensor!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }
    log_i("INA219 current monitor initialized (%.1f mΩ shunt).", CONFIG_TIP_SHUNT_MILLIOHMS);

    /* Initialize USB charger detection IC (PI3USB9281C)
     * This IC detects USB charging capabilities and port types */
    res = m_pi3usb9281.setup(Wire, 0x25, 7);  // TODO: R4J
    if (res < 0) {
        log_e("Failed to setup PI3USB9281C charger detection IC!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

    /* Initialize USB mux controller (FSUSB43)
     * Routes USB signals between internal PHY and resistor network for HVDCP */
    m_fsusb43.setup(7);
    m_fsusb43.output_select(FSUSB43_OUTPUT_1);

    /* Initialize USB Type-C and Power Delivery PHY (FUSB302)
     * Handles Type-C detection, orientation, and PD negotiation */
    res = m_fusb302.setup(Wire, 0x22);
    if (res < 0) {
        log_e("Failed to setup FUSB302 Type-C/PD PHY!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

#if R4J

    /* Configure DC-DC converter enable pin */
    pinMode(PC14, OUTPUT);
    digitalWrite(PC14, LOW);

    /* Initialize DAC for DC-DC converter regulation
     * SPI bus, 8MHz clock, CS on PA1, 3.3V reference */
    res = m_dac.setup(SPI, 8000000, PA1, 3.3);
    if (res < 0) {
        log_e("Failed to setup DAC for DC-DC regulation!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

#elif R8A

    /* Configure DC-DC converter enable pin */
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);

    /* Initialize DAC for DC-DC converter regulation
     * SPI1 bus, 8MHz clock, CS on GPIO 20, 3.3V reference */
    res = m_dac.setup(SPI1, 8000000, 20, 3.3);
    if (res < 0) {
        log_e("Failed to setup DAC for DC-DC regulation!");
        return -ERROR_PERIPHERAL_SETUP_ERROR;
    }

#endif

    /* Return success */
    return 0;
}

/**
 * @brief Finds the best power contract from available power providers
 *
 * This function searches through all available power options and returns the one
 * that offers the highest power.
 *
 * @param[out] contract The best power contract found
 * @return 0 on success, -1 if no power options are available
 */
int power_contract_get(struct power_option &contract) {
    float power_max;
    int index_max;

    /* Find out the best contract offered by USB PD first */
    power_max = 0;
    index_max = -1;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true && m_options[i].option.provider == POWER_PROVIDER_USB_PD) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
                index_max = i;
            }
        }
    }
    if (index_max >= 0) {
        contract = m_options[index_max].option;
        return 0;
    }

    /* If no USB PD option exists, find out the best contrat in amongst the other options */
    power_max = 0;
    index_max = -1;
    for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
        if (m_options[i].assigned == true) {
            float power_iter = m_option_power_max_compute(m_options[i].option);
            if (power_iter > power_max) {
                power_max = power_iter;
                index_max = i;
            }
        }
    }
    if (index_max >= 0) {
        contract = m_options[index_max].option;
        return 0;
    } else {
        return -1;
    }
}

/**
 * @brief Gets the currently negotiated power limit from the best available contract
 *
 * This function retrieves the maximum power available from the best power contract
 * that has been negotiated. It uses the power_contract_get() function to find the best
 * option and then calculates the maximum power that option can provide.
 *
 * @param[out] power_limit The negotiated power limit in watts
 * @return 0 on success, -1 if no power contract is available
 */
int power_negotiated_power_limit_get(float &power_limit) {

    int res;

    /* Find out the maximum amount of power offered by the options */
    struct power_option contract;
    res = power_contract_get(contract);
    if (res < 0) {
        power_limit = 0;
        return -1;
    }

    /* Apply the configured current factor to the power budget before heating starts. */
    power_limit = m_option_power_max_compute(contract) * m_heating_current_limit_factor_get();
    return 0;
}

/**
 * @brief Gets the negotiated current limit, reduced by the configured safety factor.
 *
 * This helper ensures the heating control loop never requests more than the allowed
 * percentage of the negotiated current from the USB source.
 *
 * @param[out] current_limit_a Current limit in amperes
 * @return 0 on success, negative error code on failure
 */
int power_negotiated_current_limit_get(float &current_limit_a) {
    int res;

    struct power_option contract;
    res = power_contract_get(contract);
    if (res < 0) {
        current_limit_a = 0.0f;
        return -1;
    }

    /* Prefer the negotiated current limit when it is explicitly known. */
    if (contract.current_max > 0.0f) {
        const float current_limit_factor = m_heating_current_limit_factor_get();
        current_limit_a = contract.current_max * current_limit_factor;
        return 0;
    }

    /* Fall back to power/voltage when only a power contract is available. */
    if (contract.voltage_max > 0.0f) {
        const float negotiated_power_w = m_option_power_max_compute(contract);
        const float current_limit_factor = m_heating_current_limit_factor_get();
        current_limit_a = (negotiated_power_w / contract.voltage_max) * current_limit_factor;
        return 0;
    }

    current_limit_a = 0.0f;
    return -1;
}

/**
 * @brief Advances the non-blocking tip resistance measurement.
 *
 * Applies a bounded test voltage, averages INA219 input-power samples, estimates
 * the tip resistance, and selects the matching nominal resistance for buck control.
 *
 * @return 0 while waiting or collecting samples, 1 when a valid measurement
 *         completes, 2 when the measurement is skipped or falls back to the
 *         default resistance, negative error code if setup or cleanup fails
 */
int power_tip_resistance_measure_task(void) {
    const float test_dac_voltage_v = 3.3f;
    const float test_output_voltage_v = m_dac_output_voltage_compute(test_dac_voltage_v);

    if (m_tip_resistance_measurement_state == TIP_RESISTANCE_MEASUREMENT_IDLE) {
        power_enabled_set(false);
        m_tip_resistance_unadjusted_ohms = CONFIG_TIP_RESISTANCE_DEFAULT_OHMS;
        m_tip_resistance_value_update();
        m_tip_resistance_measurement_timestamp = millis();
        m_tip_resistance_measurement_state = TIP_RESISTANCE_MEASUREMENT_WAIT_POWER;
    }

    if (m_tip_resistance_measurement_state == TIP_RESISTANCE_MEASUREMENT_WAIT_POWER) {
        float current_limit_a = 0.0f;
        float input_voltage_v = 0.0f;
        struct power_option contract;
        int res = power_contract_get(contract);
        bool usb_power_contract_ready = (res == 0) &&
                                        (contract.voltage_max >= CONFIG_TIP_RESISTANCE_MINIMUM_USB_VOLTAGE_V) &&
                                        (contract.current_max >= CONFIG_TIP_RESISTANCE_MINIMUM_USB_CURRENT_A);
        bool probe_power_ready = false;

        if (usb_power_contract_ready &&
            (power_negotiated_current_limit_get(current_limit_a) == 0) &&
            (power_voltage_get(input_voltage_v) == 0) &&
            (current_limit_a > 0.0f) &&
            (input_voltage_v > 0.0f)) {
            probe_power_ready = true;
        }

        if (probe_power_ready != true) {
            if ((m_tip_resistance_power_wait_completed != true) &&
                ((millis() - m_tip_resistance_measurement_timestamp) < CONFIG_TIP_RESISTANCE_POWER_WAIT_TIMEOUT)) {
                return 0;
            }

            m_tip_resistance_power_wait_completed = true;
            log_w("No valid USB power contract (minimum %.1fV and %.1fA) for tip resistance probe; using default %.1f ohm",
                CONFIG_TIP_RESISTANCE_MINIMUM_USB_VOLTAGE_V,
                CONFIG_TIP_RESISTANCE_MINIMUM_USB_CURRENT_A,
                m_tip_resistance_ohms);
            m_tip_resistance_measurement_state = TIP_RESISTANCE_MEASUREMENT_IDLE;
            return 2;
        }

        m_tip_resistance_power_wait_completed = true;

        const float worst_case_input_current_a = (test_output_voltage_v * test_output_voltage_v) /
                             (CONFIG_TIP_BUCK_EFFICIENCY * input_voltage_v * CONFIG_TIP_RESISTANCE_ALTERNATIVE_OHMS);
        if (worst_case_input_current_a > (current_limit_a * 0.8f)) {
            log_w("Skipping tip resistance probe: estimated %.2fA exceeds probe budget %.2fA", worst_case_input_current_a, current_limit_a * 0.8f);
            m_tip_resistance_measurement_state = TIP_RESISTANCE_MEASUREMENT_IDLE;
            return 2;
        }

        res = m_dac.output_voltage_set(test_dac_voltage_v);
        if (res < 0) {
            m_tip_resistance_measurement_state = TIP_RESISTANCE_MEASUREMENT_IDLE;
            return res;
        }
        m_dac_voltage_v = test_dac_voltage_v;
        m_tip_resistance_input_power_sum_w = 0.0f;
        m_tip_resistance_sample_count = 0;
        m_tip_resistance_measurement_timestamp = millis();
        power_enabled_set(true);
        m_tip_resistance_measurement_state = TIP_RESISTANCE_MEASUREMENT_SETTLE;
        return 0;
    }

    if (m_tip_resistance_measurement_state == TIP_RESISTANCE_MEASUREMENT_SETTLE) {
        if ((millis() - m_tip_resistance_measurement_timestamp) < CONFIG_TIP_RESISTANCE_MEASUREMENT_SETTLE_TIME) {
            return 0;
        }
        m_tip_resistance_measurement_state = TIP_RESISTANCE_MEASUREMENT_SAMPLE;
        m_tip_resistance_sample_timestamp = millis() - CONFIG_TIP_RESISTANCE_MEASUREMENT_SAMPLE_PERIOD;
    }

    if (m_tip_resistance_measurement_state == TIP_RESISTANCE_MEASUREMENT_SAMPLE) {
        if ((millis() - m_tip_resistance_sample_timestamp) < CONFIG_TIP_RESISTANCE_MEASUREMENT_SAMPLE_PERIOD) {
            return 0;
        }
        m_tip_resistance_sample_timestamp = millis();

        float current_a = 0.0f;
        float input_voltage_v = 0.0f;
        if ((power_current_get(current_a) < 0) || (power_voltage_get(input_voltage_v) < 0)) {
            power_tip_resistance_measure_cancel();
            return -1;
        }
        m_tip_resistance_input_power_sum_w += current_a * input_voltage_v;
        m_tip_resistance_sample_count++;
        if (m_tip_resistance_sample_count < CONFIG_TIP_RESISTANCE_MEASUREMENT_SAMPLE_COUNT) {
            return 0;
        }

        const float average_input_power_w = m_tip_resistance_input_power_sum_w / CONFIG_TIP_RESISTANCE_MEASUREMENT_SAMPLE_COUNT;
        bool measurement_valid = false;
        if (average_input_power_w > 0.1f) {
            const float unadjusted_resistance_ohms = (test_output_voltage_v * test_output_voltage_v) / (CONFIG_TIP_BUCK_EFFICIENCY * average_input_power_w);
            float offset_ohms = CONFIG_TIP_RESISTANCE_OFFSET_INVALID_OHMS;
            const bool offset_configured = m_tip_resistance_offset_get(offset_ohms);
            const float measured_resistance_ohms = unadjusted_resistance_ohms + (offset_configured ? offset_ohms : 0.0f);
            if ((measured_resistance_ohms >= CONFIG_TIP_RESISTANCE_MEASUREMENT_MIN_VALID_OHMS) &&
                (measured_resistance_ohms <= CONFIG_TIP_RESISTANCE_MEASUREMENT_MAX_VALID_OHMS)) {
                m_tip_resistance_unadjusted_ohms = unadjusted_resistance_ohms;
                m_tip_resistance_ohms = offset_configured ? measured_resistance_ohms : CONFIG_TIP_RESISTANCE_DEFAULT_OHMS;
                measurement_valid = offset_configured;
                log_i("Tip resistance measured at %.2f ohm; using %.1f ohm", measured_resistance_ohms, m_tip_resistance_ohms);
            } else {
                m_tip_resistance_unadjusted_ohms = CONFIG_TIP_RESISTANCE_DEFAULT_OHMS;
                m_tip_resistance_value_update();
                log_w("Tip resistance probe gave out-of-range result %.2f ohm; using %.1f ohm", measured_resistance_ohms, m_tip_resistance_ohms);
            }
        } else {
            m_tip_resistance_unadjusted_ohms = CONFIG_TIP_RESISTANCE_DEFAULT_OHMS;
            m_tip_resistance_value_update();
            log_w("Tip resistance probe current too low; using %.1f ohm", m_tip_resistance_ohms);
        }

        int res = power_tip_resistance_measure_cancel();
        return (res < 0) ? res : (measurement_valid ? 1 : 2);
    }

    return 0;
}

/**
 * @brief Stops the tip resistance measurement and restores normal buck regulation.
 *
 * Disables the converter, resets the measurement state, and recalculates the DAC
 * output using the current available power and selected tip resistance.
 *
 * @return 0 on success, negative error code if the DAC output could not be restored
 */
int power_tip_resistance_measure_cancel(void) {
    if (m_tip_resistance_measurement_state == TIP_RESISTANCE_MEASUREMENT_IDLE) {
        return 0;
    }

    power_enabled_set(false);
    m_tip_resistance_measurement_state = TIP_RESISTANCE_MEASUREMENT_IDLE;
    return m_adjust_buck(m_available_power_max_get());
}

int power_heating_current_limit_configure(void) {
    const float factor = m_heating_current_limit_factor_get();
    if (factor == m_buck_current_limit_factor_applied) {
        return 0;
    }

    return m_adjust_buck(m_available_power_max_get());
}

/**
 * @brief Enables or disables the DC-DC converter supplying the tip with power
 *
 * This function controls the enable pin of the DC-DC converter to turn the power
 * supply on or off. The specific pin used depends on the platform configuration.
 *
 * @param[in] enabled true to enable the power supply, false to disable
 * @return 0 on success (always succeeds for now)
 */
int power_enabled_set(const bool enabled) {
    if (enabled == true) {
#if R4J
        /* Turn on dc-dc */
        digitalWrite(PC14, HIGH);
#elif R8A
        /* Turn on dc-dc */
        digitalWrite(3, HIGH);
#endif
    } else {
#if R4J
        /* Turn off dc-dc */
        digitalWrite(PC14, LOW);
#elif R8A
        /* Turn off dc-dc */
        digitalWrite(3, LOW);
#endif
    }

    /* Return success */
    return 0;
}

/**
 * @brief Main power management task - handles power negotiation and regulation
 *
 * This is the main power management function that should be called periodically
 * from the main application loop. It handles:
 * - Buck converter regulation based on available power
 * - USB power negotiation state machine
 * - Error handling and retry logic
 *
 * The power negotiation follows this priority order:
 * 1. USB Type-C detection and orientation
 * 2. USB Power Delivery (PD) negotiation
 * 3. USB Battery Charging (BC1.2) detection
 * 4. HVDCP (High Voltage Dedicated Charging Port) negotiation
 *
 * @return 0 on success, negative error code otherwise
 *
 * @note This function implements a state machine and should be called frequently to ensure timely power negotiation
 * @see State machine documentation below for detailed flow
 */
int power_task(void) {
    int res;

    /* Adjust buck converter based on best available power option if options have changed */
    if (m_options_changed == true) {
        m_options_changed = false;

        /* Find the best available power option */
        float power_best = m_available_power_max_get();

        /* Adjust buck converter */
        res = m_adjust_buck(power_best);
        if (res < 0) {
            log_w("Failed to adjust buck converter to %.2fW", power_best);
        } else {
            log_d("Adjusted buck converter to %.2fW", power_best);
        }
    }

    /* Power Negotiation State Machine
     *
     * This state machine implements a hierarchical power negotiation strategy:
     *
     * 1) USB Type-C Detection (TC_0 → TC_1 → TC_2):
     *    - Detect Type-C cable orientation
     *    - Determine basic power capabilities (0.5A, 1.5A, 3.0A)
     *    - Add Type-C power option to available options
     *
     * 2) USB Power Delivery Negotiation (PD_0 → PD_1 → PD_2 → PD_3 → PD_4):
     *    - Attempt PD capability exchange
     *    - Parse source capabilities
     *    - Request best available PD contract
     *    - If successful, monitor PD contract (PD_MONITOR)
     *    - If failed, fall back to BC1.2 detection
     *
     * 3) USB Battery Charging Detection (BC_0 → BC_1 → BC_2 → BC_3 → BC_4):
     *    - Detect charging port type (SDP/CDP/DCP)
     *    - Determine available current (0.5A/1.5A/2.4A)
     *    - Add BC1.2 power option to available options
     *    - If DCP detected, attempt HVDCP negotiation
     *
     * 4) HVDCP Negotiation (QC_0 → QC_1 → ... → QC_7):
     *    - Only attempted if DCP was detected in BC1.2 phase
     *    - Negotiate higher voltages (12V, 9V)
     *    - Monitor negotiated voltage (QC_MONITOR)
     *
     * 5) Completion (DONE):
     *    - Power negotiation complete
     *    - Power source that doesn't require monitoring is active
     *
     * Error Handling:
     * - Each protocol has retry counters (m_errors_*)
     * - After 5 retries, fall back to next protocol
     * - If all protocols fail, return to IDLE state */
    static uint8_t m_errors_tc;
    static uint8_t m_errors_bc;
    static uint8_t m_errors_qc;
    static uint8_t m_errors_pd;
    static uint8_t m_pd_next_message_id;
    static float m_pd_voltage;
    static float m_pd_current;
    static float m_qc_voltage;
    static float m_qc_current;
    static uint32_t m_timestamp;
    static float m_diag_voltage_max;
    static enum {
        STATE_IDLE,
        STATE_TC_0,
        STATE_TC_1,
        STATE_TC_2,
        STATE_PD_0,
        STATE_PD_1,
        STATE_PD_2,
        STATE_PD_3,
        STATE_PD_4,
        STATE_PD_MONITOR,
        STATE_BC_0,
        STATE_BC_1,
        STATE_BC_2,
        STATE_BC_3,
        STATE_BC_4,
        STATE_QC_0,
        STATE_QC_1,
        STATE_QC_2,
        STATE_QC_3,
        STATE_QC_4,
        STATE_QC_5,
        STATE_QC_6,
        STATE_QC_7,
        STATE_QC_MONITOR,
        STATE_DONE,
    } m_sm;
    switch (m_sm) {

        case STATE_IDLE: {

            /* Reset error counters and flags */
            m_errors_tc = 0;
            m_errors_bc = 0;
            m_errors_qc = 0;
            m_errors_pd = 0;

            /* Clear list of options */
            m_options_clear();

            /* Start with Type-C */
            m_sm = STATE_TC_0;
            break;
        }

        case STATE_TC_0: {

            /* Don't retry too many times */
            if (m_errors_tc > 5) {
                log_e("Too many tc errors!");
                m_sm = STATE_BC_0;
                break;
            }

            /* Move on */
            m_sm = STATE_TC_1;
            break;
        }

        case STATE_TC_1: {

            /* Ensure ic is detected */
            if (m_fusb302.detect() != true) {
                log_e("Failed to detect fusb302 ic!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Reset internal registers */
            res = m_fusb302.reset();
            if (res < 0) {
                log_e("Failed to reset fusb302 ic!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_TC_2;
            break;
        }

        case STATE_TC_2: {

            /* Wait a little bit after reset */
            if ((millis() - m_timestamp) < 15) {
                break;
            }

            /* Enable power to all internal circuitry */
            res = m_fusb302.power_set(true);
            if (res < 0) {
                log_e("Failed to configure fusb302 ic!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Ensure pulldown resistors are enabled */
            res = m_fusb302.cc_pull_down();
            if (res < 0) {
                log_e("Failed to configure fusb302 ic!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Measure voltages on the cc pins to determine
             * 1) the orientation of the usb type-c cable
             * 2) the current limit reported by the dfp */
            enum usb_typec_cc_status cc1, cc2;
            res = m_fusb302.cc_measure(cc1, cc2);
            if (res < 0) {
                log_e("Failed to read cc voltages!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Detect orientation */
            usb_typec_cc_orientation orientation;
            if (cc1 > USB_TYPEC_CC_STATUS_OPEN && cc2 == USB_TYPEC_CC_STATUS_OPEN) {
                log_d("Type-C orientation is default.");
                orientation = USB_TYPEC_CC_ORIENTATION_NORMAL;
            } else if (cc1 == USB_TYPEC_CC_STATUS_OPEN && cc2 > USB_TYPEC_CC_STATUS_OPEN) {
                log_d("Type-C orientation is flipped.");
                orientation = USB_TYPEC_CC_ORIENTATION_REVERSE;
            } else {
                log_w("Invalid cc pin logic");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Set orientation */
            res = m_fusb302.cc_orientation_set(orientation);
            if (res < 0) {
                log_e("Failed to set orientation!");
                m_sm = STATE_TC_0;
                m_errors_tc++;
                break;
            }

            /* Determine current limit based on cc pin voltage */
            float current = 0.0;
            if (cc1 == USB_TYPEC_CC_STATUS_RP_3_0 || cc2 == USB_TYPEC_CC_STATUS_RP_3_0) {
                log_i("Type-C src advertises 3.0A.");
                current = 3.0;
            } else if (cc1 == USB_TYPEC_CC_STATUS_RP_1_5 || cc2 == USB_TYPEC_CC_STATUS_RP_1_5) {
                log_i("Type-C src advertises 1.5A.");
                current = 1.5;
            } else if (cc1 == USB_TYPEC_CC_STATUS_RP_DEF || cc2 == USB_TYPEC_CC_STATUS_RP_DEF) {
                log_i("Type-C src advertises 0.5A.");
                current = 0.5;
            }

            /* Add power option */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_TC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 5.0f,
                .voltage_max = 5.0f,
                .current_max = current,
                .power_max = 5.0f * current,
            };
            m_options_add(option);

            /* Move on */
            m_sm = STATE_PD_0;
            break;
        }

        case STATE_PD_0: {

            /* Don't retry too many times */
            if (m_errors_pd > 5) {
                log_e("Too many pd errors!");
                m_sm = STATE_BC_0;
                break;
            }

            /* Move on */
            m_sm = STATE_PD_1;
            break;
        }

        case STATE_PD_1: {

            /* Enable automatic retransmission */
            res = m_fusb302.pd_autoretry_set(3);
            if (res < 0) {
                log_e("Failed to enable fusb302 auto retry!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Enable automatic goodcrc
             * @note Starting from here, ensure the firmware doesn't stall the processing of pd messages that needs to happen in roughly 10ms */
            res = m_fusb302.pd_autogoodcrc_set(true);
            if (res < 0) {
                log_e("Failed to enable fusb302 auto goodcrc!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Flush TX fifo */
            res = m_fusb302.pd_tx_flush();
            if (res < 0) {
                log_e("Failed to flush fusb302 tx fifo!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Flush RX fifo */
            res = m_fusb302.pd_rx_flush();
            if (res < 0) {
                log_e("Failed to flush fusb302 rx fifo!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Reset PD logic */
            res = m_fusb302.pd_reset_logic();
            if (res < 0) {
                log_e("Failed to reset fusb302 pd logic!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Clear pd related variables */
            m_pd_next_message_id = 0;
            m_pd_voltage = 0;
            m_pd_current = 0;

            /* Clear pd related power options */
            for (unsigned int i = 0; i < POWER_OPTIONS_LIMIT; i++) {
                if (m_options[i].option.provider == POWER_PROVIDER_USB_PD) {
                    m_options[i].assigned = false;
                }
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_PD_2;
            break;
        }

        case STATE_PD_2: {

            /* Look for incoming messages */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming pd messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 1) {

                /* Log */
                log_d("Received pd message: sop=%d, header=0x%04X, object_count=%d", response.sop_type, response.header, response.object_count);
                for (unsigned int i = 0; i < response.object_count; i++) {
                    log_d(" - Object %u=0x%08X", i, response.objects[i]);
                }

                /* Handle source capabilities message */
                if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count > 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_DATA_SOURCE_CAPABILITIES)) {

                    /* Parse each pdo looking for the most interesting one
                     * For now we only handle fixed pdos
                     * Ideally we want to find a pdo that gives us 45W with a voltage of less than 17V, but we can go up to 20V if needed */
                    double power_best = 0;
                    int8_t power_best_index = -1;
                    for (uint8_t i = 0; i < response.object_count; i++) {
                        switch (response.objects[i] >> 30U) {

                            case USB_PD_PDO_TYPE_FIXED: {
                                double voltage = ((response.objects[i] & 0x000FFC00) >> 10) * 0.05;
                                double current = ((response.objects[i] & 0x000001FF) >> 0) * 0.01;
                                double power_max = voltage * current;
                                double power_usable = power_max > 45 ? 45 : power_max;
                                log_i("Received fixed pdo %fV %fA", voltage, current);
                                if (voltage <= 17) {
                                    if (power_usable >= power_best) {
                                        power_best = power_usable;
                                        power_best_index = i;
                                        m_pd_current = power_usable / voltage;
                                        m_pd_voltage = voltage;
                                    }
                                } else if (voltage <= 20) {
                                    if (power_usable > power_best) {
                                        power_best = power_usable;
                                        power_best_index = i;
                                        m_pd_current = power_usable / voltage;
                                        m_pd_voltage = voltage;
                                    }
                                }
                                break;
                            }

                            default: {
                                log_w("Received unsupported pdo.");  // TODO: Add support for other pdo types?
                                break;
                            }
                        }
                    }

                    /* Request the most interesting pdo */
                    if (power_best_index >= 0) {

                        /* Build message */
                        struct usb_pd_message request = {};
                        request.sop_type = USB_PD_SOP_TYPE_DEFAULT;
                        request.header = 0;
                        request.header |= (m_pd_next_message_id & 0b111) << 9;
                        request.header |= (USB_PD_PROTOCOL_REVISION_2_0 << 6);
                        request.header |= USB_PD_MESSAGE_TYPE_DATA_REQUEST;
                        uint16_t current_10ma = m_pd_current * 100;
                        request.object_count = 1;
                        request.objects[0] = 0;
                        request.objects[0] |= ((power_best_index + 1) << 28);
                        request.objects[0] |= (0 << 27);  // No GiveBack support for now
                        request.objects[0] |= (1 << 25);  // USB Communications Capable
                        request.objects[0] |= (1 << 24);  // No USB Suspend
                        request.objects[0] |= (current_10ma << 10);
                        request.objects[0] |= (current_10ma << 0);

                        /* Log */
                        log_i("Requesting pdo at index %u", power_best_index);

                        /* Send message */
                        res = m_fusb302.pd_message_send(request);
                        if (res < 0) {
                            log_e("Failed to request pdo (%d)!", res);
                            m_sm = STATE_PD_0;
                            m_errors_pd++;
                            break;
                        }

                        /* Move on */
                        m_timestamp = millis();
                        m_sm = STATE_PD_3;
                        break;
                    }
                }

                /* Handle other messages */
                else {
                    log_w("Unexpected pd message (1).");
                }
            }

            /* If no source capabilities message received after tFirstSourceCap (250ms), trigger a hard reset as per section 6.6.3.3 */
            if ((millis() - m_timestamp) >= 250) {

                /* Log */
                log_w("No source capabilities received.");

                /* Send hard reset sequence */
                res = m_fusb302.pd_reset_hard();
                if (res < 0) {
                    log_e("Failed to send hard reset sequence (%d)!", res);
                    m_sm = STATE_PD_0;
                    m_errors_pd++;
                    break;
                }

                /* Move on */
                m_sm = STATE_BC_0;
                break;
            }

            /* Stay here */
            break;
        }

        case STATE_PD_3: {

            /* Watch for timeout
             * @see Section 6.6.2
             * @see tReceiverResponse = 15 ms
             * @todo Revert to 15ms? */
            if ((millis() - m_timestamp) >= 115) {
                log_e("No response received!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Look for incoming messages */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming pd messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 1) {

                /* Log */
                log_d("Received pd message: sop=%d, header=0x%04X, object_count=%d", response.sop_type, response.header, response.object_count);
                for (unsigned int i = 0; i < response.object_count; i++) {
                    log_d(" - Object %u=0x%08X", i, response.objects[i]);
                }

                /* Handle good crc */
                if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count == 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_GOODCRC)) {
                    log_d("Good crc received.");

                    /* Increment message id */
                    m_pd_next_message_id = (m_pd_next_message_id + 1) & 0b111;
                }

                /* Handle accept message */
                else if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count == 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_ACCEPT)) {
                    log_i("Pdo request accepted.");

                    /* Move on */
                    m_timestamp = millis();
                    m_sm = STATE_PD_4;
                    break;
                }

                /* Handle reject message */
                else if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count == 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_REJECT)) {
                    log_w("Pdo request rejected.");
                    m_sm = STATE_PD_0;
                    m_errors_pd++;
                    break;
                }

                /* Handle other messages */
                else {
                    log_w("Unexpected pd message (2).");
                }
            }

            /* Otherwise stay in this state */
            break;
        }

        case STATE_PD_4: {

            /* Watch for timeout
             * @see Section 6.6.5.1
             * @see tPSTransition = 450 to 550 ms */
            if ((millis() - m_timestamp) >= 650) {
                log_e("No response received.");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            }

            /* Look for incoming messages */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming pd messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 1) {

                /* Log */
                log_d("Received pd message: sop=%d, header=0x%04X, object_count=%d", response.sop_type, response.header, response.object_count);
                for (unsigned int i = 0; i < response.object_count; i++) {
                    log_d(" - Object %u=0x%08X", i, response.objects[i]);
                }

                /* Handle good crc */
                if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count == 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_GOODCRC)) {
                    log_d("Good crc received.");

                    /* Increment message id */
                    m_pd_next_message_id = (m_pd_next_message_id + 1) & 0b111;
                }

                /* Handle ready message */
                else if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count == 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_PS_RDY)) {

                    /* Log */
                    log_i("Pd supply ready, delivering %.2fV %.2fA", m_pd_voltage, m_pd_current);

                    /* Add power option */
                    struct power_option option = {
                        .provider = POWER_PROVIDER_USB_PD,
                        .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                        .voltage_min = m_pd_voltage,
                        .voltage_max = m_pd_voltage,
                        .current_max = m_pd_current,
                        .power_max = m_pd_voltage * m_pd_current,
                    };
                    m_options_add(option);

                    /* Move on */
                    m_sm = STATE_PD_MONITOR;
                    break;
                }

                /* Handle other messages */
                else {
                    log_w("Unexpected pd message (3).");
                }
            }

            /* Otherwise stay in this state */
            break;
        }

        case STATE_PD_MONITOR: {

            /* Look for incoming messages and handle them.
             * This is required as USB-PD is a stateful protocol that needs constant communication.
             * The fusb302 will stop sending goodcrc responses if we don't process messages,
             * which would cause the source to think we've disconnected. */
            usb_pd_message response;
            res = m_fusb302.pd_message_receive(response);
            if (res < 0) {
                log_e("Failed to check for incoming pd messages!");
                m_sm = STATE_PD_0;
                m_errors_pd++;
                break;
            } else if (res == 1) {

                /* Log */
                log_d("Received pd message: sop=%d, header=0x%04X, object_count=%d", response.sop_type, response.header, response.object_count);
                for (unsigned int i = 0; i < response.object_count; i++) {
                    log_d(" - Object %u=0x%08X", i, response.objects[i]);
                }

                /* Handle good crc */
                if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count == 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_CONTROL_GOODCRC)) {
                    log_d("Good crc received.");

                    /* Increment message id */
                    m_pd_next_message_id = (m_pd_next_message_id + 1) & 0b111;
                }

                /* Handle source capabilities message.
                 * Some chargers will send another source capabilities message after negotiation.
                 * This allows us to renegotiate power if better options become available,
                 * for example when another device unplugs from a multi-port charger. */
                else if ((response.sop_type == USB_PD_SOP_TYPE_DEFAULT) && (response.object_count > 0) && ((response.header & 0b11111) == USB_PD_MESSAGE_TYPE_DATA_SOURCE_CAPABILITIES)) {

                    /* Parse each pdo looking for the most interesting one
                     * For now we only handle fixed pdos
                     * Ideally we want to find a pdo that gives us 45W with a voltage of less than 17V, but we can go up to 20V if needed */
                    double power_best = 0;
                    int8_t power_best_index = -1;
                    for (uint8_t i = 0; i < response.object_count; i++) {
                        switch (response.objects[i] >> 30U) {

                            case USB_PD_PDO_TYPE_FIXED: {
                                double voltage = ((response.objects[i] & 0x000FFC00) >> 10) * 0.05;
                                double current = ((response.objects[i] & 0x000001FF) >> 0) * 0.01;
                                double power_max = voltage * current;
                                double power_usable = power_max > 45 ? 45 : power_max;
                                log_i("Received fixed pdo %fV %fA", voltage, current);
                                if (voltage <= 17) {
                                    if (power_usable >= power_best) {
                                        power_best = power_usable;
                                        power_best_index = i;
                                        m_pd_current = power_usable / voltage;
                                        m_pd_voltage = voltage;
                                    }
                                } else if (voltage <= 20) {
                                    if (power_usable > power_best) {
                                        power_best = power_usable;
                                        power_best_index = i;
                                        m_pd_current = power_usable / voltage;
                                        m_pd_voltage = voltage;
                                    }
                                }
                                break;
                            }

                            default: {
                                log_w("Received unsupported pdo.");  // TODO: Add support for other pdo types?
                                break;
                            }
                        }
                    }

                    /* Request the most interesting pdo */
                    if (power_best_index >= 0) {

                        /* Build message */
                        struct usb_pd_message request = {};
                        request.sop_type = USB_PD_SOP_TYPE_DEFAULT;
                        request.header = 0;
                        request.header |= (m_pd_next_message_id & 0b111) << 9;
                        request.header |= (USB_PD_PROTOCOL_REVISION_2_0 << 6);
                        request.header |= USB_PD_MESSAGE_TYPE_DATA_REQUEST;
                        uint16_t current_10ma = m_pd_current * 100;
                        request.object_count = 1;
                        request.objects[0] = 0;
                        request.objects[0] |= ((power_best_index + 1) << 28);
                        request.objects[0] |= (0 << 27);  // No GiveBack support for now
                        request.objects[0] |= (1 << 25);  // USB Communications Capable
                        request.objects[0] |= (1 << 24);  // No USB Suspend
                        request.objects[0] |= (current_10ma << 10);
                        request.objects[0] |= (current_10ma << 0);

                        /* Log */
                        log_d("Requesting pdo at index %u", power_best_index);

                        /* Send message */
                        res = m_fusb302.pd_message_send(request);
                        if (res < 0) {
                            log_e("Failed to request pdo (%d)!", res);
                            m_sm = STATE_PD_0;
                            m_errors_pd++;
                            break;
                        }

                        /* Move on */
                        m_timestamp = millis();
                        m_sm = STATE_PD_3;
                        break;
                    }
                }

                /* Handle other messages */
                else {
                    log_w("Unexpected pd message (4).");
                }
            }

            /* Don't retry too many times */
            if (m_errors_pd > 5) {
                log_e("Too many pd errors!");
                m_sm = STATE_IDLE;
                break;
            }

            /* Monitor vbus periodically
             * @note While the FUSB302 is reading VBUS it cannot receive PD messages */
            static uint32_t m_timestamp_vbus_monitor = 0;
            if ((millis() - m_timestamp_vbus_monitor) >= 100) {
                m_timestamp_vbus_monitor = millis();

                /* Read vbus */
                float vbus = 0;
                res = m_fusb302.vbus_measure(vbus);
                if (res < 0) {
                    log_w("Failed to monitor vbus!");
                    m_errors_pd++;
                    break;
                } else {
                    m_errors_pd = 0;
                }

                /* Save max voltage in diagnostics */
                if (vbus > m_diag_voltage_max) {
                    m_diag_voltage_max = vbus;
                    settings_diagnotics_usb_voltage_max_report(m_diag_voltage_max);
                }

                /* Ensure vbus is within 20% of the expected voltage
                 * @note The FUSB302 has a resolution of 0.42V per step, so we need to allow for this when checking the voltage */
                if (((vbus + 0.42f) < (m_pd_voltage * 0.8)) || ((vbus - 0.42f) > (m_pd_voltage * 1.2))) {

                    /* Power source appears unstable, reverting to IDLE for full renegotiation.
                     * @note Future improvement: Could try falling back to lower power mode
                     * or previous working configuration instead of full restart */
                    log_w("Measured out of range %.2fV vbus while monitoring pd.", vbus);
                    m_sm = STATE_IDLE;
                    break;
                }
            }

            /* Stay here */
            break;
        }

        case STATE_BC_0: {

            /* Ensure the ic is detected */
            if (m_pi3usb9281.detect() != true) {
                log_e("Failed to detect pi3usb9281 ic!");
                m_sm = STATE_DONE;
                break;
            }

            /* Don't retry too many times */
            if (m_errors_bc > 5) {
                log_e("Too many bc errors!");
                m_sm = STATE_DONE;
                break;
            }

            /* Move on */
            m_sm = STATE_BC_1;
            break;
        }

        case STATE_BC_1: {

            /* Inspired by the chromebook-ec provider code, perform a debounce.
             * @see https://git.furworks.de/coreboot-mirror/chrome-ec/src/commit/1e800ac838504c0d2950c7aa90cdfe7bde251545/driver/bc12/pi3usb9281.c#L314 */
            res = m_pi3usb9281.switch_state_set(PI3USB9281C_SWITCH_STATE_MANUAL_OPEN);
            if (res < 0) {
                log_e("Failed to configure usb switches!");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_BC_2;
            break;
        }

        case STATE_BC_2: {

            /* Leave the switches open for 2 seconds before performing a new detection,
             * less than that seems to not reliably detect some chargers. */
            if ((millis() - m_timestamp) < 2000) {
                break;
            }

            /* Reset ic which will automatically perform a new detection */
            res = m_pi3usb9281.reset();
            if (res < 0) {
                log_e("Failed to reset pi3usb9281 ic!");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_BC_3;
            break;
        }

        case STATE_BC_3: {

            /* Wait */
            if ((millis() - m_timestamp) < 15) {
                break;
            }

            /* Watch for timeout */
            if ((millis() - m_timestamp) >= 1500) {
                log_w("Timed out when detecting a usb device, trying again...");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Wait for a device attach event */
            res = m_pi3usb9281.device_attach_get();
            if (res < 0) {
                log_w("Failed to detect a usb device, trying again...");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            } else if (res == 0) {
                break;
            }

            /* Move on */
            m_sm = STATE_BC_4;
            break;
        }

        case STATE_BC_4: {

            /* Retrieve type of device */
            enum pi3usb9281c_device_type type;
            res = m_pi3usb9281.device_type_get(&type);
            if (res < 0) {
                log_w("Failed to determine type of usb device attached, trying again...");
                m_sm = STATE_BC_0;
                m_errors_bc++;
                break;
            }

            /* Determine current limit */
            float current = 0;
            switch (type) {
                case PI3USB9281C_DEVICE_TYPE_USB_CDP: {
                    log_i("Detected usb device of type cdp (1.5A).");
                    current = 1.5f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_USB_DCP: {
                    log_i("Detected usb device of type dcp (1.5A).");
                    current = 1.5f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_1A: {
                    log_i("Detected usb device of type charger (1.0A).");
                    current = 1.0f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_2A: {
                    log_i("Detected usb device of type charger (2.0A).");
                    current = 2.0f;
                    break;
                }
                case PI3USB9281C_DEVICE_TYPE_CHARGER_2_4A: {
                    log_i("Detected usb device of type charger (2.4A).");
                    current = 2.4f;
                    break;
                }
                default: {
                    log_i("Detected usb device of type sdp (0.5A).");
                    current = 0.5f;
                    break;
                }
            }

            /* Add power option */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_BC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 5.0f,
                .voltage_max = 5.0f,
                .current_max = current,
                .power_max = 5.0f * current,
            };
            m_options_add(option);

            /* Try HVDCP handshake if we detected a DCP charger during BC1.2 detection
             * as HVDCP builds on top of standard DCP functionality */
            if ((type == PI3USB9281C_DEVICE_TYPE_USB_DCP) ||     //
                (type == PI3USB9281C_DEVICE_TYPE_CHARGER_1A) ||  //
                (type == PI3USB9281C_DEVICE_TYPE_CHARGER_2A) ||  //
                (type == PI3USB9281C_DEVICE_TYPE_CHARGER_2_4A)) {
                m_sm = STATE_QC_0;
                break;
            }

            /* Move on */
            m_sm = STATE_DONE;
            break;
        }

        case STATE_QC_0: {

            /* Don't retry too many times */
            if (m_errors_qc > 5) {
                log_e("Too many qc errors!");
                m_sm = STATE_DONE;
                break;
            }

            /* Move on */
            m_sm = STATE_QC_1;
            break;
        }

        case STATE_QC_1: {

            /* Reset all GPIO pins used for D+/D- voltage control.
             * We use a resistor network to generate specific voltage levels. */
            pinMode(m_qc_dn_h_pin, INPUT);
            pinMode(m_qc_dn_m_pin, INPUT);
            pinMode(m_qc_dn_l_pin, INPUT);
            pinMode(m_qc_dp_h_pin, INPUT);
            pinMode(m_qc_dp_m_pin, INPUT);
            pinMode(m_qc_dp_l_pin, INPUT);

            /* Route USB D+/D- to our voltage control network instead of USB data lines */
            if (m_fsusb43.output_select(FSUSB43_OUTPUT_2) < 0 ||
                m_pi3usb9281.switch_state_set(PI3USB9281C_SWITCH_STATE_MANUAL_CLOSED) < 0) {
                log_e("Failed to route signals!");
                m_sm = STATE_QC_0;
                m_errors_qc++;
                break;
            }

            /* Move on */
            m_sm = STATE_QC_2;
            break;
        }

        case STATE_QC_2: {

            /* Begin HVDCP handshake step 1.
             * To advertise that we are a HVDCP compliant sink device, we must present 0.325V-2V on D+ for at least 1.25 seconds.
             * During this time, the source may keep D+ and D- shorted. */
            pinMode(m_qc_dp_h_pin, OUTPUT);
            pinMode(m_qc_dp_l_pin, OUTPUT);
            digitalWrite(m_qc_dp_h_pin, HIGH);
            digitalWrite(m_qc_dp_l_pin, LOW);

            /* Configure ADC to monitor D- voltage for PSU response */
            analogRead(A3);

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_QC_3;
            break;
        }

        case STATE_QC_3: {

            /* Wait minimum 1s before checking PSU response */
            if ((millis() - m_timestamp) < 1000) {
                break;
            }

            /* Continue with HVDCP handshake step 2.
             * Source should discharge D- through pull-down resistor.
             * We monitor D- voltage and wait for it to drop below 0.2V.
             * Timeout after 3s if source doesn't respond. */
            float pin_dn_voltage = (3.3 * analogRead(A3)) / 1023.0;
            if (pin_dn_voltage > 0.2) {
                if ((millis() - m_timestamp) > 3000) {
                    log_w("HVDCP handshake timed out.");
                    m_sm = STATE_DONE;
                    break;
                } else {
                    break;
                }
            }
            log_i("HVDCP handshake completed.");

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_QC_4;
            break;
        }

        case STATE_QC_4: {

            /* Small delay before voltage request
             * Not sure why, but let's leave it here for now */
            if ((millis() - m_timestamp) < 2) {
                break;
            }

            /* Request 12V output by setting: D+: 0.325V-2V, D-: 0.325V-2V */
            log_d("Asking for 12V");
            pinMode(m_qc_dp_h_pin, OUTPUT);
            pinMode(m_qc_dp_l_pin, OUTPUT);
            digitalWrite(m_qc_dp_h_pin, HIGH);
            digitalWrite(m_qc_dp_l_pin, LOW);
            pinMode(m_qc_dn_h_pin, OUTPUT);
            pinMode(m_qc_dn_l_pin, OUTPUT);
            digitalWrite(m_qc_dn_h_pin, HIGH);
            digitalWrite(m_qc_dn_l_pin, LOW);

            /* Move on */
            m_qc_voltage = 12;
            m_qc_current = 1.5;
            m_timestamp = millis();
            m_sm = STATE_QC_5;
            break;
        }

        case STATE_QC_5: {

            /* Wait for voltage transition (60ms) */
            if ((millis() - m_timestamp) < 60) {
                break;
            }

            /* Verify VBUS is at 12V ±20% */
            float vbus = 0;
            res = m_fusb302.vbus_measure(vbus);
            if (res < 0) {
                log_w("Failed to read vbus!");
                m_errors_qc++;
                m_sm = STATE_QC_0;
                break;
            }
            if (((vbus + 0.42f) < (m_qc_voltage * 0.8)) || ((vbus - 0.42f) > (m_qc_voltage * 1.2))) {
                log_w("HVDCP Invalid voltage (measured %.2fV instead of %.2fV).", vbus, m_qc_voltage);
                m_sm = STATE_QC_6;
                break;
            }

            /* Add 12V/1.5A power option to available power sources */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_QC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 12.0,
                .voltage_max = 12.0,
                .current_max = 1.5,
                .power_max = 12.0f * 1.5f,
            };
            m_options_add(option);

            /* Move on */
            m_sm = STATE_QC_MONITOR;
            break;
        }

        case STATE_QC_6: {

            /* Request 9V output by setting: D+ >2V, D- 0.325V-2V */
            log_d("Asking for 9V");
            pinMode(m_qc_dp_h_pin, OUTPUT);
            pinMode(m_qc_dp_l_pin, OUTPUT);
            digitalWrite(m_qc_dp_h_pin, HIGH);
            digitalWrite(m_qc_dp_l_pin, HIGH);
            pinMode(m_qc_dn_h_pin, OUTPUT);
            pinMode(m_qc_dn_l_pin, OUTPUT);
            digitalWrite(m_qc_dn_h_pin, HIGH);
            digitalWrite(m_qc_dn_l_pin, LOW);

            /* Move on */
            m_qc_voltage = 9;
            m_qc_current = 2.0;
            m_timestamp = millis();
            m_sm = STATE_QC_7;
            break;
        }

        case STATE_QC_7: {

            /* Wait for voltage transition (60ms) */
            if ((millis() - m_timestamp) < 60) {
                break;
            }

            /* Verify VBUS is at 9V ±20% */
            float vbus = 0;
            res = m_fusb302.vbus_measure(vbus);
            if (res < 0) {
                log_w("Failed to read vbus!");
                m_errors_qc++;
                m_sm = STATE_QC_0;
                break;
            }
            if (((vbus + 0.42f) < (m_qc_voltage * 0.8)) || ((vbus - 0.42f) > (m_qc_voltage * 1.2))) {
                log_w("HVDCP Invalid voltage (measured %.2fV instead of %.2fV).", vbus, m_qc_voltage);
                m_sm = STATE_DONE;
                break;
            }

            /* Add 9V/2.0A power option to available power sources */
            struct power_option option = {
                .provider = POWER_PROVIDER_USB_QC,
                .type = POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,
                .voltage_min = 9.0,
                .voltage_max = 9.0,
                .current_max = 2.0,
                .power_max = 9.0f * 2.0f,
            };
            m_options_add(option);

            /* Move on */
            m_sm = STATE_QC_MONITOR;
            break;
        }

        case STATE_QC_MONITOR: {

            /* Don't retry too many times */
            if (m_errors_qc > 5) {
                log_e("Too many qc errors!");
                m_sm = STATE_IDLE;
                break;
            }

            /* Monitor vbus periodically
             * @note While the FUSB302 is reading VBUS it cannot receive PD messages */
            static uint32_t m_timestamp_vbus_monitor = 0;
            if ((millis() - m_timestamp_vbus_monitor) >= 100) {
                m_timestamp_vbus_monitor = millis();

                /* Read vbus */
                float vbus = 0;
                res = m_fusb302.vbus_measure(vbus);
                if (res < 0) {
                    log_w("Failed to monitor vbus!");
                    m_errors_qc++;
                    break;
                } else {
                    m_errors_qc = 0;
                }

                /* Save max voltage in diagnostics */
                if (vbus > m_diag_voltage_max) {
                    m_diag_voltage_max = vbus;
                    settings_diagnotics_usb_voltage_max_report(m_diag_voltage_max);
                }

                /* Ensure vbus is within 20% of the expected voltage
                 * @note The FUSB302 has a resolution of 0.42V per step, so we need to allow for this when checking the voltage */
                if (((vbus + 0.42f) < (m_qc_voltage * 0.8)) || ((vbus - 0.42f) > (m_qc_voltage * 1.2))) {

                    /* Power source appears unstable, reverting to IDLE for full renegotiation.
                     * @note Future improvement: Could try falling back to BC1.2 mode (DCP)
                     * instead of full restart since we know DCP was detected */
                    log_w("Measured out of range %.2fV vbus while monitoring hvdcp.", vbus);
                    m_sm = STATE_IDLE;
                    break;
                }
            }

            /* Stay here */
            break;
        }

        case STATE_DONE: {

            /* Monitor vbus periodically
             * @note While the FUSB302 is reading VBUS it cannot receive PD messages */
            static uint32_t m_timestamp_vbus_monitor = 0;
            if ((millis() - m_timestamp_vbus_monitor) >= 100) {
                m_timestamp_vbus_monitor = millis();

                /* Read vbus */
                float vbus = 0;
                res = m_fusb302.vbus_measure(vbus);
                if (res < 0) {
                    break;
                }

                /* Save max voltage in diagnostics */
                if (vbus > m_diag_voltage_max) {
                    m_diag_voltage_max = vbus;
                    settings_diagnotics_usb_voltage_max_report(m_diag_voltage_max);
                }
            }

            /* Stay here */
            break;
        }

        default: {
            log_e("Hurray, we found a cosmic ray!");
            m_sm = STATE_IDLE;
            return -1;
        }
    }

    /* Return success */
    return 0;
}
