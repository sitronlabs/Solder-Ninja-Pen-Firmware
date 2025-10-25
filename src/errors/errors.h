#ifndef ERRORS_H
#define ERRORS_H

/* C/C++ libraries */
#include <errno.h>

/**
 * List of project specific errors.
 * This list will be expanded to both an enum list and a string array.
 * @see https://www.drdobbs.com/the-new-c-x-macros/184401387 */
#define ERROR_TABLE                                                     \
    X(ERROR_GENERIC, "generic error")                                   \
    X(ERROR_GENERIC_ARGUMENT_INVALID, "invalid argument")               \
    X(ERROR_GENERIC_TIMEOUT, "operation timeout")                       \
    X(ERROR_GENERIC_BUFFER_OVERFLOW, "buffer overflow")                 \
    X(ERROR_GENERIC_STATE_INVALID, "invalid state")                     \
    X(ERROR_GENERIC_STATE_UNEXPECTED, "unexpected state")               \
    X(ERROR_POWER_INSUFFICIENT, "not enough power")                     \
    X(ERROR_PERIPHERAL_NOT_DETECTED, "peripheral not detected")         \
    X(ERROR_PERIPHERAL_COMMUNICATION, "peripheral communication error") \
    X(ERROR_PERIPHERAL_SETUP_ERROR, "peripheral setup error")           \
    X(ERROR_SETTINGS_HEADER_CORRUPTED, "corrupted header")              \
    X(ERROR_SETTINGS_HEADER_UNEXPECTED, "unexpected header")            \
    X(ERROR_SETTINGS_DATA_CORRUPTED, "corrupted data")                  \
    X(ERROR_SETTINGS_DESERIALIZATION_FAILED, "deserialization failed")

/**
 * List of error codes.
 * Generated automatically from the ERROR_TABLE. */
#define X(a, b) a,
enum error_code {
    ERROR_SYSTEM_LAST = __ELASTERROR,
    ERROR_TABLE
};
#undef X

/**
 * @brief Convert error code to human-readable string
 * @param code Error code to convert
 * @return Pointer to error string, or "unknown error" if code is invalid
 */
const char *error_to_string(const enum error_code code);

#endif
