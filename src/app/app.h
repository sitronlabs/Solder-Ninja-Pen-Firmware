#ifndef APP_H
#define APP_H
/**
 *
 */
int app_setup(void);

/**
 *
 */
enum app_state {
    APP_STATE_LOCKED,
    APP_STATE_ASLEEP,
    APP_STATE_HEATING,  // Conditions for going to heating : have a connected element, no locks
};

enum app_state app_state_get(void);

int app_sleep();
int app_wake();

/**
 * TODO Make sure that local menu lock can only be released by local menu
 */
int app_lock(/* reason */);

/**
 *
 */
int app_unlock(/* reason */);

/**
 * Requets to turn on heating.
 * Heating might start with some delay if the available contracts need additional negotiation.
 * Function call should typically originate from either the user interface of the communication link.
 * @return 0 in case of success, or a negative error code otherwise, in particular:
 * -ERROR_NOT_ENOUGH_POWER if none of the power contracts offer the minimum amount of power required by the board.
 */
int app_heating_turn_on(void);

/**
 * Requets to turn off heating.
 * Heating is expected to stop immediately.
 * Function call should typically originate from either the user interface of the communication link.
 * @return 0 in case of success.
 */
int app_heating_turn_off(void);

/**
 *
 */
int app_heating_status_get(void);

/**
 *
 */
int app_task(void);

#endif