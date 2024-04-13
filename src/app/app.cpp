/* Self header */
#include "app.h"

/* Project */
#include "power/power.h"
#include "element/element.h"
#include "log/log.h"
#include "settings/settings.h"

/* Config */
#include "../cfg/config.h"

/* Local variables */
static enum {
    STATE_0_SPLASH,
} m_sm;
static enum app_state m_state = APP_STATE_LOCKED;
static float m_target = 300;
static bool m_target_changed = false;
static uint32_t m_target_changed_timestamp;

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

    /* If available use target temperature from settings */
    float temperature;
    res = settings_temperature_get(temperature);
    if (res == 1) {
        m_target = temperature;
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

    /* Compute new target */
    m_target += 10;
    if (m_target > CONFIG_APP_TARGET_MAX) {
        m_target = CONFIG_APP_TARGET_MAX;
    }

    /* Save new target later on when the value has been stable for long enough
     * in order to avoid too frequent eeprom writes */
    m_target_changed = true;
    m_target_changed_timestamp = millis();

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

    /* Compute new target */
    m_target -= 10;
    if (m_target < CONFIG_APP_TARGET_MIN) {
        m_target = CONFIG_APP_TARGET_MIN;
    }

    /* Save new target later on when the value has been stable for long enough
     * in order to avoid too frequent eeprom writes */
    m_target_changed = true;
    m_target_changed_timestamp = millis();

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

    /* Lock if element has been disconnected */
    if (element_connected_get() == false) {
        app_lock();
    }

    /* Save tartget temperature when stable */
    if ((m_target_changed == true) && (millis() - m_target_changed_timestamp >= 500)) {
        settings_temperature_set(m_target);
        m_target_changed = false;
    }

    /* Return success */
    return 0;
}
