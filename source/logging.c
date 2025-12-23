/*
 * logging.c
 * Advanced logging system implementation
 */

#include "../include/logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/* ============================================
 * GLOBAL STATE
 * ============================================ */

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct {
    LogLevel level;
    int colors_enabled;
    int location_enabled;
    int timestamps_enabled;
    int component_filters[LOG_COMPONENT_ALL + 1];
    int initialized;
} log_config = {
    .level = LOG_LEVEL_INFO,  /* Default: show INFO and above */
    .colors_enabled = 1,
    .location_enabled = 1,
    .timestamps_enabled = 0,  /* Disabled by default to reduce clutter */
    .initialized = 0,
};

/* ============================================
 * COMPONENT NAMES AND COLORS
 * ============================================ */

static const struct {
    const char* name;
    const char* color;
} component_info[] = {
    [LOG_COMPONENT_CLOCK]       = {"CLK", COLOR_BRIGHT_BLACK},
    [LOG_COMPONENT_TIMER]       = {"TMR", COLOR_BRIGHT_BLUE},
    [LOG_COMPONENT_SCHEDULER]   = {"SCH", COLOR_BRIGHT_GREEN},
    [LOG_COMPONENT_LOADER]      = {"LDR", COLOR_BRIGHT_CYAN},
    [LOG_COMPONENT_MACHINE]     = {"MCH", COLOR_BRIGHT_YELLOW},
    [LOG_COMPONENT_MMU]         = {"MMU", COLOR_MAGENTA},
    [LOG_COMPONENT_MEMORY]      = {"MEM", COLOR_CYAN},
    [LOG_COMPONENT_INSTRUCTION] = {"EXE", COLOR_GREEN},
    [LOG_COMPONENT_KERNEL]      = {"KRN", COLOR_YELLOW},
    [LOG_COMPONENT_PROCESS]     = {"PRC", COLOR_BLUE},
    [LOG_COMPONENT_ALL]         = {"ALL", COLOR_WHITE},
};

/* ============================================
 * LEVEL NAMES AND COLORS
 * ============================================ */

static const struct {
    const char* name;
    const char* color;
    const char* symbol;
} level_info[] = {
    [LOG_LEVEL_DEBUG]    = {"DEBUG", COLOR_DIM,           "·"},
    [LOG_LEVEL_INFO]     = {"INFO ", COLOR_BRIGHT_WHITE,  "●"},
    [LOG_LEVEL_NOTICE]   = {"NOTE ", COLOR_BRIGHT_CYAN,   "◆"},
    [LOG_LEVEL_WARNING]  = {"WARN ", COLOR_BRIGHT_YELLOW, "▲"},
    [LOG_LEVEL_ERROR]    = {"ERROR", COLOR_BRIGHT_RED,    "✖"},
    [LOG_LEVEL_CRITICAL] = {"CRIT ", COLOR_BOLD BG_RED,   "█"},
};

/* ============================================
 * CONFIGURATION FUNCTIONS
 * ============================================ */

void log_init(void)
{
    /* Enable all components by default */
    pthread_mutex_lock(&log_mutex);
    if (!log_config.initialized) {
        for (int i = 0; i <= LOG_COMPONENT_ALL; i++) {
            log_config.component_filters[i] = 1;
        }
        log_config.initialized = 1;
    }
    pthread_mutex_unlock(&log_mutex);
}

void log_set_level(LogLevel level)
{
    pthread_mutex_lock(&log_mutex);
    log_config.level = level;
    pthread_mutex_unlock(&log_mutex);
}

LogLevel log_get_level(void)
{
    return log_config.level;
}

void log_set_colors_enabled(int enabled)
{
    pthread_mutex_lock(&log_mutex);
    log_config.colors_enabled = enabled;
    pthread_mutex_unlock(&log_mutex);
}

void log_set_location_enabled(int enabled)
{
    pthread_mutex_lock(&log_mutex);
    log_config.location_enabled = enabled;
    pthread_mutex_unlock(&log_mutex);
}

void log_set_timestamps_enabled(int enabled)
{
    pthread_mutex_lock(&log_mutex);
    log_config.timestamps_enabled = enabled;
    pthread_mutex_unlock(&log_mutex);
}

