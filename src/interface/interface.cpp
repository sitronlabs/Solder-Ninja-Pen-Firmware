/* Self header */
#include "interface.h"

/* Project */
#include "../app/app.h"
#include "../app/power/power.h"
#include "../element/element.h"
#include "../log/log.h"
#include "../settings/settings.h"
#include "buttons.h"

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
    STATE_0_SPLASH,
    STATE_1_SPLASH,
    STATE_2_MONITOR_REDIRECT,
    STATE_3_MONITOR_LOCKED,
    STATE_4_MONITOR_HEATING,
    STATE_5_MONITOR_ASLEEP,
    STATE_6_MONITOR_ADJUST,
    STATE_7_MENU,
} m_sm;
static float m_temperature_c;

/* Icon bitmaps */
const uint8_t m_icon_ninja[32] = {0x03, 0xF8, 0x07, 0xFC, 0x0F, 0xFE, 0xDF, 0xFF, 0x7F, 0xFF, 0x78, 0x03, 0x73, 0x33, 0x73, 0x31, 0xF0, 0x03, 0x1F, 0xFF, 0x1F, 0xFF, 0x1F, 0xFE, 0x0F, 0xFE, 0x0F, 0xFC, 0x07, 0xF8, 0x01, 0xF0};
const uint8_t m_icon_bolt[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x60, 0x00, 0xE0, 0x01, 0xC0, 0x03, 0xC0, 0x07, 0xF0, 0x0F, 0xE0, 0x03, 0xC0, 0x03, 0x80, 0x07, 0x00, 0x06, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00};
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

    /* Setup display library */
#if R4J
    res = m_library.setup(Wire, 0x3C, PF1, m_buffer);
