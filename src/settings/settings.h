#ifndef SETTINGS_H
#define SETTINGS_H

/* Project */
#include "../errors/errors.h"

/* C/C++ libraries */
#include <stddef.h>
#include <stdint.h>

/* */
int settings_setup(void);
int settings_memory_read(const size_t address, uint8_t *const data, const size_t length);
int settings_memory_wipe(void);
int settings_temperature_get(float &temperature_c);
int settings_temperature_set(const float temperature_c);
int settings_user_get(uint8_t *const icon, char *const line1, char *const line2);
int settings_user_set(const uint8_t icon[32], const char *line1, const char *line2);

#endif
