/* Self header */
#include "buttons.h"

/* Arduino libraries */
#include <Arduino.h>

/* */
enum buttons_states {
    STATE_0,
    STATE_1,
    STATE_2,
};

/* */
static struct {
    uint32_t timestamp = 0;
    bool pressed = false;
    enum buttons_states sm = STATE_0;
} m_buttons[3];

/* */
enum buttons_event m_event = BUTTONS_EVENT_NONE;

/**
 */
int buttons_setup(void) {

    /* Setup pins */
#if R4J
    pinMode(PB7, INPUT);
    pinMode(PA15, INPUT_PULLDOWN);
#elif R8A
    pinMode(2, INPUT_PULLUP);
    pinMode(8, INPUT_PULLUP);
#endif

    return 0;
}

enum buttons_event buttons_event_get(void) {
    enum buttons_event ret = m_event;
    m_event = BUTTONS_EVENT_NONE;
    return ret;
}

/**
 *
 */
int buttons_task(void) {

    /* Determine if the buttons are pressed */
#if R4J
    m_buttons[0].pressed = (digitalRead(PB7) == HIGH) ? true : false;
    m_buttons[2].pressed = (digitalRead(PA15) == HIGH) ? true : false;
    m_buttons[1].pressed = m_buttons[0].pressed & m_buttons[2].pressed;
#elif R8A
    m_buttons[0].pressed = (digitalRead(2) == LOW) ? true : false;
    m_buttons[2].pressed = (digitalRead(8) == LOW) ? true : false;
    m_buttons[1].pressed = m_buttons[0].pressed & m_buttons[2].pressed;
#endif

    /* For left button */
    switch (m_buttons[0].sm) {

        case STATE_0: {

            /* Move on when button is pressed */
            if (m_buttons[0].pressed) {
                m_buttons[0].timestamp = millis();
                m_buttons[0].sm = STATE_1;
            }
            break;
        }

        case STATE_1: {

            /* Compute amount of time button has been pressed */
            uint32_t duration = millis() - m_buttons[0].timestamp;

            /* If combined button is pressed ignore the rest */
            if (m_buttons[1].pressed) {
                m_buttons[0].sm = STATE_0;
            }

            /* Handle short press */
            else if (m_buttons[0].pressed != true) {
                if (duration > CONFIG_BUTTONS_PRESS_SHORT_DURATION && duration < CONFIG_BUTTONS_PRESS_LONG_DURATION) {
                    m_event = BUTTONS_EVENT_LEFT_SHORT;
                }
                m_buttons[0].sm = STATE_0;
            }

            /* Handle long press */
            else if (duration >= CONFIG_BUTTONS_PRESS_LONG_DURATION) {
                m_event = BUTTONS_EVENT_LEFT_LONG;
                m_buttons[0].timestamp = millis();
                m_buttons[0].sm = STATE_2;
            }
            break;
        }

        case STATE_2: {

            /* Compute amount of time button has been pressed */
            uint32_t duration = millis() - m_buttons[0].timestamp;

            /* If combined button is pressed ignore the rest */
            if (m_buttons[1].pressed) {
                m_buttons[0].sm = STATE_0;
            }

            /* Handle long press */
            else if (duration >= CONFIG_BUTTONS_PRESS_LONG_REPEAT_DURATION) {
                m_event = BUTTONS_EVENT_LEFT_LONG;
                m_buttons[0].timestamp = millis();
            } else if (!m_buttons[0].pressed) {
                m_buttons[0].sm = STATE_0;
            }
            break;
        }

        default: {
            m_buttons[0].sm = STATE_0;
            break;
        }
    }

    /* Handle right button */
    switch (m_buttons[2].sm) {

        case STATE_0: {

            /* Move on when button is pressed */
            if (m_buttons[2].pressed) {
                m_buttons[2].timestamp = millis();
                m_buttons[2].sm = STATE_1;
            }
            break;
        }

        case STATE_1: {

            /* Compute amount of time button has been pressed */
            uint32_t duration = millis() - m_buttons[2].timestamp;

            /* If combined button is pressed ignore the rest */
            if (m_buttons[1].pressed) {
                m_buttons[2].sm = STATE_0;
            }

            /* Handle short press */
            else if (m_buttons[2].pressed != true) {
                if (duration > CONFIG_BUTTONS_PRESS_SHORT_DURATION && duration < CONFIG_BUTTONS_PRESS_LONG_DURATION) {
                    m_event = BUTTONS_EVENT_RIGHT_SHORT;
                }
                m_buttons[2].sm = STATE_0;
            }

            /* Handle long press */
            else if (duration >= CONFIG_BUTTONS_PRESS_LONG_DURATION) {
                m_event = BUTTONS_EVENT_RIGHT_LONG;
                m_buttons[2].timestamp = millis();
                m_buttons[2].sm = STATE_2;
            }
            break;
        }

        case STATE_2: {

            /* Compute amount of time button has been pressed */
            uint32_t duration = millis() - m_buttons[2].timestamp;

            /* If combined button is pressed ignore the rest */
            if (m_buttons[1].pressed) {
                m_buttons[2].sm = STATE_0;
            }

            /* Handle long press */
            else if (duration >= CONFIG_BUTTONS_PRESS_LONG_REPEAT_DURATION) {
                m_event = BUTTONS_EVENT_RIGHT_LONG;
                m_buttons[2].timestamp = millis();
            } else if (!m_buttons[2].pressed) {
                m_buttons[2].sm = STATE_0;
            }
            break;
        }

        default: {
            m_buttons[2].sm = STATE_0;
            break;
        }
    }

    /* For combined button */
    switch (m_buttons[1].sm) {

        case STATE_0: {

            /* Move on when button is pressed */
            if (m_buttons[1].pressed) {
                m_buttons[1].timestamp = millis();
                m_buttons[1].sm = STATE_1;
            }
            break;
        }

        case STATE_1: {

            /* Compute amount of time button has been pressed */
            uint32_t duration = millis() - m_buttons[1].timestamp;

            /* Handle short press */
            if (m_buttons[1].pressed != true) {
                if (duration > CONFIG_BUTTONS_PRESS_SHORT_DURATION && duration < CONFIG_BUTTONS_PRESS_LONG_DURATION) {
                    m_event = BUTTONS_EVENT_BOTH_SHORT;
                }
                m_buttons[1].sm = STATE_0;
            }

            /* Handle long press */
            else if (duration >= CONFIG_BUTTONS_PRESS_LONG_DURATION) {
                m_event = BUTTONS_EVENT_BOTH_LONG;
                m_buttons[1].timestamp = millis();
                m_buttons[1].sm = STATE_2;
            }
            break;
        }

        case STATE_2: {

            /* Compute amount of time button has been pressed */
            uint32_t duration = millis() - m_buttons[1].timestamp;

            /* Handle long press */
            if (duration >= CONFIG_BUTTONS_PRESS_LONG_REPEAT_DURATION) {
                m_event = BUTTONS_EVENT_BOTH_LONG;
                m_buttons[1].timestamp = millis();
            } else if (!m_buttons[1].pressed) {
                m_buttons[1].sm = STATE_0;
            }
            break;
        }

        default: {
            m_buttons[1].sm = STATE_0;
            break;
        }
    }

    /* Return success */
    return 0;
}
