/* Self header */
#include "interface.h"

/* Project */
#include "../gen/version.h"
#include "app/app.h"
#include "element/element.h"
#include "interface/accelerometer.h"
#include "interface/buttons.h"
#include "interface/magnet.h"
#include "log/log.h"
#include "power/power.h"
#include "settings/settings.h"

/* Arduino libraries */
#include <Arduino.h>
#include <Wire.h>
#include <ssd1306.h>

/* C/C++ libraries */
#include <stdint.h>

/* Local variables */
static ssd1306 m_library(96, 16);
static uint8_t m_buffer[96 * 16 / 8];
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
    STATE_MENU_DISPLAY_ROTATION_0,
    STATE_MENU_DISPLAY_ROTATION_1,
    STATE_MENU_DISPLAY_BRIGHTNESS_0,
    STATE_MENU_DISPLAY_BRIGHTNESS_1,
} m_sm;

/* Icon bitmaps */
const uint8_t m_icon_ninja[32] = {0x03, 0xF8, 0x07, 0xFC, 0x0F, 0xFE, 0xDF, 0xFF, 0x7F, 0xFF, 0x78, 0x03, 0x73, 0x33, 0x73, 0x31, 0xF0, 0x03, 0x1F, 0xFF, 0x1F, 0xFF, 0x1F, 0xFE, 0x0F, 0xFE, 0x0F, 0xFC, 0x07, 0xF8, 0x01, 0xF0};
const uint8_t m_icon_bolt[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x60, 0x00, 0xE0, 0x01, 0xC0, 0x03, 0xC0, 0x07, 0xF0, 0x0F, 0xE0, 0x03, 0xC0, 0x03, 0x80, 0x07, 0x00, 0x06, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t m_icon_boost[32] = {0x00, 0x00, 0x08, 0x00, 0x1c, 0x30, 0x08, 0x60, 0x00, 0xe0, 0x01, 0xc0, 0x03, 0xc0, 0x07, 0xf0, 0x0f, 0xe0, 0x03, 0xc0, 0x03, 0x80, 0x07, 0x00, 0x06, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t m_icon_lock[32] = {0x00, 0x00, 0x03, 0xC0, 0x07, 0xE0, 0x0C, 0x30, 0x0C, 0x30, 0x0C, 0x30, 0x1F, 0xF8, 0x1F, 0xF8, 0x1E, 0x78, 0x1E, 0x78, 0x1E, 0x78, 0x1F, 0xF8, 0x1F, 0xF8, 0x0F, 0xF0, 0x00, 0x00, 0x00, 0x00};
const uint8_t m_icon_sleep[32] = {0x00, 0x00, 0x7F, 0x00, 0x02, 0x00, 0x04, 0x00, 0x08, 0x00, 0x10, 0x00, 0x20, 0x3E, 0x7F, 0x04, 0x00, 0x08, 0x00, 0x10, 0x3E, 0x3E, 0x04, 0x00, 0x08, 0x00, 0x10, 0x00, 0x3E, 0x00, 0x00, 0x00};
const uint8_t m_icon_thermometer[32] = {0x01, 0x00, 0x02, 0x80, 0x04, 0x58, 0x05, 0x40, 0x05, 0x58, 0x05, 0x40, 0x05, 0x58, 0x05, 0x40, 0x05, 0x40, 0x09, 0x20, 0x13, 0x90, 0x17, 0xD0, 0x13, 0x90, 0x09, 0x20, 0x04, 0x40, 0x03, 0x80};
const uint8_t m_icon_degrees_c[32] = {0x00, 0x00, 0xE0, 0x00, 0xA0, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x30, 0xC0, 0x30, 0xC0, 0x30, 0x00, 0x30, 0x00, 0x30, 0xC0, 0x30, 0xC0, 0x0F, 0x00, 0x0F, 0x00, 0x00, 0x00};
const uint8_t m_icon_degrees_f[32] = {0x00, 0x00, 0xE0, 0x00, 0xA0, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x3F, 0xC0, 0x3F, 0xC0, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x30, 0x00, 0x30, 0x00, 0x30, 0x00, 0x00, 0x00};
const uint8_t m_icon_settings[32] = {0x00, 0x00, 0x00, 0x00, 0x3f, 0xf8, 0x40, 0x04, 0x49, 0x24, 0x4b, 0xa4, 0x49, 0x24, 0x49, 0x24, 0x49, 0x24, 0x5d, 0x24, 0x49, 0x74, 0x49, 0x24, 0x40, 0x04, 0x3f, 0xf8, 0x00, 0x00, 0x00, 0x00};

/**
 * @return 0 in case of success, or a negative error code otherwise, in particular:
 * -ERROR_GENERIC
 * -ERROR_PERIPHERAL_NOT_DETECTED
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
 *
 */
int interface_task(void) {
    int res;

    /* Handle buttons */
    buttons_task();

    /* Handle display */

    /* Handle accelerometer */
    accelerometer_task();

    /* Handle input: magnet sensor */
    static enum {
        STATE_0_MAGNET_NOT_DETECTED,
        STATE_1_MAGNET_DETECTED,
    } m_magnet_sm;
    switch (m_magnet_sm) {

        case STATE_0_MAGNET_NOT_DETECTED: {

            /* Wait for magnet to be detected */
            if (magnet_detected_get() == true) {
                if (app_state_get() == APP_STATE_HEATING) {
                    app_sleep();
                }
                m_magnet_sm = STATE_1_MAGNET_DETECTED;
            }
            break;
        }

        case STATE_1_MAGNET_DETECTED: {

            /* Wait for magnet to not be detected */
            if (magnet_detected_get() == false) {
                if (app_state_get() == APP_STATE_ASLEEP) {
                    app_wake();
                }
                m_magnet_sm = STATE_0_MAGNET_NOT_DETECTED;
            }
            break;
        }
    }

    /* Handle output: display */
    switch (m_sm) {

        case STATE_SPLASH_0: {

            /* Display splash page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_ninja, 16, 16, 1);
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
            m_library.drawBitmap(0, 0, m_icon_ninja, 16, 16, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.printf("v%u.%u.%u", version_major, version_minor, version_patch);
            m_library.setCursor(20, 9);
            m_library.print(version_commit);
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
            char product_number[12 + 1];
            char serial_number[12 + 1];
            res = settings_product_get(product_number, serial_number);  // TODO Change to prevent overflow
            if (res != 1) {
                m_sm = STATE_USER_0;
                break;
            }

            /* Display product info */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_ninja, 16, 16, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.printf(product_number);
            m_library.setCursor(20, 9);
            m_library.print(serial_number);
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
            uint8_t icon[32];
            char text_line1[12 + 1];
            char text_line2[12 + 1];
            res = settings_user_get(icon, text_line1, text_line2);  // TODO Change to prevent overflow
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

            /* Move on according to app state */
            switch (app_state_get()) {
                case APP_STATE_LOCKED: {
                    log_d("Redirect to STATE_MONITOR_LOCKED");
                    m_sm = STATE_MONITOR_LOCKED;
                    break;
                }
                case APP_STATE_HEATING: {
                    log_d("Redirect to STATE_MONITOR_HEATING");
                    accelerometer_idle_reset();
                    m_sm = STATE_MONITOR_HEATING;
                    break;
                }
                case APP_STATE_ASLEEP: {
                    log_d("Redirect to STATE_MONITOR_ASLEEP");
                    accelerometer_wake_reset();
                    m_sm = STATE_MONITOR_ASLEEP;
                    break;
                }
                default: {
                    log_e("Unexpected state!");
                    m_sm = STATE_SPLASH_0;
                    return -ERROR_STATE_UNEXPECTED;
                }
            }
            break;
        }

        case STATE_MONITOR_LOCKED: {

            /* Ensure app state is still coherent */
            if (app_state_get() != APP_STATE_LOCKED) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Display monitor page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_lock, 16, 16, 1);
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            if (element_connected_get()) {
                float temperature_c = 0;
                res = element_temperature_measured_get(temperature_c);
                if (res < 0) {
                    m_library.print("err");
                } else {
                    m_library.printf("%03.0f", temperature_c);                                // TODO Use settings to change units
                    m_library.drawBitmap(17 + 12 + 12 + 12, 0, m_icon_degrees_c, 10, 16, 1);  // TODO Use settings to change units
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
                    app_unlock();
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    app_lock();
                    m_sm = STATE_MENU_HOME;
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MONITOR_HEATING: {

            /* Ensure app state is still coherent */
            if (app_state_get() != APP_STATE_HEATING) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Display monitor page */
            m_library.clear();
            if (app_boost_activated_get()) {
                static uint32_t m_boost_warning_timestamp;
                if ((millis() - m_boost_warning_timestamp) >= 1000) {
                    m_boost_warning_timestamp = millis();
                    m_library.drawBitmap(0, 0, m_icon_bolt, 16, 16, 1);
                } else if ((millis() - m_boost_warning_timestamp) >= 500) {
                    m_library.drawBitmap(0, 0, m_icon_bolt, 16, 16, 1);
                } else {
                    m_library.drawBitmap(0, 0, m_icon_boost, 16, 16, 1);
                }
            } else {
                m_library.drawBitmap(0, 0, m_icon_bolt, 16, 16, 1);
            }
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            if (element_connected_get()) {
                float temperature_c = 0;
                res = element_temperature_measured_get(temperature_c);
                if (res < 0) {
                    m_library.print("err");
                } else {
                    m_library.printf("%03.0f", temperature_c);                                // TODO Use settings to change units
                    m_library.drawBitmap(17 + 12 + 12 + 12, 0, m_icon_degrees_c, 10, 16, 1);  // TODO Use settings to change units
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

            /* Handle accelerometer */
            if (accelerometer_idle_detected_get()) {
                app_sleep();
            }

            /* Handle buttons
             * Short and long presses on either button will adjust target temperature
             * A short press on both buttons will trigger a lock
             * A long press on both buttons will trigger a lock and open the menu */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG: {
                    app_target_decrease();
                    m_timestamp = millis();
                    m_sm = STATE_MONITOR_ADJUST;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    app_target_increase();
                    m_timestamp = millis();
                    m_sm = STATE_MONITOR_ADJUST;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    app_lock();
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    app_lock();
                    m_sm = STATE_MENU_HOME;
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MONITOR_ASLEEP: {

            /* Ensure app state is still coherent */
            if (app_state_get() != APP_STATE_ASLEEP) {
                m_sm = STATE_MONITOR_REDIRECT;
                break;
            }

            /* Display monitor page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_sleep, 16, 16, 1);
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            if (element_connected_get()) {
                float temperature_c = 0;
                res = element_temperature_measured_get(temperature_c);
                if (res < 0) {
                    m_library.print("err");
                } else {
                    m_library.printf("%03.0f", temperature_c);                                // TODO Use settings to change units
                    m_library.drawBitmap(17 + 12 + 12 + 12, 0, m_icon_degrees_c, 10, 16, 1);  // TODO Use settings to change units
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

            /* Handle accelerometer */
            if (accelerometer_wake_detected_get()) {
                app_wake();
            }

            /* Handle buttons
             * Short and long presses on either button will trigger a wake
             * A short press on both buttons will trigger a lock
             * A long press on both buttons will trigger a lock and open the menu */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG:
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    app_wake();
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    app_lock();
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    app_lock();
                    m_sm = STATE_MENU_HOME;
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

            /* Display monitor page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_thermometer, 16, 16, 1);
            m_library.setTextSize(2);
            m_library.setCursor(17, 1);
            m_library.printf("%03.0f", app_target_get());                             // TODO Use settings to change units
            m_library.drawBitmap(17 + 12 + 12 + 12, 0, m_icon_degrees_c, 10, 16, 1);  // TODO Use settings to change units
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG: {
                    app_target_decrease();
                    m_timestamp = millis();
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    app_target_increase();
                    m_timestamp = millis();
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_HOME: {
            m_sm = STATE_MENU_DISPLAY_ROTATION_0;
            break;
        }

        case STATE_MENU_DISPLAY_ROTATION_0: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_settings, 16, 16, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Settings");
            m_library.setCursor(20, 9);
            m_library.print("Disp rot.");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
                    m_sm = STATE_MENU_DISPLAY_BRIGHTNESS_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    m_sm = STATE_MENU_DISPLAY_ROTATION_1;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_DISPLAY_ROTATION_1: {

            /* Retrieve interface orientation from settings */
            bool left_handed = false;
            res = settings_interface_rotation_get(left_handed);

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_settings, 16, 16, 1);
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
                    m_sm = STATE_MENU_DISPLAY_ROTATION_0;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_MONITOR_REDIRECT;
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_MENU_DISPLAY_BRIGHTNESS_0: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_settings, 16, 16, 1);
            m_library.setTextSize(1);
            m_library.setCursor(20, 0);
            m_library.print("Settings");
            m_library.setCursor(20, 9);
            m_library.print("Disp bright.");
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT: {
                    m_sm = STATE_MENU_DISPLAY_ROTATION_0;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT: {
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
            }

            /* That's it */
            break;
        }

        case STATE_MENU_DISPLAY_BRIGHTNESS_1: {

            /* Retrieve brightness from settings */
            int brightness = 100;
            res = settings_display_brightness_get(brightness);

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_settings, 16, 16, 1);
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
            }

            break;
        }

        default: {
            log_e("Unexpected state!");
            m_sm = STATE_SPLASH_0;
            return -ERROR_STATE_UNEXPECTED;
        }
    }

    /* Return success */
    return 0;
}
