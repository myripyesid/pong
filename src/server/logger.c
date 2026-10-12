#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include "logger.h"

#define TIME_STR_LEN 64

static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_to_string(log_level_t level) {
    switch (level) {
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_DEBUG: return "DEBUG";
        default:        return "UNKNOWN";
    }
}

int logger_init(const char *filename) {
    pthread_mutex_lock(&log_mutex);
    if (log_file != NULL) {
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }

    log_file = fopen(filename, "a");
    if (!log_file) {
        perror("[Logger] Error al abrir archivo de log");
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }

    pthread_mutex_unlock(&log_mutex);
    return 0;
}

void logger_log(log_level_t level, const char *format, ...) {
    pthread_mutex_lock(&log_mutex);

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char time_buf[TIME_STR_LEN];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    printf("[%s] [%s] ", time_buf, level_to_string(level));
    if (log_file) {
        fprintf(log_file, "[%s] [%s] ", time_buf, level_to_string(level));
    }

    va_list args1, args2;
    va_start(args1, format);
    va_copy(args2, args1);

    vprintf(format, args1);
    printf("\n");
    fflush(stdout);

    if (log_file) {
        vfprintf(log_file, format, args2);
        fprintf(log_file, "\n");
        fflush(log_file);
    }

    va_end(args1);
    va_end(args2);

    pthread_mutex_unlock(&log_mutex);
}

void logger_close(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file) {
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&log_mutex);
    pthread_mutex_destroy(&log_mutex);
}
