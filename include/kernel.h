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
#include "memory.h"
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
    uint32_t rr_quantum;             /* Quantum para Round Robin en ticks (solo usado en RR) */
    uint32_t process_gen_period;     /* Periodo de generación de procesos en ticks */
    uint32_t clock_speed_ms;         /* Velocidad del reloj en milisegundos */
    uint32_t simulation_duration;    /* Duración de la simulación en ticks (0 = infinito) */
    SchedulerAlgorithm scheduler_algorithm;  /* Algoritmo de scheduling a usar */
} KernelConfig;

/* Tipos de eventos que requieren atención del scheduler */
typedef enum {
    SCHED_EVENT_NONE = 0,
    SCHED_EVENT_PROCESS_CREATED = (1 << 0),    /* Nuevo proceso creado */
    SCHED_EVENT_PROCESS_TERMINATED = (1 << 1), /* Proceso terminado */
    SCHED_EVENT_QUANTUM_EXPIRED = (1 << 2)     /* Quantum expirado (RR/CH) */
} SchedulerEventFlags;

/* Estado del kernel */
typedef struct Kernel Kernel; /* Forward declaration para evitar dependencias circulares */

struct Kernel {
    KernelConfig config;
    Machine* machine;
    ProcessQueue* process_queue;
    Timer* procgen_timer;     /* Timer para el ProcessGenerator */
    PhysicalMemory* physical_memory;  /* Physical Memory (Parte 3) */
    
    /* Sistema de señalización para eventos del scheduler */
    pthread_cond_t scheduler_cond;    /* Condition variable para despertar al scheduler */
    pthread_mutex_t scheduler_mutex;  /* Mutex para proteger pending_events */
    uint32_t pending_events;          /* Flags de eventos pendientes (SchedulerEventFlags) */
    
    pthread_t clock_thread;
    pthread_t procgen_timer_thread;
    pthread_t scheduler_thread;
    pthread_t process_gen_thread;
    
    volatile int running;
    pthread_mutex_t running_mutex;
    
    uint32_t next_pid;
    pthread_mutex_t pid_mutex;
};

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

/* Señalizar un evento al scheduler */
void kernel_signal_scheduler(Kernel* kernel, SchedulerEventFlags event);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_KERNEL_H */
