#ifndef LOG_H
#define LOG_H

/* Arduino libraries */
#include <Arduino.h>

/* C/C++ libraries */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Logging */
enum log_level {
    LOG_LEVEL_TRACE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
};

/* Setup */
int log_setup(void);

/* Logging */
int log_write(const enum log_level level, const char* const file, const unsigned int line, const char* const format, ...);
#define log_t(...) log_write(LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define log_d(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_i(...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_w(...) log_write(LOG_LEVEL_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define log_e(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

/* Task */
int log_task(void);

#endif
