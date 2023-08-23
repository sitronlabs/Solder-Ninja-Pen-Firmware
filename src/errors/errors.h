#ifndef ERRORS_H
#define ERRORS_H

/* C/C++ libraries */
#include <errno.h>

/**
 * List of project specific errors.
 * This list will be expanded to both an enum list and a string array.
 * @see https://www.drdobbs.com/the-new-c-x-macros/184401387 */
#define ERROR_TABLE                                             \
    X(ERROR_INVALID_ARGUMENT, "invalid argument")               \
    X(ERROR_NOT_ENOUGH_POWER, "not enough power")               \
    X(ERROR_I2C_COMMUNICATION, "i2c error")                     \
    X(ERROR_PERIPHERAL_NOT_DETECTED, "peripheral not detected") \
    X(ERROR_PERIPHERAL_SETUP_ERROR, "peripheral setup error")   \
    X(ERROR_STATE_UNEXPECTED, "unexpected state")               \
    X(ERROR_GENERIC, "generic error")

/**
 * List of error codes.
 * Generated automatically from the ERROR_TABLE. */
#define X(a, b) a,
enum error_code {
    ERROR_SYSTEM_LAST = __ELASTERROR,
    ERROR_TABLE
	ERROR_PROJECT_LAST,
};
#undef X

/* */
const char *error_to_string(const enum error_code code);

#endif
