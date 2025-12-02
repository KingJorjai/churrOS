/*
 * timer.h
 * Temporizador que genera interrupciones periódicas
 */

#ifndef CHURROS_TIMER_H
#define CHURROS_TIMER_H

#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t period;           /* Periodo del timer en ticks */
    uint32_t tick_count;       /* Contador de ticks desde la última interrupción */
    uint32_t interrupts_generated; /* Contador total de interrupciones generadas */
    pthread_mutex_t mutex;
    pthread_cond_t interrupt_cond;
    int interrupt_pending;     /* Flag de interrupción pendiente */
} Timer;

/* Crear e inicializar un timer */
Timer* churros_timer_create(uint32_t period);

/* Destruir el timer */
void churros_timer_destroy(Timer* timer);

/* Notificar un tick al timer (llamado por el Clock) */
void churros_timer_tick(Timer* timer);

/* Esperar a la siguiente interrupción del timer */
void churros_timer_wait_interrupt(Timer* timer);

/* Verificar si hay una interrupción pendiente sin bloquear */
int churros_timer_check_interrupt(Timer* timer);

/* Obtener el número total de interrupciones generadas (seguro para hilos) */
uint32_t churros_timer_get_generated(Timer* timer);

/* Forzar el estado de interrupción y despertar a los hilos que esperan
 * Útil para detener el kernel y desbloquear hilos que esperan por una interrupción
 */
void churros_timer_wake(Timer* timer);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_TIMER_H */
