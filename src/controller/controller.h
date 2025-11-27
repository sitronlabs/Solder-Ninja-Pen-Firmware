#ifndef CONTROLLER_H
#define CONTROLLER_H

/* Config */
#include "../cfg/config.h"

/* Project headers */
#include "../interface/accelerometer.h"

/* Setup */
int controller_setup(void);

/* State */
enum controller_state {
    CONTROLLER_STATE_LOCKED,
    CONTROLLER_STATE_ASLEEP,
    CONTROLLER_STATE_ACTIVE,
};
enum controller_state controller_state_get(void);

/* Sleeping */
enum controller_sleep_reason {
    CONTROLLER_SLEEP_REASON_MOTION,
    CONTROLLER_SLEEP_REASON_MAGNET,
};
int controller_sleep(enum controller_sleep_reason reason);
enum controller_wake_reason {
    CONTROLLER_WAKE_REASON_BUTTONS,
    CONTROLLER_WAKE_REASON_MOTION,
    CONTROLLER_WAKE_REASON_MAGNET,
};
int controller_wake(enum controller_wake_reason reason);

/* Locking */
enum controller_lock_reason {
    CONTROLLER_LOCK_REASON_BUTTONS,
    CONTROLLER_LOCK_REASON_REMOTE,
    CONTROLLER_LOCK_REASON_FREFALL,
};
int controller_lock(enum controller_lock_reason reason);
enum controller_unlock_reason {
    CONTROLLER_UNLOCK_REASON_BUTTONS,
    CONTROLLER_UNLOCK_REASON_REMOTE,
};
int controller_unlock(enum controller_unlock_reason reason);

/* Target temperature */
int controller_temperature_target_get(float &temperature_c);
int controller_temperature_target_set(const float temperature_c);
int controller_temperature_target_increase(void);
int controller_temperature_target_decrease(void);

/* Measured temperature */
int controller_temperature_measured_get(float &temperature_c);

/* Boost */
bool controller_boost_activated_get(void);

// /**
//  * Requets to turn on heating.
//  * Heating might start with some delay if the available contracts need additional negotiation.
//  * Function call should typically originate from either the user interface of the communication link.
//  * @return 0 in case of success, or a negative error code otherwise, in particular:
//  * -ERROR_NOT_ENOUGH_POWER if none of the power contracts offer the minimum amount of power required by the board.
//  */
// int controller_heating_turn_on(void);

// /**
//  * Requets to turn off heating.
//  * Heating is expected to stop immediately.
//  * Function call should typically originate from either the user interface of the communication link.
//  * @return 0 in case of success.
//  */
// int controller_heating_turn_off(void);

/* Task */
int controller_task(void);

#endif
