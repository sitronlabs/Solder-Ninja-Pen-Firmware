/* Self header */
#include "app.h"

/* Project */
#include "element/element.h"
#include "log/log.h"
#include "power/power.h"
#include "settings/settings.h"

/* Config */
#include "../cfg/config.h"

/* Local variables */
static bool m_sleep_inactivity = false;
static bool m_sleep_stand = false;
static bool m_lock = false;
static float m_target = 350;
static bool m_target_changed = false;
static uint32_t m_target_changed_timestamp;
static bool m_boost_activated = false;
static uint32_t m_boost_timestamp;

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
    res = settings_temperature_target_get(temperature);
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
    if (m_lock) {
        return APP_STATE_LOCKED;
    } else if (m_sleep_inactivity || m_sleep_stand) {
        return APP_STATE_ASLEEP;
    } else {
        return APP_STATE_ACTIVE;
    }
}

/**
 *
 */
int app_sleep(enum app_sleep_reason reason) {

    /* Disable heating */
    element_heating_disable();

    /* Update sleep flags */
    if (reason == APP_SLEEP_REASON_MOTION) {
        m_sleep_inactivity = true;
    } else if (reason == APP_SLEEP_REASON_MAGNET) {
        m_sleep_stand = true;
    }

    /* Return success */
    return 0;
}

/**
 *
 */
int app_wake(enum app_wake_reason reason) {

    /* Update sleep flags */
    if (reason == APP_WAKE_REASON_MOTION ||
        reason == APP_WAKE_REASON_BUTTONS) {
        m_sleep_inactivity = false;
    } else if (reason == APP_WAKE_REASON_MAGNET) {
        m_sleep_stand = false;
    }

    /* Enable heating
     * @todo Prevent transitionning to heating if not enough power?
     * @todo Check power available by asking power_xxx()
     * @todo If not enough power, return specific error code */
    if (app_state_get() == APP_STATE_ACTIVE) {
        element_temperature_target_set(m_target);
        element_heating_enable();
    }

    /* Return success */
    return 0;
}

/**
 *
 */
int app_lock(enum app_lock_reason reason) {

    /* Disable heating */
    element_heating_disable();

    /* Update lock flags */
    m_lock = true;

    /* Return success */
    return 0;
}

/**
 *
 */
int app_unlock(enum app_unlock_reason reason) {

    /* Update lock flags */
    m_lock = false;

    /* Enable heating
     * @todo Prevent transitionning to heating if not enough power?
     * @todo Check power available by asking power_xxx()
     * @todo If not enough power, return specific error code */
    if (app_state_get() == APP_STATE_ACTIVE) {
        element_temperature_target_set(m_target);
        element_heating_enable();
    }

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
    if (m_target > CONFIG_APP_TARGET_MAX_BOOST) {
        m_target = CONFIG_APP_TARGET_MAX_BOOST;
    }

    /* If over the safe limit, save the timestamp to revert to the safe limit after a while */
    if (m_target > CONFIG_APP_TARGET_MAX_SAFE) {
        m_boost_activated = true;
        m_boost_timestamp = millis();
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

    /* If over the safe limit, save the timestamp to revert to the safe limit after a while */
    if (m_target > CONFIG_APP_TARGET_MAX_SAFE) {
        m_boost_activated = true;
        m_boost_timestamp = millis();
    } else {
        m_boost_activated = false;
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
bool app_boost_activated_get(void) {
    return m_boost_activated;
}

/**
 * @brief
 * @param
 * @return
 */
int app_task(void) {

    /* Power negotiatior task */
    power_task();

    /* Heating element task */
    element_task();

    /* Lock if element has been disconnected */
    if (element_connected_get() == false) {
        element_heating_disable();
        m_lock = true;
    }

    /* Disable boost after a while */
    if ((m_boost_activated == true) && (millis() - m_boost_timestamp >= CONFIG_APP_BOOST_DURATION_LIMIT)) {
        if (m_target > CONFIG_APP_TARGET_MAX_SAFE) {
            m_target = CONFIG_APP_TARGET_MAX_SAFE;
            element_temperature_target_set(m_target);
        }
        m_boost_activated = false;
    }

    /* Save tartget temperature when stable */
    if ((m_target_changed == true) && (millis() - m_target_changed_timestamp >= 500)) {
        if (m_target > CONFIG_APP_TARGET_MAX_SAFE) {
            settings_temperature_target_set(CONFIG_APP_TARGET_MAX_SAFE);
        } else {
            settings_temperature_target_set(m_target);
        }
        m_target_changed = false;
    }

    /* Return success */
    return 0;
}
