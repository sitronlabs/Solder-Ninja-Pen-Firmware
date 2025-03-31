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
#ifndef CONFIG_LOG_BUFFER_SIZE
#define CONFIG_LOG_BUFFER_SIZE 2560  //!< Total size of the rolling log buffer.
#endif

/* Local variables */
static char m_buffer[CONFIG_LOG_BUFFER_SIZE];  // Ring buffer to hold log messages.
static volatile size_t m_buffer_head = 0;      // Next write position.
static volatile size_t m_buffer_tail = 0;      // Next read position.
static HardwareSerial *m_uart_library = NULL;

/**
 * Append a block of data to the rolling log buffer.
 * If there isn’t enough free space, the oldest data is overwritten.
 */
static void m_buffer_append(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        m_buffer[m_buffer_head] = data[i];
        m_buffer_head = (m_buffer_head + 1) % CONFIG_LOG_BUFFER_SIZE;

        /* If buffer becomes full, advance tail (i.e. drop oldest data) */
        if (m_buffer_head == m_buffer_tail) {
            m_buffer_tail = (m_buffer_tail + 1) % CONFIG_LOG_BUFFER_SIZE;
        }
    }
}

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
    char entry[CONFIG_LOG_ENTRY_MAX_LENGTH + 3];
    size_t entry_len = 0;
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

    /* Append header */
    (void)file;
    (void)line;
    const char *const levels[] = {
        [LOG_LEVEL_TRACE] = "trc",
        [LOG_LEVEL_DEBUG] = "dbg",
        [LOG_LEVEL_INFO] = "nfo",
        [LOG_LEVEL_WARNING] = "wrn",
        [LOG_LEVEL_ERROR] = "err",
    };
    res = sprintf(&entry[entry_len], "[%" PRIu32 "] [%s] ", millis(), levels[level]);
    if (res < 0) {
        return -1;
    }
    entry_len += res;

    /* Append message */
    va_list args;
    va_start(args, format);
    res = vsnprintf(&entry[entry_len], CONFIG_LOG_ENTRY_MAX_LENGTH - entry_len, format, args);
    va_end(args);
    entry_len += res;

    /* Append newline */
    entry[entry_len++] = '\r';
    entry[entry_len++] = '\n';

    // Check available space (if not enough, oldest data will be dropped).
    // size_t free_space = m_buffer_free_space();
    // if (entry_len > free_space) {
    //     // Optionally, you could signal an overflow condition here.
    // }

    // Append the formatted log entry to the rolling buffer.
    m_buffer_append(entry, entry_len);

    /* Return success */
    return 0;
}

/**
 * @brief
 * @param
 * @return
 */
int log_task(void) {

    /* Ensure output peripheral has been configured */
    if (m_uart_library == NULL) {
        return -EINVAL;
    }

    if (m_buffer_tail != m_buffer_head) {
        m_uart_library->write(m_buffer[m_buffer_tail]);
        m_buffer_tail = (m_buffer_tail + 1) % CONFIG_LOG_BUFFER_SIZE;
        m_uart_library->flush();
    }

    /* Return success */
    return 0;
}
