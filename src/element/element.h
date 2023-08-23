#ifndef ELEMENT_H
#define ELEMENT_H

/* Functions related to measuring */
int element_connected_get(void);
int element_temperature_measured_get(float &temperature_c);

/* Functions related to heating */
int element_temperature_target_get(float &temperature_c);
int element_temperature_target_set(const float temperature_c);
int element_heating_enable(void);
int element_heating_disable(void);

/* Periodic task */
int element_task(void);

#endif
