/* Self header */
#include "errors.h"

/**
 * List of error names.
 * Generated automatically from the ERROR_TABLE. */
#define X(a, b) b,
static const char *m_error_name[] = {"", ERROR_TABLE};

#undef X

/**
 *
 */
const char *error_to_string(const enum error_code code) {
    if (code > ERROR_SYSTEM_LAST && code < ERROR_PROJECT_LAST) {
        return m_error_name[code - ERROR_SYSTEM_LAST];
    } else {
        return m_error_name[0];
    }
}
