#ifndef TSA_LOG_H
#define TSA_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    TSA_LOG_TRACE = 0,
    TSA_LOG_DEBUG,
    TSA_LOG_INFO,
    TSA_LOG_WARN,
    TSA_LOG_ERROR,
    TSA_LOG_OFF
} tsa_log_level_t;

/**
 * Global log configuration
 */
void tsa_log_set_level(tsa_log_level_t level);
void tsa_log_set_json(bool enabled);

/**
 * Internal logging function - do not call directly.
 * Use the macros below instead.
 */
void tsa_log_impl(tsa_log_level_t level, const char* file, int line, const char* tag, const char* fmt, ...);

/* Macro definitions for logging */
#define tsa_trace(tag, fmt, ...) tsa_log_impl(TSA_LOG_TRACE, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)
#define tsa_debug(tag, fmt, ...) tsa_log_impl(TSA_LOG_DEBUG, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)
#define tsa_info(tag, fmt, ...) tsa_log_impl(TSA_LOG_INFO, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)
#define tsa_warn(tag, fmt, ...) tsa_log_impl(TSA_LOG_WARN, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)
#define tsa_error(tag, fmt, ...) tsa_log_impl(TSA_LOG_ERROR, __FILE__, __LINE__, tag, fmt, ##__VA_ARGS__)

#endif
