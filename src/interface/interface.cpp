/* Self header */
#include "interface.h"

/* Project headers */
#include "../gen/icons.h"
#include "../gen/version.h"
#include "controller/controller.h"
#include "element/element.h"
#include "interface/accelerometer.h"
#include "interface/buttons.h"
#include "interface/magnet.h"
#include "log/log.h"
#include "power/power.h"
#include "settings/settings.h"

/* Arduino headers */
#include <Arduino.h>
#include <Wire.h>
#include <ssd1306.h>

#if R8A
/* Pico headers */
#include <pico/bootrom.h>
#endif

/* Local variables */
static ssd1306 m_library(CONFIG_UI_DISPLAY_WIDTH, CONFIG_UI_DISPLAY_HEIGHT);
static uint8_t m_buffer[CONFIG_UI_DISPLAY_WIDTH * CONFIG_UI_DISPLAY_HEIGHT / 8];
static uint32_t m_timestamp;
static enum {
    STATE_SPLASH_0,
    STATE_SPLASH_1,
    STATE_INFO_0,
    STATE_INFO_1,
    STATE_INFO_2,
    STATE_INFO_3,
    STATE_USER_0,
    STATE_USER_1,
    STATE_MONITOR_REDIRECT,
    STATE_MONITOR_LOCKED,
    STATE_MONITOR_HEATING,
    STATE_MONITOR_ASLEEP,
    STATE_MONITOR_ADJUST,
    STATE_MENU_HOME,
    STATE_MENU_INTERFACE_UNITS_0,
    STATE_MENU_INTERFACE_UNITS_1,
    STATE_MENU_INTERFACE_ROTATION_0,
    STATE_MENU_INTERFACE_ROTATION_1,
    STATE_MENU_DISPLAY_BRIGHTNESS_0,
    STATE_MENU_DISPLAY_BRIGHTNESS_1,
    STATE_MENU_ACCELEROMETER_IDLE_TIME_0,
    STATE_MENU_ACCELEROMETER_IDLE_TIME_1,
    STATE_MENU_UPDATE_0,
} m_sm;

/**
 * @brief Initialize the user interface system
 *
 * This function sets up all interface components including buttons, magnet sensor,
 * accelerometer, and the OLED display. It configures display settings such as
 * rotation and brightness based on stored preferences.
 *
 * @return 0 on success, negative error code on failure
 */
int interface_setup(void) {
    int res;

    /* Setup buttons */
    res = buttons_setup();
    if (res < 0) {
        log_e("Failed to setup buttons!");
        return -ERROR_GENERIC;
    }

    /* Setup magnet sensor */
    res = magnet_setup();
    if (res < 0) {
        log_e("Failed to setup magnet sensor!");
        return -ERROR_GENERIC;
    }

    /* Setup accelerometer */
    res = accelerometer_setup();
    if (res < 0) {
        log_e("Failed to setup accelerometer!");
        return -ERROR_GENERIC;
    }

    /* Setup display library */
#if R4J
    res = m_library.setup(Wire, 0x3C, PF1, m_buffer);
#elif R8A
    res = m_library.setup(Wire, 0x3C, 6, m_buffer);
    if (res < 0) {
        log_e("Failed to setup display!");
        return -ERROR_GENERIC;
    }
    bool left_handed = false;
    settings_interface_rotation_get(left_handed);
    m_library.setRotation(left_handed ? 2 : 0);
    int brightness = 100;
    settings_display_brightness_get(brightness);
    m_library.brightness_set(brightness / 100.0);

#else
#error Invalid hardware version
#endif
    if (res < 0) {
        log_e("Failed to setup display library!");
        return -ERROR_GENERIC;
    }

    /* Detect display panel */
    if (m_library.detect() != true) {
        log_e("Failed to detect display panel!");
        return -ERROR_PERIPHERAL_NOT_DETECTED;
    }

    /* Return success */
    return 0;
}

