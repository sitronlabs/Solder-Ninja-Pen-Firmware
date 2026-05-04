#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

/* C/C++ libraries */
#include <errno.h>
#include <stdint.h>

/* Config */
#include "../cfg/config.h"
#ifndef CONFIG_ACCEL_IDLE_DURATION_MIN
#define CONFIG_ACCEL_IDLE_DURATION_MIN 10000  //!< Minimum time (10 seconds) after which no movement of the device will be interpreted as inactivity (in milliseconds).
#endif
#ifndef CONFIG_ACCEL_IDLE_DURATION_MAX
#define CONFIG_ACCEL_IDLE_DURATION_MAX 300000  //!< Maximum time (5 minutes) after which no movement of the device will be interpreted as inactivity (in milliseconds).
#endif
#ifndef CONFIG_ACCEL_IDLE_DURATION_DEFAULT
#define CONFIG_ACCEL_IDLE_DURATION_DEFAULT 40000  //!< Time after which no movement of the device will be interpreted as inactivity (in milliseconds).
#endif
#ifndef CONFIG_ACCEL_IDLE_ACCELERATION_MIN
#define CONFIG_ACCEL_IDLE_ACCELERATION_MIN 16  //!< Minimum idle acceleration (milli-g); motion below this does not reset inactivity.
#endif
#ifndef CONFIG_ACCEL_IDLE_ACCELERATION_MAX
#define CONFIG_ACCEL_IDLE_ACCELERATION_MAX 992  //!< Maximum idle acceleration (milli-g).
#endif
#ifndef CONFIG_ACCEL_IDLE_ACCELERATION_DEFAULT
#define CONFIG_ACCEL_IDLE_ACCELERATION_DEFAULT 160  //!< Default idle acceleration (milli-g).
#endif
#ifndef CONFIG_ACCEL_ODR_HZ
#define CONFIG_ACCEL_ODR_HZ 50  //!< Output data rate of the accelerometer (in Hz).
#endif
#ifndef CONFIG_ACCEL_FALL_ACCELERATION_MIN
#define CONFIG_ACCEL_FALL_ACCELERATION_MIN 16  //!< Minimum freefall acceleration threshold (milli-g).
#endif
#ifndef CONFIG_ACCEL_FALL_ACCELERATION_MAX
#define CONFIG_ACCEL_FALL_ACCELERATION_MAX 992  //!< Maximum freefall threshold (milli-g).
#endif
#ifndef CONFIG_ACCEL_FALL_ACCELERATION_DEFAULT
#define CONFIG_ACCEL_FALL_ACCELERATION_DEFAULT 336  //!< Default freefall acceleration threshold (milli-g).
#endif
#ifndef CONFIG_ACCEL_FALL_DURATION_MIN_MS
#define CONFIG_ACCEL_FALL_DURATION_MIN_MS 20  //!< Minimum freefall event duration (in milliseconds).
#endif
#ifndef CONFIG_ACCEL_FALL_DURATION_MAX_MS
#define CONFIG_ACCEL_FALL_DURATION_MAX_MS 200  //!< Maximum freefall event duration (in milliseconds).
#endif
#ifndef CONFIG_ACCEL_FALL_DURATION_DEFAULT_MS
#define CONFIG_ACCEL_FALL_DURATION_DEFAULT_MS 40  //!< Default freefall event duration (in milliseconds).
#endif

/* Setup */
int accelerometer_setup(void);

/* Idle */
int accelerometer_idle_duration_get(uint32_t &duration_ms);
int accelerometer_idle_duration_set(const uint32_t duration_ms);
int accelerometer_idle_acceleration_get(uint32_t &acceleration_mg);
int accelerometer_idle_acceleration_set(const uint32_t acceleration_mg);
int accelerometer_idle_acceleration_step_get(uint32_t &step_mg);
int accelerometer_idle_clear(void);
int accelerometer_idle_detected_get(void);

/* Wake */
int accelerometer_wake_clear(void);
int accelerometer_wake_detected_get(void);

/* Freefall */
int accelerometer_fall_duration_get(uint32_t &duration_ms);
int accelerometer_fall_duration_set(const uint32_t duration_ms);
int accelerometer_fall_duration_step_get(uint32_t &step_ms);
int accelerometer_fall_acceleration_get(uint32_t &acceleration_mg);
int accelerometer_fall_acceleration_set(const uint32_t acceleration_mg);
int accelerometer_fall_acceleration_step_get(uint32_t &step_mg);
int accelerometer_fall_clear(void);
int accelerometer_fall_detected_get(void);

/* Task */
int accelerometer_task(void);

#endif
