#ifndef POWER_H
#define POWER_H

/* Setup */
int power_setup(void);

/* List of power providers */
enum power_provider {
    POWER_PROVIDER_USB_BC,  //!< Battery Charging 1.2
    POWER_PROVIDER_USB_TC,  //!< Type-C (5V 3A max)
    POWER_PROVIDER_USB_PD,  //!< Power Delivery
    POWER_PROVIDER_USB_QC,  //!< High Voltage Dedicated Charging Port
    POWER_PROVIDER_USB_VO,  //!< Voltage Open Multi-Step Constant-Current Charging
};

/* List of power types */
enum power_type {
    POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,   //!< Table 6-9 Fixed Supply PDO - Provider
    POWER_TYPE_VARIABLE_VOLTAGE_FIXED_CURRENT,  //!< Table 6-11 Variable Supply (non-Battery) PDO - Provider / Table 6-8 Variable Supply (non-Battery) PDO - Source
    POWER_TYPE_VARIABLE_VOLTAGE_FIXED_POWER,    //!< Table 6-12 Battery Supply PDO - Provider                / Table 6-9 Battery Supply PDO - Source
};

/* */
struct power_option {
    enum power_provider provider;
    enum power_type type;
    float voltage_min;
    float voltage_max;
    float current_max;
    float power_max;
};

/* Contract */
int power_contract_get(struct power_option &contract);
int power_negotiated_power_limit_get(float &power_limit);

/* Periodic task */
int power_task(void);

#endif
