/* Self header */
#include "app.h"

/* Project */
#include "app/power/power.h"
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

    /* Setup power negotiator */
    res = power_setup();
    if (res < 0) {
        log_e("Failed to setup power negotiator!");
        return -1;
    }

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
int app_sleep(void) {

    /* Transition to sleep state
     * if we are heating */
    if (m_state == APP_STATE_HEATING) {
        element_heating_disable();
        m_state = APP_STATE_ASLEEP;
    }

    /* Return success */
    return 0;
}

/**
 *
 */
int app_wake(void) {

    /* Transition to heating state
     * if and only if we were asleep */
    if (m_state == APP_STATE_ASLEEP) {
        element_temperature_target_set(m_target);
        element_heating_enable();
        m_state = APP_STATE_HEATING;
    }

    /* Return success */
    return 0;
}

/**
 *
 */
int app_lock(void) {

    /* Transition to locked state */
    element_heating_disable();
    m_state = APP_STATE_LOCKED;

    /* Return success */
    return 0;
}

/**
 *
 */
int app_unlock(void) {

    /* Transition to heating state */
    element_temperature_target_set(m_target);
    element_heating_enable();
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

    /* */
    m_target += 10;
    if (m_target > CONFIG_APP_TARGET_MAX) {
        m_target = CONFIG_APP_TARGET_MAX;
    }

    /* Pass along */
    element_temperature_target_set(m_target);

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int app_target_decrease(void) {

    /* */
    m_target -= 10;
    if (m_target < CONFIG_APP_TARGET_MIN) {
        m_target = CONFIG_APP_TARGET_MIN;
    }

    /* Pass along */
    element_temperature_target_set(m_target);

    /* Return success */
    return 0;
}

// int app_heating_turn_on(void) {
//     /* Pass along */
//     element_temperature_target_set(m_target);
//     element_heating_enable();
//     /* Return success */
//     return 0;
// }

// int app_heating_turn_off(void) {
//     /* Pass along */
//     element_heating_disable();
//     /* Return success */
//     return 0;
// }

// int app_heating_enabled_get(void) {
//     /* Return success */
//     return 0;
// }

int app_task(void) {

    // App has no splash, but rather starts in the locked state

    /* Power negotiatior task */
    power_task();

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
