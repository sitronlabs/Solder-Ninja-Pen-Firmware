/* Self header */
#include "log.h"

/* Config */
#include "../cfg/config.h"
#ifndef CONFIG_LOG_ENTRY_MAX_LENGTH
#define CONFIG_LOG_ENTRY_MAX_LENGTH 256  //!< Maximum length in characters of a log message.
#endif
#ifndef CONFIG_LOG_BUFFER_SIZE
#define CONFIG_LOG_BUFFER_SIZE 2560  //!< Total size of the rolling log buffer.
#endif
#ifndef CONFIG_LOG_BAUDRATE
#define CONFIG_LOG_BAUDRATE 115200  //!< UART baudrate for log output.
#endif
#ifndef CONFIG_LOG_FLUSH_TIMEOUT_MS
#define CONFIG_LOG_FLUSH_TIMEOUT_MS 1  //!< Maximum time to spend flushing log buffer per task call.
#endif

/* Local variables */
static char m_buffer[CONFIG_LOG_BUFFER_SIZE];  //!< Ring buffer to hold log messages
static volatile size_t m_buffer_head = 0;      //!< Next write position
static volatile size_t m_buffer_tail = 0;      //!< Next read position
static volatile size_t m_buffer_count = 0;     //!< Number of bytes currently in buffer
static volatile uint32_t m_log_missed = 0;     //!< Counter for missed log entries due to overflow
static HardwareSerial *m_uart_library = NULL;

/**
 * @brief Append a block of data to the rolling log buffer.
 * @param data Pointer to data to append
 * @param len Number of bytes to append
 * @return Number of bytes appended if data was appended successfully, negative error code if buffer overflow occurred
 * @note If there isn't enough free space, the data is rejected and overflow is signaled.
 */
static int m_buffer_append(const char *data, size_t len) {

    /* If there is no data to append, return success */
    if (len == 0) {
        return 0;
    }

    /* Check if we have enough space for the new data
     * If not, increment the missed counter and return failure */
    if ((m_buffer_count + len) > CONFIG_LOG_BUFFER_SIZE) {
        m_log_missed++;
        return -ENOSPC;
    }

    /* Handle case where we need to wrap around
     * Write first part to end of buffer
     * Write remaining part from start of buffer */
    if ((m_buffer_head + len) > CONFIG_LOG_BUFFER_SIZE) {
        size_t first_part = CONFIG_LOG_BUFFER_SIZE - m_buffer_head;
        memcpy(&m_buffer[m_buffer_head], data, first_part);
        size_t second_part = len - first_part;
        memcpy(m_buffer, &data[first_part], second_part);
        m_buffer_head = second_part;
    }

    /* Simple case: no wrap around needed
     * Write data to buffer */
    else {
        memcpy(&m_buffer[m_buffer_head], data, len);
        m_buffer_head = (m_buffer_head + len) % CONFIG_LOG_BUFFER_SIZE;
    }

    /* Update count and return success */
    m_buffer_count += len;
    return len;
}

/**
 * @brief Get the number of bytes currently in the buffer
 * @return Number of bytes that have been written to the buffer
 */
static size_t m_buffer_bytes_used(void) {
    return m_buffer_count;
}

/**
 * @brief Initialize the logging system
 * @return 0 on success, negative error code on failure
 */
int log_setup(void) {
    /* Initialize UART based on hardware version */
#if R4J
    m_uart_library = &Serial;
#elif R8A
    m_uart_library = &Serial1;
    Serial1.begin(CONFIG_LOG_BAUDRATE);
#else
#error Invalid hardware version
#endif

    /* Initialize buffer state */
    m_buffer_head = 0;
    m_buffer_tail = 0;
    m_buffer_count = 0;
    m_log_missed = 0;

    /* Return success */
    return 0;
}

