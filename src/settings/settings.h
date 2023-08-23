#ifndef SETTINGS_H
#define SETTINGS_H

/* Project code */
#include "../errors/errors.h"

/**
 * @return 1 in case of success, 0 if the settings have not been fully loaded yet, or a negative error code otherwise, in particular:
 *  -ERROR_I2C_COMMUNICATION
 */
int settings_loaded(void);

#endif