/**
 * @brief Main interface task handler
 *
 * This function implements the main user interface state machine that handles
 * all display updates, user input processing, and state transitions. It should
 * be called regularly to poll for user interactions and update the display
 * accordingly.
 *
 * The state machine manages various UI states including:
 * - Splash screen display
 * - Information screens (version, product info, user info)
 * - Monitor states (locked, heating, asleep, adjust)
 * - Settings menu navigation
 *
 * @return 0 on success, negative error code on failure
 */
int interface_task(void) {
    int res;

    /* Handle buttons processing */
    buttons_task();

    /* Handle accelerometer */
    accelerometer_task();

    /* Handle magnet sensor */
    static enum {
        STATE_MAGNET_0_NOT_DETECTED,
        STATE_MAGNET_1_DETECTED,
    } m_magnet_sm;
    switch (m_magnet_sm) {

        case STATE_MAGNET_0_NOT_DETECTED: {

            /* Wait for magnet to be detected */
            if (magnet_detected_get() == true) {
                controller_sleep(CONTROLLER_SLEEP_REASON_MAGNET);
                m_magnet_sm = STATE_MAGNET_1_DETECTED;
            }
            break;
        }

        case STATE_MAGNET_1_DETECTED: {

            /* Wait for magnet to not be detected */
            if (magnet_detected_get() == false) {
                controller_wake(CONTROLLER_WAKE_REASON_MAGNET);
                m_magnet_sm = STATE_MAGNET_0_NOT_DETECTED;
            }
            break;
        }
    }

    /* Handle accelerometer */
    if (accelerometer_wake_detected_get() > 0) {
        accelerometer_wake_clear();
        controller_wake(CONTROLLER_WAKE_REASON_MOTION);
    }
    if (accelerometer_idle_detected_get() > 0) {
        accelerometer_idle_clear();
        controller_sleep(CONTROLLER_SLEEP_REASON_MOTION);
    }
    if (accelerometer_fall_detected_get() > 0) {
        accelerometer_fall_clear();
        controller_lock(CONTROLLER_LOCK_REASON_FREFALL);
    }

    /* Handle user interface */
    switch (m_sm) {

        case STATE_SPLASH_0: {

            /* Display splash page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_ninja.data, k_icon_ninja.width, k_icon_ninja.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Solder");
            m_library.setCursor(20, 9);
            m_library.print("Ninja");
            m_library.display();

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_SPLASH_1;
            break;
        }

        case STATE_SPLASH_1: {

            /* Wait for timeout */
            if ((millis() - m_timestamp) < CONFIG_UI_SPLASH_DURATION) {
                break;
            }

            /* Move on */
            m_sm = STATE_INFO_0;
            break;
        }

        case STATE_INFO_0: {

            /* Only show version and product information if user pressed a button */
            if (buttons_event_get() == BUTTONS_EVENT_NONE) {
                m_sm = STATE_USER_0;
                break;
            }

            /* Display version info */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_firmware_version.data, k_icon_firmware_version.width, k_icon_firmware_version.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.printf("v%u.%u.%u", k_version_major, k_version_minor, k_version_patch);
            m_library.setCursor(20, 9);
            m_library.print(k_version_commit);
            m_library.display();

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_INFO_1;
            break;
        }

        case STATE_INFO_1: {

            /* Wait for timeout */
            if ((millis() - m_timestamp) < CONFIG_UI_INFO_DURATION) {
                break;
            }

            /* Move on */
            m_sm = STATE_INFO_2;
            break;
        }

        case STATE_INFO_2: {

            /* Retrieve product information,
             * Or skip if not avaiable */
            char number[CONFIG_SETTINGS_PRODUCT_NUMBER_MAX_LENGTH + 1] = {0};
            char revision[CONFIG_SETTINGS_PRODUCT_REVISION_MAX_LENGTH + 1] = {0};
            char serial[CONFIG_SETTINGS_SERIAL_NUMBER_MAX_LENGTH + 1] = {0};
            res = settings_product_get(number, revision, serial);
            if (res != 1) {
                m_sm = STATE_USER_0;
                break;
            }

            /* Display product info */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_serialnumber.data, k_icon_serialnumber.width, k_icon_serialnumber.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.printf(number);
            m_library.setCursor(20, 9);
            m_library.print(serial);
            m_library.display();

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_INFO_3;
            break;
        }

        case STATE_INFO_3: {

            /* Wait for timeout */
            if ((millis() - m_timestamp) < CONFIG_UI_INFO_DURATION) {
                break;
            }

            /* Move on */
            m_sm = STATE_USER_0;
            break;
        }

        case STATE_USER_0: {

            /* Retrieve user information,
             * Or skip if not avaiable */
            uint8_t icon[32] = {0};
            char text_line1[CONFIG_SETTINGS_USERNAME_LINE1_MAX_LENGTH + 1] = {0};
            char text_line2[CONFIG_SETTINGS_USERNAME_LINE2_MAX_LENGTH + 1] = {0};
            res = settings_user_get(icon, text_line1, text_line2);
            if (res != 1) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Display user info page */
            m_library.clear();
            m_library.drawBitmap(0, 0, icon, 16, 16, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print(text_line1);
            m_library.setCursor(20, 9);
            m_library.print(text_line2);
            m_library.display();

            /* Move on */
            m_timestamp = millis();
            m_sm = STATE_USER_1;
            break;
        }

        case STATE_USER_1: {

            /* Wait for timeout */
            if ((millis() - m_timestamp) < CONFIG_UI_SPLASH_DURATION) {
                break;
            }

            /* Move on */
            m_sm = STATE_MONITOR_REDIRECT;
            break;
        }

        case STATE_MONITOR_REDIRECT: {

            /* Move on according to controller state */
            switch (controller_state_get()) {
                case CONTROLLER_STATE_LOCKED: {
                    m_sm = STATE_MONITOR_LOCKED;
                    break;
                }
                case CONTROLLER_STATE_ACTIVE: {
                    m_sm = STATE_MONITOR_HEATING;
                    break;
                }
                case CONTROLLER_STATE_ASLEEP: {
                    m_sm = STATE_MONITOR_ASLEEP;
                    break;
                }
                default: {
                    m_sm = STATE_SPLASH_0;
                    return -ERROR_GENERIC_STATE_UNEXPECTED;
                }
            }
            break;
        }

        case STATE_MONITOR_LOCKED: {

            /* Ensure controller state is still coherent */
            if (controller_state_get() != CONTROLLER_STATE_LOCKED) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Retrieve relevant settings */
            bool fahrenheit = false;
            settings_interface_units_get(fahrenheit);

            /* Display monitor page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_lock.data, k_icon_lock.width, k_icon_lock.height, 1);
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            if (element_connected_get()) {
                float temperature_c = 0;
                res = element_temperature_measured_get(temperature_c);
                if (res < 0) {
                    m_library.print("err");
                } else {
                    if (fahrenheit) {
                        m_library.printf("%03.0f", temperature_c * 1.8 + 32);
                        m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_f.data, k_icon_degrees_f.width, k_icon_degrees_f.height, 1);
                    } else {
                        m_library.printf("%03.0f", temperature_c);
                        m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_c.data, k_icon_degrees_c.width, k_icon_degrees_c.height, 1);
                    }
                }
            } else {
                m_library.print("tip");
            }
            m_library.setTextSize(1);
            struct power_option contract;
            res = power_contract_get(contract);
            if (res < 0) {
                m_library.setCursor(11 * 6, 0);
                m_library.print("--.-V");
                m_library.setCursor(11 * 6, 9);
                m_library.print("-.-A");
            } else {
                m_library.setCursor(11 * 6, 0);
                m_library.printf("%4.1fV", contract.voltage_max);
                m_library.setCursor(12 * 6, 9);
                m_library.printf("%3.1fA", contract.current_max);
            }
            m_library.display();

            /* Handle buttons
             * A short press on both buttons will trigger an unlock
             * A long press on both buttons will trigger a lock and open the menu */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_BOTH_SHORT: {
                    controller_unlock(CONTROLLER_UNLOCK_REASON_BUTTONS);
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    controller_lock(CONTROLLER_LOCK_REASON_BUTTONS);
                    m_sm = STATE_MENU_HOME;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MONITOR_HEATING: {

            /* Ensure controller state is still coherent */
            if (controller_state_get() != CONTROLLER_STATE_ACTIVE) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Retrieve relevant settings */
            bool fahrenheit = false;
            settings_interface_units_get(fahrenheit);

            /* Display monitor page */
            m_library.clear();
            if (controller_boost_activated_get()) {
                static uint32_t m_boost_warning_timestamp;
                if ((millis() - m_boost_warning_timestamp) >= 1000) {
                    m_boost_warning_timestamp = millis();
                    m_library.drawBitmap(0, 0, k_icon_bolt.data, k_icon_bolt.width, k_icon_bolt.height, 1);
                } else if ((millis() - m_boost_warning_timestamp) >= 500) {
                    m_library.drawBitmap(0, 0, k_icon_bolt.data, k_icon_bolt.width, k_icon_bolt.height, 1);
                } else {
                    m_library.drawBitmap(0, 0, k_icon_bolt_plus.data, k_icon_bolt_plus.width, k_icon_bolt_plus.height, 1);
                }
            } else {
                m_library.drawBitmap(0, 0, k_icon_bolt.data, k_icon_bolt.width, k_icon_bolt.height, 1);
            }
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            if (element_connected_get()) {
                float temperature_c = 0;
                res = element_temperature_measured_get(temperature_c);
                if (res < 0) {
                    m_library.print("err");
                } else {
                    if (fahrenheit) {
                        m_library.printf("%03.0f", temperature_c * 1.8 + 32);
                        m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_f.data, k_icon_degrees_f.width, k_icon_degrees_f.height, 1);
                    } else {
                        m_library.printf("%03.0f", temperature_c);
                        m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_c.data, k_icon_degrees_c.width, k_icon_degrees_c.height, 1);
                    }
                }
            } else {
                m_library.print("tip");
            }
            m_library.setTextSize(1);
            struct power_option contract;
            res = power_contract_get(contract);
            if (res < 0) {
                m_library.setCursor(11 * 6, 0);
                m_library.print("--.-V");
                m_library.setCursor(11 * 6, 9);
                m_library.print("-.-A");
            } else {
                m_library.setCursor(11 * 6, 0);
                m_library.printf("%4.1fV", contract.voltage_max);
                m_library.setCursor(12 * 6, 9);
                m_library.printf("%3.1fA", contract.current_max);
            }
            m_library.display();

            /* Handle buttons
             * Short and long presses on either button will adjust target temperature
             * A short press on both buttons will trigger a lock
             * A long press on both buttons will trigger a lock and open the menu */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG: {
                    controller_temperature_target_decrease();
                    m_timestamp = millis();
                    m_sm = STATE_MONITOR_ADJUST;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    controller_temperature_target_increase();
                    m_timestamp = millis();
                    m_sm = STATE_MONITOR_ADJUST;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    controller_lock(CONTROLLER_LOCK_REASON_BUTTONS);
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    controller_lock(CONTROLLER_LOCK_REASON_BUTTONS);
                    m_sm = STATE_MENU_HOME;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MONITOR_ASLEEP: {

            /* Ensure controller state is still coherent */
            if (controller_state_get() != CONTROLLER_STATE_ASLEEP) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Retrieve relevant settings */
            bool fahrenheit = false;
            settings_interface_units_get(fahrenheit);

            /* Display monitor page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_sleep.data, k_icon_sleep.width, k_icon_sleep.height, 1);
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            if (element_connected_get()) {
                float temperature_c = 0;
                res = element_temperature_measured_get(temperature_c);
                if (res < 0) {
                    m_library.print("err");
                } else {
                    if (fahrenheit) {
                        m_library.printf("%03.0f", temperature_c * 1.8 + 32);
                        m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_f.data, k_icon_degrees_f.width, k_icon_degrees_f.height, 1);
                    } else {
                        m_library.printf("%03.0f", temperature_c);
                        m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_c.data, k_icon_degrees_c.width, k_icon_degrees_c.height, 1);
                    }
                }
            } else {
                m_library.print("tip");
            }
            m_library.setTextSize(1);
            struct power_option contract;
            res = power_contract_get(contract);
            if (res < 0) {
                m_library.setCursor(11 * 6, 0);
                m_library.print("--.-V");
                m_library.setCursor(11 * 6, 9);
                m_library.print("-.-A");
            } else {
                m_library.setCursor(11 * 6, 0);
                m_library.printf("%4.1fV", contract.voltage_max);
                m_library.setCursor(12 * 6, 9);
                m_library.printf("%3.1fA", contract.current_max);
            }
            m_library.display();

            /* Handle buttons
             * Short and long presses on either button will trigger a wake
             * A short press on both buttons will trigger a lock
             * A long press on both buttons will trigger a lock and open the menu */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG:
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    controller_wake(CONTROLLER_WAKE_REASON_BUTTONS);
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    controller_lock(CONTROLLER_LOCK_REASON_BUTTONS);
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    controller_lock(CONTROLLER_LOCK_REASON_BUTTONS);
                    m_sm = STATE_MENU_HOME;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MONITOR_ADJUST: {

            /* Revert after timeout */
            if ((millis() - m_timestamp) >= CONFIG_UI_ADJUST_DURATION) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Retrieve relevant settings */
            bool fahrenheit = false;
            settings_interface_units_get(fahrenheit);

            /* Display monitor page */
            float temperature_c = 0;
            controller_temperature_target_get(temperature_c);
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_thermometer.data, k_icon_thermometer.width, k_icon_thermometer.height, 1);
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            if (fahrenheit) {
                m_library.printf("%03.0f", temperature_c * 1.8 + 32);
                m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_f.data, k_icon_degrees_f.width, k_icon_degrees_f.height, 1);
            } else {
                m_library.printf("%03.0f", temperature_c);
                m_library.drawBitmap(17 + 12 + 12 + 12, 0, k_icon_degrees_c.data, k_icon_degrees_c.width, k_icon_degrees_c.height, 1);
            }
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG: {
                    controller_temperature_target_decrease();
                    m_timestamp = millis();
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    controller_temperature_target_increase();
                    m_timestamp = millis();
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_HOME: {
            m_sm = STATE_MENU_INTERFACE_UNITS_0;
            break;
        }

        case STATE_MENU_INTERFACE_UNITS_0: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Settings");
            m_library.setCursor(20, 9);
            m_library.print("Units");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    m_sm = STATE_MENU_INTERFACE_ROTATION_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_INTERFACE_UNITS_1;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_INTERFACE_UNITS_1: {

            /* Retrieve relevant settings */
            bool fahrenheit = false;
            settings_interface_units_get(fahrenheit);

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Units");
            m_library.setCursor(20, 9);
            m_library.printf("%s", fahrenheit ? "Fahrenheit" : "Celsius");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    fahrenheit = !fahrenheit;
                    settings_interface_units_set(fahrenheit);
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_INTERFACE_UNITS_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_INTERFACE_ROTATION_0: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Settings");
            m_library.setCursor(20, 9);
            m_library.print("Disp rot.");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    m_sm = STATE_MENU_INTERFACE_UNITS_0;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    m_sm = STATE_MENU_DISPLAY_BRIGHTNESS_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_INTERFACE_ROTATION_1;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_INTERFACE_ROTATION_1: {

            /* Retrieve relevant settings */
            bool left_handed = false;
            res = settings_interface_rotation_get(left_handed);

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Disp rot.");
            m_library.setCursor(20, 9);
            m_library.printf("%s handed", left_handed ? "Left" : "Right");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    left_handed = !left_handed;
                    settings_interface_rotation_set(left_handed);
                    m_library.setRotation(left_handed ? 2 : 0);
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_INTERFACE_ROTATION_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_DISPLAY_BRIGHTNESS_0: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Settings");
            m_library.setCursor(20, 9);
            m_library.print("Disp bright.");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    m_sm = STATE_MENU_INTERFACE_ROTATION_0;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    m_sm = STATE_MENU_ACCELEROMETER_IDLE_TIME_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_DISPLAY_BRIGHTNESS_1;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_DISPLAY_BRIGHTNESS_1: {

            /* Retrieve relevant settings */
            int brightness = 100;
            res = settings_display_brightness_get(brightness);

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Disp bright.");
            m_library.setCursor(20, 9);
            m_library.printf("%d%%", brightness);
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    if (brightness >= 20) {
                        brightness -= 10;
                    }
                    brightness = 10 * (brightness / 10);
                    m_library.brightness_set(brightness / 100.0);
                    settings_display_brightness_set(brightness);
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    if (brightness <= 90) {
                        brightness += 10;
                    }
                    brightness = 10 * (brightness / 10);
                    m_library.brightness_set(brightness / 100.0);
                    settings_display_brightness_set(brightness);
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_DISPLAY_BRIGHTNESS_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_ACCELEROMETER_IDLE_TIME_0: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Settings");
            m_library.setCursor(20, 9);
            m_library.print("Idle time");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    m_sm = STATE_MENU_DISPLAY_BRIGHTNESS_0;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    m_sm = STATE_MENU_UPDATE_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_ACCELEROMETER_IDLE_TIME_1;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_ACCELEROMETER_IDLE_TIME_1: {

            /* Retrieve relevant settings */
            uint32_t idle_time_ms = accelerometer_idle_time_get();

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_settings.data, k_icon_settings.width, k_icon_settings.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Idle time");
            m_library.setCursor(20, 9);
            m_library.printf("%lus", idle_time_ms / 1000);
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {

                    /* Decrease idle time */
                    if (idle_time_ms > CONFIG_ACCEL_IDLE_TIME_MIN) {
                        idle_time_ms -= 5000;
                    }

                    /* Round to nearest 5 seconds */
                    idle_time_ms = 5000 * ((idle_time_ms + 2500) / 5000);
                    if (idle_time_ms < CONFIG_ACCEL_IDLE_TIME_MIN) {
                        idle_time_ms = CONFIG_ACCEL_IDLE_TIME_MIN;
                    }

                    /* Set new idle time */
                    accelerometer_idle_time_set(idle_time_ms);
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {

                    /* Increment idle time */
                    if (idle_time_ms < CONFIG_ACCEL_IDLE_TIME_MAX) {
                        idle_time_ms += 5000;
                    }

                    /* Round to nearest 5 seconds */
                    idle_time_ms = 5000 * ((idle_time_ms + 2500) / 5000);
                    if (idle_time_ms > CONFIG_ACCEL_IDLE_TIME_MAX) {
                        idle_time_ms = CONFIG_ACCEL_IDLE_TIME_MAX;
                    }

                    /* Set new idle time */
                    accelerometer_idle_time_set(idle_time_ms);
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_ACCELEROMETER_IDLE_TIME_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_UPDATE_0: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, k_icon_firmware_update.data, k_icon_firmware_update.width, k_icon_firmware_update.height, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Settings");
            m_library.setCursor(20, 9);
            m_library.print("Firm. update");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    m_sm = STATE_MENU_ACCELEROMETER_IDLE_TIME_0;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
#if R8A
                    reset_usb_boot(0, 0);
#endif
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                default: {
                    break;
                }
            }

            /* That's it */
            break;
        }

        default: {
            log_e("Unexpected state!");
            m_sm = STATE_SPLASH_0;
            return -ERROR_GENERIC_STATE_UNEXPECTED;
        }
    }

    /* Return success */
    return 0;
}
