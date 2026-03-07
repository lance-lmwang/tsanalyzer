#include "tsa_log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static tsa_log_level_t g_log_level = TSA_LOG_INFO;
static bool g_log_json = false;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

const char* level_strs[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};

void tsa_log_set_level(tsa_log_level_t level) {
    g_log_level = level;
}

void tsa_log_set_json(bool enabled) {
    g_log_json = enabled;
}

void tsa_log_impl(tsa_log_level_t level, const char* file, int line, const char* tag, const char* fmt, ...) {
    if (level < g_log_level) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    va_list args;
    va_start(args, fmt);

    pthread_mutex_lock(&g_log_lock);
    if (g_log_json) {
        printf("{\"time\":\"%s\",\"level\":\"%s\",\"tag\":\"%s\",\"file\":\"%s\",\"line\":%d,\"msg\":\"", time_str,
               level_strs[level], tag ? tag : "sys", file, line);
        vprintf(fmt, args);
        printf("\"}\n");
    } else {
        printf("[%s][%s][%s] ", time_str, level_strs[level], tag ? tag : "sys");
        vprintf(fmt, args);
        printf("\n");
    }
    pthread_mutex_unlock(&g_log_lock);

    va_end(args);
}
