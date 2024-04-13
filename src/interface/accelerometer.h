#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

/* C/C++ libraries */
#include <errno.h>

/* Prototypes */
int accelerometer_setup(void);
int accelerometer_idle_reset(void);
int accelerometer_idle_detected_get(void);
int accelerometer_task(void);

#endif
