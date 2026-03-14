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
    TSA_LOG_REPORT, /* For final analysis results */
    TSA_LOG_QUIET   /* Silence all except REPORT */
} tsa_log_level_t;

/**
 * Global log configuration
 */
void tsa_log_init(const char* file_path);
void tsa_log_destroy(void);
void tsa_log_set_level(tsa_log_level_t level);
void tsa_log_set_json(bool enabled);

/**
 * Direct synchronous output for large blobs (e.g. final JSON reports).
 * Use only when real-time analysis is NOT running.
 */
void tsa_log_result(const char* fmt, ...);

/**
 * Internal logging function - do not call directly.
 * Use the macros below instead.
 */
void tsa_log_impl(tsa_log_level_t level, const char* file, int line, const char* tag, uint64_t context_id, uint32_t pid,
                  const char* fmt, ...);

/* Standard Log Macros */
#define tsa_trace(tag, fmt, ...) tsa_log_impl(TSA_LOG_TRACE, __FILE__, __LINE__, tag, 0, 0, fmt, ##__VA_ARGS__)
#define tsa_debug(tag, fmt, ...) tsa_log_impl(TSA_LOG_DEBUG, __FILE__, __LINE__, tag, 0, 0, fmt, ##__VA_ARGS__)
#define tsa_info(tag, fmt, ...) tsa_log_impl(TSA_LOG_INFO, __FILE__, __LINE__, tag, 0, 0, fmt, ##__VA_ARGS__)
#define tsa_warn(tag, fmt, ...) tsa_log_impl(TSA_LOG_WARN, __FILE__, __LINE__, tag, 0, 0, fmt, ##__VA_ARGS__)
#define tsa_error(tag, fmt, ...) tsa_log_impl(TSA_LOG_ERROR, __FILE__, __LINE__, tag, 0, 0, fmt, ##__VA_ARGS__)
#define tsa_report(tag, fmt, ...) tsa_log_impl(TSA_LOG_REPORT, __FILE__, __LINE__, tag, 0, 0, fmt, ##__VA_ARGS__)

/* Structured Context Log Macros */
#define tsa_info_ctx(tag, ctx, pid, fmt, ...) \
    tsa_log_impl(TSA_LOG_INFO, __FILE__, __LINE__, tag, ctx, pid, fmt, ##__VA_ARGS__)
#define tsa_warn_ctx(tag, ctx, pid, fmt, ...) \
    tsa_log_impl(TSA_LOG_WARN, __FILE__, __LINE__, tag, ctx, pid, fmt, ##__VA_ARGS__)
#define tsa_error_ctx(tag, ctx, pid, fmt, ...) \
    tsa_log_impl(TSA_LOG_ERROR, __FILE__, __LINE__, tag, ctx, pid, fmt, ##__VA_ARGS__)

#endif
