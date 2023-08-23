/* Self header */
#include "app.h"

/* State machine */
static enum {
    STATE_0_SPLASH,
} m_sm;
static bool m_locked = true;

/**
 *
 */
int app_setup(void) {

    /* Return success */
    return 0;
}

/**
 *
 */
enum app_state app_state_get(void) {
    return APP_STATE_LOCKED;  // TODO
}

int app_sleep() {
    return -1;
}

int app_wake() {
    return -1;
}

int app_lock(/* reason */) {
    return -1;
}

int app_unlock(/* reason */) {
    return -1;
}

int app_heating_turn_on(void) {

    /* Return success */
    return 0;
}

int app_heating_turn_off(void) {

    /* Return success */
    return 0;
}

int app_heating_status_get(void) {

    /* Return success */
    return 0;
}

int app_task(void) {

    // App has no splash, but rather starts in the locked state

    /* */
    switch (m_sm) {
            // Load settings
            // interface_page_show(PAGE_SPLASH); Show splash screen
            // wait
            // interface_page_show(PAGE_MONITOR);
        case STATE_0_SPLASH: {
            break;
        }
    }

    /* Return success */
    return 0;
}
