/*
 * timer.c
 * Implementación del temporizador
 */

#include "../include/timer.h"
#include <stdlib.h>
#include <stdio.h>

Timer* churros_timer_create(uint32_t period)
{
    if (period == 0)
        return NULL;
    
    Timer* timer = (Timer*)malloc(sizeof(Timer));
    if (!timer)
        return NULL;
    
    timer->period = period;
    timer->tick_count = 0;
    timer->interrupts_generated = 0;
    timer->interrupt_pending = 0;
    
    pthread_mutex_init(&timer->mutex, NULL);
    pthread_cond_init(&timer->interrupt_cond, NULL);
    
    return timer;
}

void churros_timer_destroy(Timer* timer)
{
    if (!timer)
        return;
    
    pthread_mutex_destroy(&timer->mutex);
    pthread_cond_destroy(&timer->interrupt_cond);
    free(timer);
}

void churros_timer_tick(Timer* timer)
{
    if (!timer)
        return;
    
    pthread_mutex_lock(&timer->mutex);
    
    if (++timer->tick_count >= timer->period) {
        timer->tick_count = 0;
        timer->interrupt_pending = 1;
        timer->interrupts_generated++;
        pthread_cond_broadcast(&timer->interrupt_cond);
    }
    
    pthread_mutex_unlock(&timer->mutex);
}

void churros_timer_wake(Timer* timer)
{
    if (!timer)
        return;

    pthread_mutex_lock(&timer->mutex);
    timer->interrupt_pending = 1;
    pthread_cond_broadcast(&timer->interrupt_cond);
    pthread_mutex_unlock(&timer->mutex);
}

void churros_timer_wait_interrupt(Timer* timer)
{
    if (!timer)
        return;
    
    pthread_mutex_lock(&timer->mutex);
    while (!timer->interrupt_pending) {
        pthread_cond_wait(&timer->interrupt_cond, &timer->mutex);
    }
    timer->interrupt_pending = 0;
    pthread_mutex_unlock(&timer->mutex);
}

int churros_timer_check_interrupt(Timer* timer)
{
    if (!timer)
        return 0;
    
    pthread_mutex_lock(&timer->mutex);
    int pending = timer->interrupt_pending;
    pthread_mutex_unlock(&timer->mutex);
    
    return pending;
}

uint32_t churros_timer_get_generated(Timer* timer)
{
    if (!timer)
        return 0;
        
    pthread_mutex_lock(&timer->mutex);
    uint32_t count = timer->interrupts_generated;
    pthread_mutex_unlock(&timer->mutex);
    return count;
}
