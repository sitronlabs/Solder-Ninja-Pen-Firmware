/* Self header */
#include "app.h"

/* Project */
#include "element/element.h"
#include "log/log.h"

/* Config */
#include "../cfg/config.h"

/* Local variables */
static enum {
    STATE_0_SPLASH,
} m_sm;
static enum app_state m_state = APP_STATE_LOCKED;
static float m_target = 300;

/**
 *
 */
int app_setup(void) {
    int res;

    /* Setup heating element */
    res = element_setup();
    if (res < 0) {
        log_e("Failed to setup heating element!");
        return -1;
    }

    /* Return success */
    return 0;
}

/**
 *
 */
enum app_state app_state_get(void) {
    return m_state;
}

/**
 *
 */
int app_sleep() {
    return -1;
}

/**
 *
 */
int app_wake() {
    return -1;
}

/**
 *
 */
int app_lock(enum app_lock_source source) {

    /* Log */
    log_d("Request to lock with source = %d", source);

    /* Temp */
    m_state = APP_STATE_LOCKED;

    /* Return success */
    return 0;
}

/**
 *
 */
int app_unlock(enum app_lock_source source) {

    /* Log */
    log_d("Request to unlock with source = %d", source);

    /* Temp */
    m_state = APP_STATE_HEATING;

    /* Return success */
    return 0;
}

/**
 *
 */
float app_target_get(void) {
    return m_target;
}

/**
 * @brief
 * @param
 * @return
 */
int app_target_increase(void) {
    m_target += 10;
    if (m_target > CONFIG_APP_TARGET_MAX) {
        m_target = CONFIG_APP_TARGET_MAX;
    }
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int app_target_decrease(void) {
    m_target -= 10;
    if (m_target < CONFIG_APP_TARGET_MIN) {
        m_target = CONFIG_APP_TARGET_MIN;
    }
    return 0;
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

    /* Heating element task */
    element_task();

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
