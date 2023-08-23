#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

/* C/C++ libraries */
#include <errno.h>

/* Prototypes */
int accelerometer_setup(void);
int accelerometer_movement_detected_get(void);

#endif
