/*
 * logging.h
 * Advanced logging system with levels, colors, and filtering
 */

#ifndef CHURROS_LOGGING_H
#define CHURROS_LOGGING_H

#include "clock.h"
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * LOG LEVELS
 * ============================================ */
typedef enum {
    LOG_LEVEL_DEBUG = 0,    /* Detailed debug information */
    LOG_LEVEL_INFO = 1,     /* General information */
    LOG_LEVEL_NOTICE = 2,   /* Important events */
    LOG_LEVEL_WARNING = 3,  /* Warning conditions */
    LOG_LEVEL_ERROR = 4,    /* Error conditions */
    LOG_LEVEL_CRITICAL = 5, /* Critical conditions */
    LOG_LEVEL_NONE = 6      /* Disable all logging */
} LogLevel;

/* ============================================
 * LOG COMPONENTS
 * ============================================ */
typedef enum {
    LOG_COMPONENT_CLOCK,
    LOG_COMPONENT_TIMER,
    LOG_COMPONENT_SCHEDULER,
    LOG_COMPONENT_LOADER,
    LOG_COMPONENT_MACHINE,
    LOG_COMPONENT_MMU,
    LOG_COMPONENT_MEMORY,
    LOG_COMPONENT_INSTRUCTION,
    LOG_COMPONENT_KERNEL,
    LOG_COMPONENT_PROCESS,
    LOG_COMPONENT_ALL
} LogComponent;

/* ============================================
 * ANSI COLOR CODES
 * ============================================ */
#define COLOR_RESET     "\x1b[0m"
#define COLOR_BOLD      "\x1b[1m"
#define COLOR_DIM       "\x1b[2m"

/* Foreground colors */
#define COLOR_BLACK     "\x1b[30m"
#define COLOR_RED       "\x1b[31m"
#define COLOR_GREEN     "\x1b[32m"
#define COLOR_YELLOW    "\x1b[33m"
#define COLOR_BLUE      "\x1b[34m"
#define COLOR_MAGENTA   "\x1b[35m"
#define COLOR_CYAN      "\x1b[36m"
#define COLOR_WHITE     "\x1b[37m"

/* Bright colors */
#define COLOR_BRIGHT_BLACK   "\x1b[90m"
#define COLOR_BRIGHT_RED     "\x1b[91m"
#define COLOR_BRIGHT_GREEN   "\x1b[92m"
#define COLOR_BRIGHT_YELLOW  "\x1b[93m"
#define COLOR_BRIGHT_BLUE    "\x1b[94m"
#define COLOR_BRIGHT_MAGENTA "\x1b[95m"
#define COLOR_BRIGHT_CYAN    "\x1b[96m"
#define COLOR_BRIGHT_WHITE   "\x1b[97m"

/* Background colors */
#define BG_RED      "\x1b[41m"
#define BG_GREEN    "\x1b[42m"
#define BG_YELLOW   "\x1b[43m"
#define BG_BLUE     "\x1b[44m"

/* ============================================
 * CONFIGURATION
 * ============================================ */

/* Initialize logging system */
void log_init(void);

/* Set global log level (only messages >= this level will be shown) */
void log_set_level(LogLevel level);

/* Get current log level */
LogLevel log_get_level(void);

/* Enable/disable colors */
void log_set_colors_enabled(int enabled);

/* Enable/disable showing CPU/Core/Thread info */
void log_set_location_enabled(int enabled);

/* Enable/disable timestamps */
void log_set_timestamps_enabled(int enabled);

/* Enable/disable component filtering */
void log_set_component_filter(LogComponent component, int enabled);

/* ============================================
 * CORE LOGGING FUNCTIONS
 * ============================================ */

/* Main logging function */
void log_message(LogLevel level, LogComponent component, 
                 const char* fmt, ...) __attribute__((format(printf, 3, 4)));

/* Logging with CPU/Core/Thread location */
void log_message_at(LogLevel level, LogComponent component,
                    uint32_t cpu, uint32_t core, uint32_t thread,
                    const char* fmt, ...) __attribute__((format(printf, 6, 7)));

/* ============================================
 * CONVENIENCE MACROS
 * ============================================ */

/* Basic logging macros */
#define LOG_DEBUG(component, fmt, ...)    log_message(LOG_LEVEL_DEBUG, component, fmt, ##__VA_ARGS__)
#define LOG_INFO(component, fmt, ...)     log_message(LOG_LEVEL_INFO, component, fmt, ##__VA_ARGS__)
#define LOG_NOTICE(component, fmt, ...)   log_message(LOG_LEVEL_NOTICE, component, fmt, ##__VA_ARGS__)
#define LOG_WARN(component, fmt, ...)     log_message(LOG_LEVEL_WARNING, component, fmt, ##__VA_ARGS__)
#define LOG_ERROR(component, fmt, ...)    log_message(LOG_LEVEL_ERROR, component, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(component, fmt, ...) log_message(LOG_LEVEL_CRITICAL, component, fmt, ##__VA_ARGS__)

/* Location-aware logging macros */
#define LOG_AT_DEBUG(component, cpu, core, thread, fmt, ...) \
    log_message_at(LOG_LEVEL_DEBUG, component, cpu, core, thread, fmt, ##__VA_ARGS__)
#define LOG_AT_INFO(component, cpu, core, thread, fmt, ...) \
    log_message_at(LOG_LEVEL_INFO, component, cpu, core, thread, fmt, ##__VA_ARGS__)
#define LOG_AT_NOTICE(component, cpu, core, thread, fmt, ...) \
    log_message_at(LOG_LEVEL_NOTICE, component, cpu, core, thread, fmt, ##__VA_ARGS__)
#define LOG_AT_WARN(component, cpu, core, thread, fmt, ...) \
    log_message_at(LOG_LEVEL_WARNING, component, cpu, core, thread, fmt, ##__VA_ARGS__)
#define LOG_AT_ERROR(component, cpu, core, thread, fmt, ...) \
    log_message_at(LOG_LEVEL_ERROR, component, cpu, core, thread, fmt, ##__VA_ARGS__)

/* Special macros for common operations */
#define LOG_TICK(tick) \
    log_message(LOG_LEVEL_DEBUG, LOG_COMPONENT_CLOCK, "Tick %lu", tick)

#define LOG_SEPARATOR() \
    log_message(LOG_LEVEL_INFO, LOG_COMPONENT_ALL, "═══════════════════════════════════════")

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_LOGGING_H */
