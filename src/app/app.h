#ifndef APP_H
#define APP_H

/* Setup */
int app_setup(void);

/* State */
enum app_state {
    APP_STATE_LOCKED,
    APP_STATE_ASLEEP,
    APP_STATE_HEATING,  // Conditions for going to heating : have a connected element, no locks
};
enum app_state app_state_get(void);

/* Sleeping */
int app_sleep(void);
int app_wake(void);

/* Locking */
int app_lock(void);
int app_unlock(void);

/* Target temperature */
float app_target_get(void);
int app_target_increase(void);
int app_target_decrease(void);

/* Boost */
bool app_boost_activated_get(void);

// /**
//  * Requets to turn on heating.
//  * Heating might start with some delay if the available contracts need additional negotiation.
//  * Function call should typically originate from either the user interface of the communication link.
//  * @return 0 in case of success, or a negative error code otherwise, in particular:
//  * -ERROR_NOT_ENOUGH_POWER if none of the power contracts offer the minimum amount of power required by the board.
//  */
// int app_heating_turn_on(void);

// /**
//  * Requets to turn off heating.
//  * Heating is expected to stop immediately.
//  * Function call should typically originate from either the user interface of the communication link.
//  * @return 0 in case of success.
//  */
// int app_heating_turn_off(void);

/* Task */
int app_task(void);

#endif