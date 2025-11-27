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
