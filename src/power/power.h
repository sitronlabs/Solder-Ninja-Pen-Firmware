/**
 * @file power.h
 * @brief Power management implementation for USB power negotiation and regulation
 *
 * This module provides comprehensive USB power negotiation and regulation including:
 * - USB Type-C detection and orientation
 * - USB Power Delivery (PD) negotiation with source capability parsing
 * - USB Battery Charging (BC1.2) detection (SDP/CDP/DCP)
 * - HVDCP (High Voltage Dedicated Charging Port) negotiation for higher voltages
 * - Buck converter regulation based on available power
 * - Multi-protocol power source management
 *
 * The power management system implements a hierarchical negotiation strategy:
 * 1. USB Type-C basic power detection (0.5A, 1.5A, 3.0A)
 * 2. USB Power Delivery negotiation for higher power contracts
 * 3. USB Battery Charging detection for legacy chargers
 * 4. HVDCP negotiation for high-voltage charging
 *
 * The system automatically selects the best available power source and regulates
 * the buck converter output to match the negotiated power capabilities.
 */

#ifndef POWER_H
#define POWER_H

/**
 * @brief Initialize the power management system
 *
 * Sets up all power-related peripherals including USB Type-C controller,
 * Power Delivery PHY, charger detection IC, and buck converter DAC.
 * Must be called before any power management functions are used.
 *
 * @return 0 on success, negative error code on failure
 *
 * @note This function initializes multiple ICs and should be called during system startup
 */
int power_setup(void);

/**
 * @brief Reads current drawn by the DC-DC converter from the USB input path.
 *
 * The INA219 measures the converter input branch, not the current through the tip.
 *
 * @param[out] current_a Current in amperes
 * @return 0 on success, negative error code on failure
 */
int power_current_get(float &current_a);

/**
 * @brief Gets the tip resistance currently used for buck regulation.
 *
 * Returns the selected nominal resistance, or the configured default when a
 * valid measurement is unavailable.
 *
 * @param[out] resistance_ohms Current tip resistance in ohms
 * @return 0 on success
 */
int power_tip_resistance_get(float &resistance_ohms);

/**
 * @brief Applies the configured tip resistance offset to regulation.
 * @return 0 on success, negative error code if buck regulation fails
 */
int power_tip_resistance_offset_apply(void);

/**
 * @brief Applies a changed heating current limit to the buck converter.
 * @return 0 on success, negative error code if buck regulation fails
 */
int power_heating_current_limit_configure(void);

/**
 * @brief Advances the non-blocking tip resistance measurement.
 *
 * Call periodically while the measurement is active. The probe is skipped when
 * USB-PD power is unavailable or its safety budget is insufficient.
 *
 * @return 0 while waiting or collecting samples, 1 when a valid measurement
 *         completes, 2 when the measurement is skipped or falls back to the
 *         default resistance, negative error code on failure
 */
int power_tip_resistance_measure_task(void);

/**
 * @brief Cancels an active tip resistance measurement.
 *
 * Disables the probe output and restores buck regulation using the available
 * power and currently selected tip resistance.
 *
 * @return 0 on success, negative error code if buck regulation cannot be restored
 */
int power_tip_resistance_measure_cancel(void);

/**
 * @brief Reduces buck output in response to measured USB input overcurrent.
 *
 * Uses the measured current ratio and inverse DAC transfer function to lower
 * the buck output in bounded steps. Returns an error if the output is already
 * at its minimum and measured current still exceeds the limit.
 *
 * @param[in] current_a Measured current in amperes
 * @param[in] current_limit_a Maximum allowed current in amperes
 * @return 0 if no correction is needed or the correction succeeds; negative on failure
 */
int power_heating_current_limit_apply(const float current_a, const float current_limit_a);

/**
 * @brief Sets the buck output corresponding to a requested USB input-current limit.
 *
 * Converts the requested current to a buck voltage and applies the closest
 * supported DAC code. This is used to create the soft-start current ramp.
 *
 * @param[in] current_limit_a Requested USB input-current limit in amperes
 * @param[out] applied_current_limit_a Modeled limit after DAC quantization and range limits
 * @param[out] minimum_voltage_reached True when DAC code 255 is selected
 * @return 0 on success, negative error code otherwise
 */
int power_heating_current_limit_ramp_apply(const float current_limit_a, float &applied_current_limit_a, bool &minimum_voltage_reached);

/**
 * @brief Reads the bus voltage feeding the DC-DC converter.
 *
 * @param[out] voltage_v Voltage in volts
 * @return 0 on success, negative error code on failure
 */
