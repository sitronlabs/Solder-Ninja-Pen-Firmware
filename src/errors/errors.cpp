/* Self header */
#include "errors.h"

/**
 * @brief List of error names.
 * Generated automatically from the ERROR_TABLE. */
#define X(a, b) b,
static const char *m_error_name[] = {"", ERROR_TABLE};

#undef X

/**
 * @brief Convert error code to human-readable string
 * @param code Error code to convert
 * @return Pointer to error string, or "unknown error" if code is invalid
 */
const char *error_to_string(const enum error_code code) {
    if ((code > ERROR_SYSTEM_LAST) && (code < (ERROR_SYSTEM_LAST + sizeof(m_error_name) / sizeof(m_error_name[0])))) {
        return m_error_name[code - ERROR_SYSTEM_LAST];
    } else {
        return "unknown error";
    }
}
