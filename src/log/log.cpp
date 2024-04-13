/* Self header */
#include "log.h"

#if R8A
/* Pico libraries */
#include <tusb.h>
#endif

/* Config */
#include "../cfg/config.h"
#ifndef CONFIG_LOG_ENTRY_MAX_LENGTH
#define CONFIG_LOG_ENTRY_MAX_LENGTH 256  //!< Maximum length in characters of a log message.
#endif

/* Local variables */
static char m_buffer[CONFIG_LOG_ENTRY_MAX_LENGTH + 3];
static HardwareSerial *m_uart_library = NULL;

/**
 *
 */
int log_setup(void) {

    /* */
#if R4J
    m_uart_library = &Serial;
#elif R8A
    m_uart_library = &Serial1;
    Serial1.begin(115200);
    // m_uart_library = &Serial;  // Temp
    // Serial.begin(115200);      // Temp
    // pinMode(8, INPUT_PULLUP);
    // if (digitalRead(8) == LOW) {
    //     uint32_t t = millis();
    //     while (1) {
    //         if (millis() - t >= 10000) {
    //             break;
    //         } else if (tud_cdc_connected()) {
    //             delay(500);
    //             break;
    //         }
    //     }
    // }
    Serial.printf("Hello world!\r\n");
#else
#error Invalid hardware version
#endif

    /* Return success */
    return 0;
}

/**
 * Adds an entry to the log.
 * @param[in] level The level of the log entry.
 * @param[in] file The name or path of the file that has called the function.
 * @param[in] line The line number of the file that has called the function.
 * @param[in] format The message formatter string potentially followed by the variables names to replace.
 */
int log_write(const enum log_level level, const char *const file, const unsigned int line, const char *const format, ...) {
    int res;

    /* Ensure output peripheral has been configured */
    if (m_uart_library == NULL) {
        return -EINVAL;
    }

    /* Ensure level is valid */
    if (level != LOG_LEVEL_TRACE &&
        level != LOG_LEVEL_DEBUG &&
        level != LOG_LEVEL_INFO &&
        level != LOG_LEVEL_WARNING &&
        level != LOG_LEVEL_ERROR) {
        return -EINVAL;
    }

    /* Add timestamp, level, file and line  */
    (void)file;
    (void)line;
    const char *const levels[] = {
        [LOG_LEVEL_TRACE] = "trc",
        [LOG_LEVEL_DEBUG] = "dbg",
        [LOG_LEVEL_INFO] = "nfo",
        [LOG_LEVEL_WARNING] = "wrn",
        [LOG_LEVEL_ERROR] = "err",
    };
    res = sprintf(&m_buffer[0], "[%" PRIu32 "] [%s] ", millis(), levels[level]);
    if (res < 0) {
        return -1;
    }

    /* Add content */
    va_list args;
    va_start(args, format);
    vsnprintf(&m_buffer[res], CONFIG_LOG_ENTRY_MAX_LENGTH - res, format, args);
    va_end(args);

    /* Output to uart */
    m_uart_library->println(m_buffer);
    m_uart_library->flush();

    /* Return success */
    return 0;
}