/**
 * @brief Adds an entry to the log
 * @param level The level of the log entry
 * @param file The name or path of the file that has called the function
 * @param line The line number of the file that has called the function
 * @param format The message formatter string potentially followed by the variables names to replace
 * @return 0 on success, negative error code on failure
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
    if (level < LOG_LEVEL_TRACE || level > LOG_LEVEL_ERROR) {
        return -EINVAL;
    }

    /* Format log level strings */
    static const char *const levels[] = {
        [LOG_LEVEL_TRACE] = "trc",
        [LOG_LEVEL_DEBUG] = "dbg",
        [LOG_LEVEL_INFO] = "nfo",
        [LOG_LEVEL_WARNING] = "wrn",
        [LOG_LEVEL_ERROR] = "err",
    };

    /* Extract filename from full path */
    const char *filename = file;
    const char *last_slash = strrchr(file, '/');
    const char *last_backslash = strrchr(file, '\\');
    const char *last_separator = (last_slash > last_backslash) ? last_slash : last_backslash;
    if (last_separator != NULL) {
        filename = last_separator + 1;
    }

    /* Build log entry header with timestamp, level, and source location */
    res = snprintf(&entry[entry_len], CONFIG_LOG_ENTRY_MAX_LENGTH - entry_len, "[%" PRIu32 "] [%s] [%s:%u] ", millis(), levels[level], filename, line);
    entry_len += res;

    /* Append formatted message */
    va_list args;
    va_start(args, format);
    res = vsnprintf(&entry[entry_len], CONFIG_LOG_ENTRY_MAX_LENGTH - entry_len, format, args);
    va_end(args);
    entry_len += res;

    /* Append newline */
    entry[entry_len++] = '\r';
    entry[entry_len++] = '\n';

    /* Try to append the formatted log entry to the rolling buffer */
    res = m_buffer_append(entry, entry_len);
    if (res < 0) {
        return res;
    }

    /* Return success */
    return 0;
}

/**
 * @brief Process the log buffer and flush to UART
 * @return 0 on success, negative error code on failure
 * @note This function should be called regularly to flush buffered log messages
 */
int log_task(void) {
    int res;

    /* Ensure output peripheral has been configured */
    if (m_uart_library == NULL) {
        return -EINVAL;
    }

    /* If buffer is empty and we have previously missed data, report it */
    if ((m_buffer_bytes_used() == 0) && (m_log_missed > 0)) {
        char missed_msg[128];
        int msg_len = snprintf(missed_msg, sizeof(missed_msg), "[%" PRIu32 "] [wrn] [log.cpp:%u] %" PRIu32 " log entries missed due to buffer overflow!\r\n", millis(), __LINE__, m_log_missed);

        /* Try to append the missed message to buffer
         * If buffer is still full, the missed message itself will be lost, but the counter will remain for next time */
        res = m_buffer_append(missed_msg, msg_len);
        if (res >= 0) {
            m_log_missed = 0;
        }
    }

    /* If buffer has data, send it */
    if (m_buffer_bytes_used() > 0) {

        /* Calculate maximum characters we can send in the time slot
         * Each character is 10 bits (1 start + 8 data + 1 stop), so chars/sec = baudrate/10 */
        const uint32_t chars_per_ms = CONFIG_LOG_BAUDRATE / 10000;
        const uint32_t chars_limit_timeslot = chars_per_ms * CONFIG_LOG_FLUSH_TIMEOUT_MS;

        /* Send as many characters as possible in the time slot, in bulk for better efficiency */
        for (uint32_t chars_sent = 0; (m_buffer_bytes_used() > 0) && (chars_sent < chars_limit_timeslot);) {

            /* Calculate the number of characters to send in this iteration */
            size_t chars_to_send = m_buffer_bytes_used();
            if (chars_to_send > (chars_limit_timeslot - chars_sent)) {
                chars_to_send = chars_limit_timeslot - chars_sent;
            }

            /* Handle wrap-around case for bulk transfer
             * Send first part to end of buffer
             * Send remaining part from start of buffer */
            if (m_buffer_tail + chars_to_send > CONFIG_LOG_BUFFER_SIZE) {
                size_t first_part = CONFIG_LOG_BUFFER_SIZE - m_buffer_tail;
                m_uart_library->write(&m_buffer[m_buffer_tail], first_part);
                size_t second_part = chars_to_send - first_part;
                m_uart_library->write(m_buffer, second_part);
                m_buffer_tail = second_part;
            }

            /* Simple case: no wrap around needed
             * Send data to buffer */
            else {
                m_uart_library->write(&m_buffer[m_buffer_tail], chars_to_send);
                m_buffer_tail = (m_buffer_tail + chars_to_send) % CONFIG_LOG_BUFFER_SIZE;
            }

            /* Update counters */
            m_buffer_count -= chars_to_send;
            chars_sent += chars_to_send;
        }

        /* Flush UART buffer to ensure data is sent */
        m_uart_library->flush();
    }

    /* Return success */
    return 0;
}