void log_set_component_filter(LogComponent component, int enabled)
{
    if (component >= 0 && component <= LOG_COMPONENT_ALL) {
        pthread_mutex_lock(&log_mutex);
        log_config.component_filters[component] = enabled;
        pthread_mutex_unlock(&log_mutex);
    }
}

/* ============================================
 * CORE LOGGING IMPLEMENTATION
 * ============================================ */

static void log_internal(LogLevel level, LogComponent component,
                        uint32_t cpu, uint32_t core, uint32_t thread,
                        int has_location, const char* fmt, va_list args)
{
    /* Auto-initialize on first use */
    if (!log_config.initialized) {
        log_init();
    }
    
    /* Check if this message should be logged */
    if (level < log_config.level || level >= LOG_LEVEL_NONE) {
        return;
    }
    
    if (component >= 0 && component <= LOG_COMPONENT_ALL) {
        if (!log_config.component_filters[component]) {
            return;
        }
    }
    
    char buffer[2048];
    int pos = 0;
    
    /* Build the log message */
    
    /* 1. Level indicator with color */
    if (log_config.colors_enabled) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s%s%s ",
                       level_info[level].color,
                       level_info[level].symbol,
                       COLOR_RESET);
    } else {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[%s] ",
                       level_info[level].name);
    }
    
    /* 2. Timestamp (tick count) */
    unsigned long tick = clock_get_tick();
    if (log_config.colors_enabled) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                       "%s%5lu%s ",
                       COLOR_DIM, tick, COLOR_RESET);
    } else {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[%5lu] ", tick);
    }
    
    /* 3. Component tag with color */
    if (component >= 0 && component <= LOG_COMPONENT_ALL) {
        if (log_config.colors_enabled) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s%-3s%s ",
                           component_info[component].color,
                           component_info[component].name,
                           COLOR_RESET);
        } else {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, "[%-3s] ",
                           component_info[component].name);
        }
    }
    
    /* 4. Location (CPU:Core:Thread) if available */
    if (has_location && log_config.location_enabled) {
        if (log_config.colors_enabled) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                           "%s(%u:%u:%u)%s ",
                           COLOR_DIM, cpu, core, thread, COLOR_RESET);
        } else {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                           "(%u:%u:%u) ", cpu, core, thread);
        }
    }
    
    /* 5. The actual message */
    if (log_config.colors_enabled) {
        /* Use a lighter color for the message based on level */
        const char* msg_color = COLOR_RESET;
        if (level == LOG_LEVEL_DEBUG) msg_color = COLOR_DIM;
        else if (level == LOG_LEVEL_WARNING) msg_color = COLOR_YELLOW;
        else if (level == LOG_LEVEL_ERROR) msg_color = COLOR_RED;
        else if (level == LOG_LEVEL_CRITICAL) msg_color = COLOR_BRIGHT_RED;
        
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s", msg_color);
    }
    
    pos += vsnprintf(buffer + pos, sizeof(buffer) - pos, fmt, args);
    
    if (log_config.colors_enabled) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%s", COLOR_RESET);
    }
    
    /* 6. Newline */
    pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\n");
    
    /* Write atomically */
    if (pos > 0 && pos < (int)sizeof(buffer)) {
        pthread_mutex_lock(&log_mutex);
        write(STDOUT_FILENO, buffer, pos);
        pthread_mutex_unlock(&log_mutex);
    }
}

void log_message(LogLevel level, LogComponent component, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(level, component, 0, 0, 0, 0, fmt, args);
    va_end(args);
}

void log_message_at(LogLevel level, LogComponent component,
                   uint32_t cpu, uint32_t core, uint32_t thread,
                   const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_internal(level, component, cpu, core, thread, 1, fmt, args);
    va_end(args);
}

/* ============================================
 * LEGACY COMPATIBILITY (deprecated)
 * ============================================ */

void logging_log(const char* color, const char* tag, const char* fmt, ...)
{
    (void)color;  /* Ignore legacy color */
    (void)tag;    /* Ignore legacy tag */
    
    va_list args;
    va_start(args, fmt);
    log_internal(LOG_LEVEL_INFO, LOG_COMPONENT_ALL, 0, 0, 0, 0, fmt, args);
    va_end(args);
}

void logging_tick(const char* tag, unsigned long tick)
{
    (void)tag;
    log_message(LOG_LEVEL_DEBUG, LOG_COMPONENT_CLOCK, "Tick %lu", tick);
}
