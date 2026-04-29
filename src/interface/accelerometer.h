#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

/* C/C++ libraries */
#include <errno.h>
#include <stdint.h>

/* Config */
#include "../cfg/config.h"
#ifndef CONFIG_ACCEL_IDLE_TIME_MIN
#define CONFIG_ACCEL_IDLE_TIME_MIN 10000  //!< Minimum time after which no movement of the device will be interpreted as inactivity (in milliseconds).
#endif
#ifndef CONFIG_ACCEL_IDLE_TIME_MAX
#define CONFIG_ACCEL_IDLE_TIME_MAX 40000  //!< Maximum time after which no movement of the device will be interpreted as inactivity (in milliseconds).
#endif
#ifndef CONFIG_ACCEL_IDLE_TIME_DEFAULT
#define CONFIG_ACCEL_IDLE_TIME_DEFAULT 40000  //!< Time after which no movement of the device will be interpreted as inactivity (in milliseconds).
#endif
#define CONFIG_ACCEL_IDLE_ACCELERATION_THRESHOLD 0.160  //!< Threshold under which acceleration will not be registered as movement (in g, after high pass filter).
#define CONFIG_ACCEL_FALL_ACCELERATION_THRESHOLD 0.336  //!< Threshold under which acceleration will be considered as free-fall (in g, absolute).

/* Setup */
int accelerometer_setup(void);

/* Idle */
uint32_t accelerometer_idle_time_get(void);
int accelerometer_idle_time_set(const uint32_t time_ms);
int accelerometer_idle_clear(void);
int accelerometer_idle_detected_get(void);

/* Wake */
int accelerometer_wake_clear(void);
int accelerometer_wake_detected_get(void);

/* Freefall */
int accelerometer_fall_clear(void);
int accelerometer_fall_detected_get(void);

/* Task */
int accelerometer_task(void);

#endif
