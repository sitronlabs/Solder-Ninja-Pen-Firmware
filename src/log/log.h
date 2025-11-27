/**
 * @file log.h
 * @brief Logging implementation for firmware debugging and diagnostics
 *
 * This module provides a comprehensive logging system with the following features:
 * - Multi-level logging (TRACE, DEBUG, INFO, WARNING, ERROR)
 * - Ring buffer for efficient log storage and overflow handling
 * - Automatic timestamp and source location tracking
 * - Non-blocking UART output with configurable baudrate
 * - Convenient macro-based logging interface
 * - Thread-safe operation for embedded systems
 *
 * The logging system uses a rolling buffer to store log messages and flushes
 * them to UART in a non-blocking manner via the log_task() function.
 */

#ifndef LOG_H
#define LOG_H

/* Arduino headers */
#include <Arduino.h>

/* C/C++ headers */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * @brief Log level enumeration for categorizing log messages
 *
 * Log levels are ordered from most verbose (TRACE) to least verbose (ERROR).
 *
 * @note The enum values are intentionally sequential to allow for easy level comparison and filtering operations.
 */
enum log_level {
    LOG_LEVEL_TRACE,    //!< Most verbose level - detailed execution flow, variable values
    LOG_LEVEL_DEBUG,    //!< Debug information - function entry/exit, state changes
    LOG_LEVEL_INFO,     //!< General information - normal operation status
    LOG_LEVEL_WARNING,  //!< Warning conditions - potential issues that don't stop execution
    LOG_LEVEL_ERROR,    //!< Error conditions - serious problems that may affect functionality
};

/**
 * @brief Initialize the logging system
 *
 * Sets up the UART peripheral for log output and initializes the internal
 * ring buffer. Must be called before any logging functions are used.
 *
 * @return 0 on success, negative error code on failure
 */
int log_setup(void);

/**
 * @brief Write a log message to the buffer
 *
 * Formats and stores a log message in the internal ring buffer. The message
 * includes timestamp, log level, source file/line, and user-provided content.
 *
 * @param level Severity level of the log message
 * @param file Source file name (typically __FILE__)
 * @param line Source line number (typically __LINE__)
 * @param format printf-style format string
 * @param ... Variable arguments for format string
 *
 * @return 0 on success, negative error code on failure
 *
 * @note Messages are formatted as: [timestamp] [level] [file:line] message
 */
int log_write(const enum log_level level, const char* const file, const unsigned int line, const char* const format, ...);

/**
 * @name Logging Convenience Macros
 * @brief Convenience macros for each log level that automatically include __FILE__ and __LINE__
 *
 * These macros simplify logging by automatically providing the source file and line number.
 * They all use the same printf-style formatting as the underlying log_write() function.
 *
 * @{
 */

/**
 * @brief Convenience macro for TRACE level logging
 */
#define log_t(...) log_write(LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Convenience macro for DEBUG level logging
 */
#define log_d(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Convenience macro for INFO level logging
 */
#define log_i(...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Convenience macro for WARNING level logging
 */
#define log_w(...) log_write(LOG_LEVEL_WARNING, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Convenience macro for ERROR level logging
 */
#define log_e(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

/** @} */

/**
 * @brief Process and flush log buffer to UART
 *
 * Transfers buffered log messages to the UART output in a non-blocking manner.
 * Respects time limits to prevent blocking the main application loop.
 *
 * @return 0 on success, negative error code on failure
 *
 * @note Should be called regularly from the main loop
 * @note Function is non-blocking and respects CONFIG_LOG_FLUSH_TIMEOUT_MS
 * @note Automatically reports buffer overflow statistics when buffer is empty
 */
int log_task(void);

#endif
