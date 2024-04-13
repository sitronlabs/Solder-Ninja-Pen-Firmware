#ifndef BUTTONS_H
#define BUTTONS_H

/* Config */
#include "../cfg/config.h"

/* C/C++ libraries */
#include <errno.h>
#include <stdint.h>

/* Setup */
int buttons_setup(void);

/* Events */
enum buttons_event {
    BUTTONS_EVENT_NONE,
    BUTTONS_EVENT_LEFT_SHORT,
    BUTTONS_EVENT_LEFT_LONG,
    BUTTONS_EVENT_RIGHT_SHORT,
    BUTTONS_EVENT_RIGHT_LONG,
    BUTTONS_EVENT_BOTH_SHORT,
    BUTTONS_EVENT_BOTH_LONG,
};
enum buttons_event buttons_event_get(void);

/* Task */
int buttons_task(void);

#endif
