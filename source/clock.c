/*
 * clock.c
 * Implementation of the clock synchronization primitives.
 * The demo/main was intentionally removed to provide a reusable library.
 */

#include "../include/clock.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>

/* Internal state */
static pthread_mutex_t clock_mutex;
static pthread_cond_t clock_cond;
static unsigned long clock_tick = 0;
static pthread_once_t clock_init_once = PTHREAD_ONCE_INIT;
static int clock_initialized = 0;

/* Initialize internal synchronization primitives once. */
static void clock_init_func(void)
{
    (void)pthread_mutex_init(&clock_mutex, NULL);
    (void)pthread_cond_init(&clock_cond, NULL);
    clock_tick = 0;
    clock_initialized = 1;
}

void clock_init(void)
{
    pthread_once(&clock_init_once, clock_init_func);
}

void clock_destroy(void)
{
    if (!clock_initialized)
        return;
    pthread_mutex_destroy(&clock_mutex);
    pthread_cond_destroy(&clock_cond);
    clock_initialized = 0;
}

void clock_pulse(void)
{
    /* Ensure initialized */
    clock_init();

    pthread_mutex_lock(&clock_mutex);
    clock_tick++;
    pthread_cond_broadcast(&clock_cond);
    pthread_mutex_unlock(&clock_mutex);
}

unsigned long clock_wait_tick(unsigned long *last)
{
    if (!last)
        return 0; /* guard: null pointer */

    clock_init();

    pthread_mutex_lock(&clock_mutex);
    while (clock_tick <= *last)
    {
        pthread_cond_wait(&clock_cond, &clock_mutex);
    }
    *last = clock_tick;
    unsigned long result = clock_tick;
    pthread_mutex_unlock(&clock_mutex);
    return result;
}
