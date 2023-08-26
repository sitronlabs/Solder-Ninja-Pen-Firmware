#ifndef POWER_H
#define POWER_H

/* Setup */
int power_setup(void);

/* */
enum power_source {
    POWER_SOURCE_USB_BC,  //!< USB Battery Charging
    POWER_SOURCE_USB_TC,  //!< USB Type-C (5V 3A max)
    POWER_SOURCE_USB_PD,  //!< USB Power Delivery
    POWER_SOURCE_USB_QC,  //!< High Voltage Dedicated Charging Port
} source;

/* */
enum power_type {
    POWER_TYPE_FIXED_VOLTAGE_LIMITED_CURRENT,   //!< Table 6-9 Fixed Supply PDO - Source
    POWER_TYPE_VARIABLE_VOLTAGE_FIXED_CURRENT,  //!< Table 6-11 Variable Supply (non-Battery) PDO - Source
    POWER_TYPE_VARIABLE_VOLTAGE_FIXED_POWER,    //!< Table 6-12 Battery Supply PDO - Source
};

/* Contract */
struct power_contract {
    enum power_source source;
    enum power_type type;
    float voltage_min;
    float voltage_max;
    float current_max;
    float power_max;
};
int power_contract_get(struct power_contract *contract);

/* Periodic task */
int power_task(void);

#endif