int power_voltage_get(float &voltage_v);

/**
 * @brief Power source provider enumeration
 *
 * Defines the different USB power negotiation protocols and sources
 * that the system can detect and negotiate with. Each provider has
 * different capabilities and negotiation methods.
 */
enum power_provider {
    POWER_PROVIDER_USB_BC,  //!< USB Battery Charging 1.2 - Legacy charging standard (0.5A/1.5A/2.4A)
    POWER_PROVIDER_USB_TC,  //!< USB Type-C - Basic Type-C power (5V, up to 3A max)
    POWER_PROVIDER_USB_PD,  //!< USB Power Delivery - Advanced power negotiation (up to 100W)
    POWER_PROVIDER_USB_QC,  //!< HVDCP (High Voltage Dedicated Charging Port) - High voltage charging (9V/12V)
    POWER_PROVIDER_USB_VO,  //!< Voltage Open - Multi-step constant-current charging protocol
};

/**
 * @brief Power source type enumeration
 *
 * Defines the different types of power sources and their characteristics.
 * Each type has different voltage/current/power characteristics and constraints.
 * Used for both USB Power Delivery PDOs and other power source descriptions.
 */
enum power_type {
    POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,   //!< Fixed voltage with current limit (USB PD Table 6-9)
    POWER_TYPE_VARIABLE_VOLTAGE_FIXED_CURRENT,  //!< Variable voltage with fixed current (USB PD Tables 6-8, 6-11)
    POWER_TYPE_VARIABLE_VOLTAGE_FIXED_POWER,    //!< Variable voltage with fixed power (USB PD Tables 6-9, 6-12)
};

/**
 * @brief Power option structure defining available power capabilities
 *
 * Represents a single power source option with its characteristics.
 * The system maintains a list of available power options and selects
 * the best one based on voltage, current, and power requirements.
 */
struct power_option {
    enum power_provider provider;  //!< Power source protocol/provider type
    enum power_type type;          //!< Power delivery object type (PDO type)
    float voltage_min;             //!< Minimum supported voltage (V)
    float voltage_max;             //!< Maximum supported voltage (V)
    float current_max;             //!< Maximum supported current (A)
    float power_max;               //!< Maximum supported power (W)
};

/**
 * @name Power Contract Management
 * @brief Functions for retrieving power contract information
 * @{
 */

/**
 * @brief Get the currently active power contract
 *
 * Retrieves the power option that is currently being used to supply power
 * to the system. This represents the negotiated power source capabilities.
 *
 * @param[out] contract Reference to power_option structure to fill with contract details
 * @return 0 on success, negative error code on failure
 */
int power_contract_get(struct power_option &contract);

/**
 * @brief Get the negotiated power limit in watts
 *
 * Returns the maximum power that can be drawn from the current power source
 * based on the negotiated contract. This value is used for buck converter regulation.
 *
 * @param[out] power_limit Reference to float to store power limit in watts
 * @return 0 on success, negative error code on failure
 */
int power_negotiated_power_limit_get(float &power_limit);

/**
 * @brief Get the negotiated current limit in amperes with the configured safety factor applied.
 *
 * @param[out] current_limit_a Current limit in amperes
 * @return 0 on success, negative error code on failure
 */
int power_negotiated_current_limit_get(float &current_limit_a);

/** @} */

/**
 * @name Power Control
 * @brief Functions for controlling power delivery
 * @{
 */

/**
 * @brief Enable or disable power delivery to the heating element
 *
 * Controls whether power is delivered to the heating element. When disabled,
 * the heating element will not receive power regardless of available power sources.
 *
 * @param[in] enabled True to enable power delivery, false to disable
 * @return 0 on success, negative error code on failure
 */
int power_enabled_set(const bool enabled);

/** @} */

/**
 * @name Power Management Task
 * @brief Main power management functions
 * @{
 */

/**
 * @brief Main power management task
 *
 * This is the primary power management function that should be called periodically
 * from the main application loop. It handles the complete power negotiation state machine
 * including USB Type-C detection, Power Delivery negotiation, Battery Charging detection,
 * HVDCP negotiation, and buck converter regulation.
 *
 * The function implements a hierarchical power negotiation strategy:
 * 1. USB Type-C basic power detection
 * 2. USB Power Delivery negotiation
 * 3. USB Battery Charging detection
 * 4. HVDCP negotiation
 *
 * @return 0 on success, negative error code on failure
 */
int power_task(void);

/** @} */

#endif
