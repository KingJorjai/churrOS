/*
 * kernel.h
 * Núcleo principal del simulador - Coordina todos los componentes
 */

#ifndef CHURROS_KERNEL_H
#define CHURROS_KERNEL_H

#include "clock.h"
#include "timer.h"
#include "machine.h"
#include "process_queue.h"
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Algoritmos de scheduling disponibles */
typedef enum {
    SCHEDULER_ROUND_ROBIN,      /* Round Robin con quantum fijo */
    SCHEDULER_FIFO,             /* First In First Out (sin preemption por tiempo) */
    SCHEDULER_CHOCOLATE_CALIENTE /* Quantum adaptativo basado en temperatura */
} SchedulerAlgorithm;

/* Configuración del sistema */
typedef struct {
    uint32_t num_cpus;
    uint32_t num_cores_per_cpu;
    uint32_t num_hw_threads_per_core;
    uint32_t timer_period;           /* Periodo del timer en ticks */
    uint32_t process_gen_period;     /* Periodo de generación de procesos en ticks */
    uint32_t clock_speed_ms;         /* Velocidad del reloj en milisegundos */
    uint32_t simulation_duration;    /* Duración de la simulación en ticks (0 = infinito) */
    SchedulerAlgorithm scheduler_algorithm;  /* Algoritmo de scheduling a usar */
} KernelConfig;

/* Estado del kernel */
typedef struct {
    KernelConfig config;
    Machine* machine;
    ProcessQueue* process_queue;
    /* Timers separados para cada componente que lo necesite */
    Timer* sched_timer;       /* Timer para el Scheduler */
    Timer* procgen_timer;     /* Timer para el ProcessGenerator */
    
    pthread_t clock_thread;
    pthread_t sched_timer_thread;
    pthread_t procgen_timer_thread;
    pthread_t scheduler_thread;
    pthread_t process_gen_thread;
    
    volatile int running;
    pthread_mutex_t running_mutex;
    
    uint32_t next_pid;
    pthread_mutex_t pid_mutex;
} Kernel;

/* Crear e inicializar el kernel con la configuración dada */
Kernel* kernel_create(KernelConfig* config);

/* Destruir el kernel y liberar todos los recursos */
void kernel_destroy(Kernel* kernel);

/* Iniciar el kernel (lanzar todos los hilos) */
int kernel_start(Kernel* kernel);

/* Detener el kernel */
void kernel_stop(Kernel* kernel);

/* Obtener el siguiente PID disponible */
uint32_t kernel_get_next_pid(Kernel* kernel);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_KERNEL_H */
