/*
 * clock.h
 * Public interface for the simple clock synchronization primitive.
 *
 * This header exposes a small API that lets a clock producer emit ticks
 * (callers call `clock_pulse()`) and multiple consumers (timers) wait
 * for the next tick using `clock_wait_tick()`.
 */

#ifndef CHURROS_CLOCK_H
#define CHURROS_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the clock subsystem. Safe to call multiple times. */
void clock_init(void);

/* Destroy clock subsystem resources. Call at program termination if desired. */
void clock_destroy(void);

/* Emit a clock pulse (increments the internal tick counter and notifies waiters). */
void clock_pulse(void);

/*
 * Block until the internal tick counter is strictly greater than *last.
 * On return *last is updated to the new tick and the function returns the
 * new tick value. Use this from consumer threads to wait for the next tick.
 */
unsigned long clock_wait_tick(unsigned long *last);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_CLOCK_H */
