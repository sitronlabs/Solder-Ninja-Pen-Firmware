#ifndef POWER_H
#define POWER_H

/* Setup */
int power_setup(void);

/* Contract */
struct power_contract {
    enum { FIXED_VOLTAGE_LIMITED_CURRENT,   // Table 6-9 Fixed Supply PDO - Source
           VARIABLE_VOLTAGE_FIXED_CURRENT,  // Table 6-11 Variable Supply (non-Battery) PDO - Source
           VARIABLE_VOLTAGE_FIXED_POWER,    // Table 6-12 Battery Supply PDO - Source
    } type;
    float voltage_min;
    float voltage_max;
    float current_max;
    float power_max;
};
int power_contract_get(struct power_contract *contract);

/* Periodic task */
int power_task(void);

#endif
