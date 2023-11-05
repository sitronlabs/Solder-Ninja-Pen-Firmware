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

#endif