#elif R8A
    res = m_library.setup(Wire, 0x3C, 6, m_buffer);
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

    /* State machine */
    switch (m_sm) {

        case STATE_0_SPLASH: {

            // /* Wait for settings to have loaded */
            // res = settings_loaded();
            // if (res == 0) {
            //     break;
            // }

            /* Use settings to customize splash page */
            const uint8_t* icon = m_icon_ninja;  // TODO
            const char* text_line1 = "Solder";   // TODO
            const char* text_line2 = "Ninja";    // TODO

            /* Display splash page */
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
            m_sm = STATE_1_SPLASH;
            break;
        }

        case STATE_1_SPLASH: {

            /* Wait for timeout */
            if ((millis() - m_timestamp) < CONFIG_UI_SPLASH_DURATION) {
                break;
            }

            /* Move on */
            m_sm = STATE_2_MONITOR_REDIRECT;
            break;
        }

        case STATE_2_MONITOR_REDIRECT: {

            /* Move on according to app state */
            switch (app_state_get()) {
                case APP_STATE_LOCKED: {
                    log_d("Redirect to STATE_3_MONITOR_LOCKED");
                    m_sm = STATE_3_MONITOR_LOCKED;
                    break;
                }
                case APP_STATE_HEATING: {
                    log_d("Redirect to STATE_4_MONITOR_HEATING");
                    m_sm = STATE_4_MONITOR_HEATING;
                    break;
                }
                case APP_STATE_ASLEEP: {
                    log_d("Redirect to STATE_5_MONITOR_ASLEEP");
                    m_sm = STATE_5_MONITOR_ASLEEP;
                    break;
                }
                default: {
                    log_e("Unexpected state!");
                    m_sm = STATE_0_SPLASH;
                    return -ERROR_STATE_UNEXPECTED;
                }
            }
            break;
        }

        case STATE_3_MONITOR_LOCKED: {

            /* Ensure app state is still coherent */
            if (app_state_get() != APP_STATE_LOCKED) {
                m_sm = STATE_2_MONITOR_REDIRECT;
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
                    m_temperature_c = (temperature_c + 9 * m_temperature_c) / 10.0;
                    m_library.printf("%03.0f", m_temperature_c);                              // TODO Use settings to change units
                    m_library.drawBitmap(17 + 12 + 12 + 12, 0, m_icon_degrees_c, 10, 16, 1);  // TODO Use settings to change units
                }
            } else {
                m_library.print("tip");
            }
            m_library.setTextSize(1);
            struct power_option contract;
            res = power_contract_get(&contract);
            if (res < 0) {
                m_library.setCursor(11 * 6, 0);
                m_library.printf("--.-V", 12.3);
                m_library.setCursor(11 * 6, 9);
                m_library.printf("-.-A", 2.5);
            } else {
                m_library.setCursor(11 * 6, 0);
                m_library.printf("%4.1fV", contract.voltage_max);
                m_library.setCursor(12 * 6, 9);
                m_library.printf("%3.1fA", contract.current_max);
            }
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_BOTH_SHORT: {
                    app_unlock(APP_LOCK_SOURCE_BUTTONS);
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    app_lock(APP_LOCK_SOURCE_BUTTONS);
                    m_sm = STATE_7_MENU;
                    break;
                }
            }

            /* Handle magnet */
            // TODO
            // Actually maybe magnet should be managed by app directly?

            /* That's it */
            break;
        }

        case STATE_4_MONITOR_HEATING: {

            /* Ensure app state is still coherent */
            if (app_state_get() != APP_STATE_HEATING) {
                m_sm = STATE_2_MONITOR_REDIRECT;
                break;
            }

            /* Display monitor page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_bolt, 16, 16, 1);
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
            res = power_contract_get(&contract);
            if (res < 0) {
                m_library.setCursor(11 * 6, 0);
                m_library.printf("%4.1fV", 12.3);
                m_library.setCursor(11 * 6, 9);
                m_library.printf("%3.1fA", 2.5);
            } else {
                m_library.setCursor(11 * 6, 0);
                m_library.printf("%4.1fV", 12.3);
                m_library.setCursor(12 * 6, 9);
                m_library.printf("%3.1fA", 2.5);
            }
            m_library.display();

            /* Handle buttons
             * Short and long presses on either buttons will adjust target temperature
             * A short press on both buttons will trigger a lock
             * A long press on both buttons will trigger a lock and open the menu */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG: {
                    app_target_decrease();
                    m_timestamp = millis();
                    m_sm = STATE_6_MONITOR_ADJUST;
                    break;
                }
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    app_target_increase();
                    m_timestamp = millis();
                    m_sm = STATE_6_MONITOR_ADJUST;
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    app_lock(APP_LOCK_SOURCE_BUTTONS);
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    app_lock(APP_LOCK_SOURCE_BUTTONS);
                    m_sm = STATE_7_MENU;
                    break;
                }
            }

            /* That's it */
            break;
        }

        case STATE_5_MONITOR_ASLEEP: {

            /* Ensure app state is still coherent */
            if (app_state_get() != APP_STATE_ASLEEP) {
                m_sm = STATE_2_MONITOR_REDIRECT;
                break;
            }

            /* Display monitor page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_sleep, 16, 16, 1);
            // TODO
            m_library.display();

            /* Handle buttons
             * Short and long presses on either buttons will trigger a wake
             * A short press on both buttons will trigger a lock
             * A long press on both buttons will trigger a lock and open the menu */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_LEFT_SHORT:
                case BUTTONS_EVENT_LEFT_LONG:
                case BUTTONS_EVENT_RIGHT_SHORT:
                case BUTTONS_EVENT_RIGHT_LONG: {
                    app_wake();
                    break;
                }
                case BUTTONS_EVENT_BOTH_SHORT: {
                    app_lock(APP_LOCK_SOURCE_BUTTONS);  // APP_LOCK_INTERFACE_BUTTON
                    m_sm = STATE_2_MONITOR_REDIRECT;
                    break;
                }
                case BUTTONS_EVENT_BOTH_LONG: {
                    app_lock(APP_LOCK_SOURCE_BUTTONS);  // APP_LOCK_INTERFACE_BUTTON
                    m_sm = STATE_7_MENU;
                    break;
                }
            }
            break;
        }

        case STATE_6_MONITOR_ADJUST: {

            /* Revert after timeout */
            if ((millis() - m_timestamp) >= 1000) {  // TODO Change by a config value
                m_sm = STATE_2_MONITOR_REDIRECT;
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

        case STATE_7_MENU: {

            /* Display menu page */
            m_library.clear();
            m_library.drawBitmap(0, 0, m_icon_settings, 16, 16, 1);
            m_library.display();

            /* Handle buttons */
            switch (buttons_event_get()) {
                case BUTTONS_EVENT_BOTH_LONG: {
                    m_sm = STATE_2_MONITOR_REDIRECT;
                    break;
                }
            }

            /* That's it */
            break;
        }

        default: {
            log_e("Unexpected state!");
            m_sm = STATE_0_SPLASH;
            return -ERROR_STATE_UNEXPECTED;
        }
    }

    /* Return success */
    return 0;
}
