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
static int shutdown_requested = 0;

void clock_init(void)
{
    pthread_mutex_init(&clock_mutex, NULL);
    pthread_cond_init(&clock_cond, NULL);
    clock_tick = 0;
    shutdown_requested = 0;
}

void clock_destroy(void)
{
    pthread_mutex_destroy(&clock_mutex);
    pthread_cond_destroy(&clock_cond);
}

void clock_pulse(void)
{
    pthread_mutex_lock(&clock_mutex);
    clock_tick++;
    pthread_cond_broadcast(&clock_cond);
    pthread_mutex_unlock(&clock_mutex);
}

unsigned long clock_wait_tick(unsigned long *last)
{
    if (!last)
        return 0;

    pthread_mutex_lock(&clock_mutex);
    while (clock_tick <= *last && !shutdown_requested) {
        pthread_cond_wait(&clock_cond, &clock_mutex);
    }
    *last = clock_tick;
    pthread_mutex_unlock(&clock_mutex);
    return *last;
}

unsigned long clock_get_tick(void)
{
    pthread_mutex_lock(&clock_mutex);
    unsigned long tick = clock_tick;
    pthread_mutex_unlock(&clock_mutex);
    
    return tick;
}
void clock_shutdown(void)
{
    pthread_mutex_lock(&clock_mutex);
    shutdown_requested = 1;
    pthread_cond_broadcast(&clock_cond);
    pthread_mutex_unlock(&clock_mutex);
}