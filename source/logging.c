#include "../include/logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void logging_log(const char* color, const char* tag, const char* fmt, ...)
{
    char buf[1024];
    unsigned long tick = clock_get_tick();
    va_list ap;
    
    va_start(ap, fmt);
    int n = snprintf(buf, sizeof(buf), "%s[%s] (tick %lu) ", 
                     color ? color : "", tag ? tag : "", tick);
    if (n > 0 && n < (int)sizeof(buf)) {
        n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    }
    va_end(ap);
    
    if (n > 0 && n < (int)sizeof(buf) - 1) {
        n += snprintf(buf + n, sizeof(buf) - n, "%s\n", COLOR_RESET);
    }
    
    if (n > 0) {
        size_t len = (n < (int)sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
        pthread_mutex_lock(&log_mutex);
        write(STDOUT_FILENO, buf, len);
        pthread_mutex_unlock(&log_mutex);
    }
}

void logging_tick(const char* tag, unsigned long tick)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "[%s] Tick %lu\n", tag, tick);
    
    if (n > 0) {
        size_t len = (n < (int)sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
        pthread_mutex_lock(&log_mutex);
        write(STDOUT_FILENO, buf, len);
        pthread_mutex_unlock(&log_mutex);
    }
}
