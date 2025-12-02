#ifndef CHURROS_LOGGING_H
#define CHURROS_LOGGING_H

#include "clock.h"
#include <stdio.h>

/* ANSI color codes */
#define COLOR_RESET "\x1b[0m"
#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_BLUE "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN "\x1b[36m"
#define COLOR_BOLD "\x1b[1m"

/* Thread-safe logging function. It formats the message (including current
 * clock tick and provided tag), applies the color, and writes the whole
 * line under a mutex to avoid interleaving from multiple threads.
 */
void logging_log(const char* color, const char* tag, const char* fmt, ...);

/* Convenience wrappers */
#define LOG_INFO(tag, fmt, ...) logging_log(COLOR_CYAN, tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) logging_log(COLOR_YELLOW, tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) logging_log(COLOR_RED, tag, fmt, ##__VA_ARGS__)

/* Simple tick log: prints a single unadorned line like
 * [Clock] Tick 42
 * This is useful when you want a concise tick-only entry. */
void logging_tick(const char* tag, unsigned long tick);
#define LOG_TICK(tag, tick) logging_tick(tag, tick)

#endif /* CHURROS_LOGGING_H */
