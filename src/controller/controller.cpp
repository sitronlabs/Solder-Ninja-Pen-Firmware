/* Self header */
#include "controller.h"

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
static float m_target = CONFIG_CONTROLLER_TARGET_MAX_SAFE;
static bool m_boost_requested = false; /* Boost mode requested (target > safe limit) but timer not yet started */
static bool m_boost_activated = false; /* Boost mode active (timer running, will auto-disable after duration limit) */
static uint32_t m_boost_timestamp;     /* Timestamp when boost timer was started */

/**
 *
 */
int controller_setup(void) {
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
enum controller_state controller_state_get(void) {
    if (m_lock) {
        return CONTROLLER_STATE_LOCKED;
    } else if (m_sleep_inactivity || m_sleep_stand) {
        return CONTROLLER_STATE_ASLEEP;
    } else {
        return CONTROLLER_STATE_ACTIVE;
    }
}

/**
 *
 */
int controller_sleep(enum controller_sleep_reason reason) {

    /* Disable heating */
    element_heating_disable();

    /* Update sleep flags */
    if (reason == CONTROLLER_SLEEP_REASON_MOTION) {
        m_sleep_inactivity = true;
    } else if (reason == CONTROLLER_SLEEP_REASON_MAGNET) {
        m_sleep_stand = true;
    }

    /* Disable boost and reset target to safe limit if needed */
    m_boost_requested = false;
    m_boost_activated = false;
    m_target = (m_target > CONFIG_CONTROLLER_TARGET_MAX_SAFE) ? CONFIG_CONTROLLER_TARGET_MAX_SAFE : m_target;

    /* Return success */
    return 0;
}

/**
 *
 */
int controller_wake(enum controller_wake_reason reason) {

    /* Update sleep flags */
    if (reason == CONTROLLER_WAKE_REASON_MOTION ||
        reason == CONTROLLER_WAKE_REASON_BUTTONS) {
        m_sleep_inactivity = false;
    } else if (reason == CONTROLLER_WAKE_REASON_MAGNET) {
        m_sleep_stand = false;
    }

    /* Enable heating
     * @todo Prevent transitionning to heating if not enough power?
     * @todo Check power available by asking power_xxx()
     * @todo If not enough power, return specific error code */
    if (controller_state_get() == CONTROLLER_STATE_ACTIVE) {
        element_temperature_target_set(m_target);
        element_heating_enable();
    }

    /* Return success */
    return 0;
}

/**
 *
 */
int controller_lock(enum controller_lock_reason reason) {

    /* Disable heating */
    element_heating_disable();

    /* Update lock flags */
    m_lock = true;

    /* Disable boost and reset target to safe limit if needed */
    m_boost_requested = false;
    m_boost_activated = false;
    m_target = (m_target > CONFIG_CONTROLLER_TARGET_MAX_SAFE) ? CONFIG_CONTROLLER_TARGET_MAX_SAFE : m_target;

    /* Return success */
    return 0;
}

/**
 *
 */
int controller_unlock(enum controller_unlock_reason reason) {

    /* Update lock flags */
    m_lock = false;

    /* Enable heating
     * @todo Prevent transitionning to heating if not enough power?
     * @todo Check power available by asking power_xxx()
     * @todo If not enough power, return specific error code */
    if (controller_state_get() == CONTROLLER_STATE_ACTIVE) {
        element_temperature_target_set(m_target);
        element_heating_enable();
    }

    /* Return success */
    return 0;
}

/**
 *
 */
float controller_target_get(void) {
    return m_target;
}

/**
 * @brief
 * @param
 * @return
 */
int controller_target_increase(void) {

    /* Compute new target */
    m_target += 10;

    /* Ensure target is within bounds */
    if (m_target > CONFIG_CONTROLLER_TARGET_MAX_BOOST) {
        m_target = CONFIG_CONTROLLER_TARGET_MAX_BOOST;
    }

    /* If target exceeds safe limit, request boost mode (timer will start once measured temp exceeds safe limit) */
    if (m_target > CONFIG_CONTROLLER_TARGET_MAX_SAFE) {
        m_boost_requested = true;
    }

    /* Save new target to settings */
    settings_temperature_target_set(m_target > CONFIG_CONTROLLER_TARGET_MAX_SAFE ? CONFIG_CONTROLLER_TARGET_MAX_SAFE : m_target);

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
int controller_target_decrease(void) {

    /* Compute new target */
    m_target -= 10;

    /* Ensure target is within bounds */
    if (m_target < CONFIG_CONTROLLER_TARGET_MIN) {
        m_target = CONFIG_CONTROLLER_TARGET_MIN;
    }

    /* If target exceeds safe limit, request boost mode (timer will start once measured temp exceeds safe limit) */
    /* Otherwise, cancel any pending or active boost */
    if (m_target > CONFIG_CONTROLLER_TARGET_MAX_SAFE) {
        m_boost_requested = true;
    } else {
        m_boost_requested = false;
        m_boost_activated = false;
    }

    /* Save new target to settings */
    settings_temperature_target_set(m_target > CONFIG_CONTROLLER_TARGET_MAX_SAFE ? CONFIG_CONTROLLER_TARGET_MAX_SAFE : m_target);

    /* Pass along */
    element_temperature_target_set(m_target);

    /* Return success */
    return 0;
}

/**
 * @brief Check if boost mode is requested or currently active
 * @return true if boost is requested (waiting for temp to exceed safe limit) or active (timer running)
 */
bool controller_boost_activated_get(void) {
    return m_boost_requested || m_boost_activated;
}

/**
 * @brief
 * @param
 * @return
 */
int controller_task(void) {
    int res;

    /* Power negotiatior task */
    power_task();

    /* Heating element task */
    element_task();

    /* Lock if element has been disconnected */
    if (element_connected_get() == false) {
        element_heating_disable();
        m_lock = true;
    }

    /* Boost activation: Start timer only once measured temperature exceeds safe limit
     * This ensures boost duration is counted from when the element actually reaches boost temperature,
     * not from when the user requests it (which may take time to heat up) */
    if ((m_boost_requested == true) && (m_boost_activated == false)) {
        float measured_temp;
        res = element_temperature_measured_get(measured_temp);
        if (res == 0) {
            if (measured_temp > CONFIG_CONTROLLER_TARGET_MAX_SAFE) {
                m_boost_requested = false;
                m_boost_activated = true;
                m_boost_timestamp = millis();
            }
        }
    }

    /* Boost deactivation: After duration limit, automatically reduce target to safe limit and disable boost */
    if ((m_boost_activated == true) && (millis() - m_boost_timestamp >= CONFIG_CONTROLLER_BOOST_DURATION_LIMIT)) {
        if (m_target > CONFIG_CONTROLLER_TARGET_MAX_SAFE) {
            m_target = CONFIG_CONTROLLER_TARGET_MAX_SAFE;
            element_temperature_target_set(m_target);
        }
        m_boost_activated = false;
    }

    /* Return success */
    return 0;
}
