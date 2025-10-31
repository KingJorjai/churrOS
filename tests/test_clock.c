/*
 * test_clock.c
 * Tests de concurrencia y sincronización para el clock de churrOS
 * Prueba un único clock con múltiples workers
 * 
 * Tests generados por Claude Sonnet 4.5 bajo supervisión de Jorge Arévalo
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include "../include/clock.h"

#define NUM_WORKERS 8
#define TICKS_PER_WORKER 20
#define TICK_INTERVAL_US 50000  /* 50ms */

/* Variables globales para tracking de resultados */
static int worker_completed[16] = {0};
static pthread_mutex_t result_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Pulser: genera ticks periódicos */
void *pulser_thread(void *arg)
{
    int num_ticks = *(int*)arg;
    
    for (int i = 0; i < num_ticks; ++i) {
        usleep(TICK_INTERVAL_US);
        clock_pulse();
    }
    
    return NULL;
}

/* Worker: espera ticks y verifica sincronización */
void *worker_thread(void *arg)
{
    int id = (int)(intptr_t)arg;
    unsigned long last = 0;
    unsigned long prev_tick = 0;
    
    for (int i = 0; i < TICKS_PER_WORKER; ++i) {
        unsigned long tick = clock_wait_tick(&last);
        
        /* Verificar que el tick incrementa monotónicamente */
        assert(tick > prev_tick);
        prev_tick = tick;
        
        /* Simular trabajo */
        usleep(1000); /* 1ms de trabajo */
    }
    
    pthread_mutex_lock(&result_mutex);
    worker_completed[id] = 1;
    pthread_mutex_unlock(&result_mutex);
    
    printf("Worker %d: completó %d ticks correctamente\n", id, TICKS_PER_WORKER);
    
    return NULL;
}

/* ========== TESTS ========== */

void test_basic_synchronization(void)
{
    printf("\n=== TEST 1: Sincronización Básica (8 Workers) ===\n");
    
    pthread_t pulser;
    pthread_t workers[NUM_WORKERS];
    int num_ticks = TICKS_PER_WORKER + 5;
    
    /* Reset */
    memset(worker_completed, 0, sizeof(worker_completed));
    clock_init();
    
    /* Crear threads */
    pthread_create(&pulser, NULL, pulser_thread, &num_ticks);
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_create(&workers[i], NULL, worker_thread, (void*)(intptr_t)i);
    }
    
    /* Esperar */
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(workers[i], NULL);
    }
    pthread_join(pulser, NULL);
    
    /* Verificar que todos completaron */
    for (int i = 0; i < NUM_WORKERS; i++) {
        assert(worker_completed[i] == 1);
    }
    
    printf("✓ Todos los %d workers completaron correctamente\n", NUM_WORKERS);
    printf("✓ Sincronización verificada\n");
}

void test_stress_synchronization(void)
{
    printf("\n=== TEST 2: Stress Test (16 Workers) ===\n");
    
    #define STRESS_WORKERS 16
    pthread_t pulser;
    pthread_t workers[STRESS_WORKERS];
    int num_ticks = TICKS_PER_WORKER + 10;
    
    /* Reset */
    memset(worker_completed, 0, sizeof(worker_completed));
    
    /* Crear threads */
    pthread_create(&pulser, NULL, pulser_thread, &num_ticks);
    for (int i = 0; i < STRESS_WORKERS; i++) {
        pthread_create(&workers[i], NULL, worker_thread, (void*)(intptr_t)i);
    }
    
    /* Esperar */
    for (int i = 0; i < STRESS_WORKERS; i++) {
        pthread_join(workers[i], NULL);
    }
    pthread_join(pulser, NULL);
    
    /* Verificar que todos completaron */
    for (int i = 0; i < STRESS_WORKERS; i++) {
        assert(worker_completed[i] == 1);
    }
    
    printf("✓ Stress test con %d workers completado\n", STRESS_WORKERS);
}

void test_clock_monotonicity(void)
{
    printf("\n=== TEST 3: Monotonía del Clock ===\n");
    
    unsigned long last = 0;
    pthread_t pulser;
    int num_ticks = 35;
    
    pthread_create(&pulser, NULL, pulser_thread, &num_ticks);
    
    /* Verificar que los ticks son estrictamente crecientes */
    unsigned long prev = 0;
    for (int i = 0; i < 30; i++) {
        unsigned long tick = clock_wait_tick(&last);
        assert(tick > 0);
        assert(tick == last);
        assert(tick > prev);
        prev = tick;
        
        if (i % 10 == 0) {
            printf("  Tick %d: %lu\n", i, tick);
        }
    }
    
    pthread_join(pulser, NULL);
    
    printf("✓ Monotonía del clock verificada (30 ticks)\n");
}

int main(void)
{
    printf("========================================\n");
    printf("   churrOS Clock - Test Suite\n");
    printf("========================================\n");
    
    /* Ejecutar tests */
    test_basic_synchronization();
    test_stress_synchronization();
    test_clock_monotonicity();
    
    /* Cleanup final */
    clock_destroy();
    pthread_mutex_destroy(&result_mutex);
    
    printf("\n========================================\n");
    printf("   ✓ TODOS LOS TESTS PASARON\n");
    printf("========================================\n");
    
    return 0;
}
