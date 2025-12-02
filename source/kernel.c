/*
 * kernel.c
 * Implementación del núcleo del simulador
 */

#include "../include/kernel.h"
#include "../include/pcb.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "../include/logging.h"

/* Variable global para acceder al kernel desde los hilos */
static Kernel* g_kernel = NULL;

/* Prototipos de funciones auxiliares */
static int kernel_is_running(Kernel* kernel);

/* Prototipos de funciones de los hilos */
static void* clock_thread_func(void* arg);
static void* sched_timer_thread_func(void* arg);
static void* procgen_timer_thread_func(void* arg);
static void* scheduler_thread_func(void* arg);
static void* process_generator_thread_func(void* arg);

Kernel* kernel_create(KernelConfig* config)
{
    if (!config)
        return NULL;
    
    Kernel* kernel = (Kernel*)malloc(sizeof(Kernel));
    if (!kernel)
        return NULL;
    
    /* Copiar configuración */
    kernel->config = *config;
    
    /* Crear la máquina */
    kernel->machine = machine_create(
        config->num_cpus,
        config->num_cores_per_cpu,
        config->num_hw_threads_per_core
    );
    
    if (!kernel->machine) {
        free(kernel);
        return NULL;
    }
    
    /* Crear la cola de procesos */
    kernel->process_queue = (ProcessQueue*)malloc(sizeof(ProcessQueue));
    if (!kernel->process_queue) {
        machine_destroy(kernel->machine);
        free(kernel);
        return NULL;
    }
    process_queue_init(kernel->process_queue);
    
    /* Crear timers separados: uno para el scheduler y otro para el generador de procesos */
    kernel->sched_timer = churros_timer_create(config->timer_period);
    kernel->procgen_timer = churros_timer_create(config->process_gen_period);
    if (!kernel->sched_timer || !kernel->procgen_timer) {
        if (kernel->sched_timer) churros_timer_destroy(kernel->sched_timer);
        if (kernel->procgen_timer) churros_timer_destroy(kernel->procgen_timer);
        process_queue_destroy(kernel->process_queue);
        free(kernel->process_queue);
        machine_destroy(kernel->machine);
        free(kernel);
        return NULL;
    }
    
    /* Inicializar el reloj */
    clock_init();
    
    /* Inicializar mutexes y estado */
    pthread_mutex_init(&kernel->running_mutex, NULL);
    pthread_mutex_init(&kernel->pid_mutex, NULL);
    kernel->running = 0;
    kernel->next_pid = 1;
    
    g_kernel = kernel;
    
    return kernel;
}

void kernel_destroy(Kernel* kernel)
{
    if (!kernel)
        return;
    
    /* Asegurarse de que el kernel está detenido */
    kernel_stop(kernel);
    
    /* Destruir componentes */
    churros_timer_destroy(kernel->sched_timer);
    churros_timer_destroy(kernel->procgen_timer);
    process_queue_destroy(kernel->process_queue);
    free(kernel->process_queue);
    machine_destroy(kernel->machine);
    clock_destroy();
    
    /* Destruir mutexes */
    pthread_mutex_destroy(&kernel->running_mutex);
    pthread_mutex_destroy(&kernel->pid_mutex);
    
    g_kernel = NULL;
    free(kernel);
}

int kernel_start(Kernel* kernel)
{
    if (!kernel)
        return -1;
    
    pthread_mutex_lock(&kernel->running_mutex);
    if (kernel->running) {
        pthread_mutex_unlock(&kernel->running_mutex);
        return -1; /* Ya está ejecutándose */
    }
    kernel->running = 1;
    pthread_mutex_unlock(&kernel->running_mutex);
    
    printf("=== Iniciando Kernel de churrOS ===\n");
    printf("Configuración:\n");
    printf("  CPUs: %u\n", kernel->config.num_cpus);
    printf("  Cores por CPU: %u\n", kernel->config.num_cores_per_cpu);
    printf("  HW Threads por Core: %u\n", kernel->config.num_hw_threads_per_core);
    printf("  Periodo del Timer: %u ticks\n", kernel->config.timer_period);
    printf("  Periodo de generación de procesos: %u ticks\n", kernel->config.process_gen_period);
    printf("  Velocidad del reloj: %u ms\n", kernel->config.clock_speed_ms);
    if (kernel->config.simulation_duration > 0)
        printf("  Duración de la simulación: %u ticks\n", kernel->config.simulation_duration);
    else
        printf("  Duración de la simulación: infinita\n");
    printf("===================================\n\n");
    
    /* Crear e iniciar hilos */
    if (pthread_create(&kernel->scheduler_thread, NULL, scheduler_thread_func, kernel) != 0) {
        fprintf(stderr, "Error al crear el hilo Scheduler\n");
        kernel->running = 0;
        return -1;
    }
    
    if (pthread_create(&kernel->process_gen_thread, NULL, process_generator_thread_func, kernel) != 0) {
        fprintf(stderr, "Error al crear el hilo Process Generator\n");
        kernel->running = 0;
        return -1;
    }
    
    /* Crear hilos monitor de timers (imprimen eventos sin consumir la interrupción) */
    if (pthread_create(&kernel->sched_timer_thread, NULL, sched_timer_thread_func, kernel) != 0) {
        fprintf(stderr, "Error al crear el hilo Timer (scheduler)\n");
        kernel->running = 0;
        return -1;
    }

    if (pthread_create(&kernel->procgen_timer_thread, NULL, procgen_timer_thread_func, kernel) != 0) {
        fprintf(stderr, "Error al crear el hilo Timer (procgen)\n");
        kernel->running = 0;
        return -1;
    }
    
    if (pthread_create(&kernel->clock_thread, NULL, clock_thread_func, kernel) != 0) {
        fprintf(stderr, "Error al crear el hilo Clock\n");
        kernel->running = 0;
        return -1;
    }
    
    return 0;
}

void kernel_stop(Kernel* kernel)
{
    if (!kernel)
        return;

    pthread_mutex_lock(&kernel->running_mutex);
    if (!kernel->running) {
        pthread_mutex_unlock(&kernel->running_mutex);
        return;
    }
    kernel->running = 0;
    pthread_mutex_unlock(&kernel->running_mutex);

    printf("\n=== Deteniendo Kernel ===\n");

    churros_timer_wake(kernel->sched_timer);
    churros_timer_wake(kernel->procgen_timer);

    /* Opcional: forzar cancelación de hilos potencialmente bloqueados */
    pthread_cancel(kernel->clock_thread);
    pthread_cancel(kernel->sched_timer_thread);
    pthread_cancel(kernel->procgen_timer_thread);
    pthread_cancel(kernel->scheduler_thread);
    pthread_cancel(kernel->process_gen_thread);

    pthread_join(kernel->clock_thread, NULL);
    pthread_join(kernel->sched_timer_thread, NULL);
    pthread_join(kernel->procgen_timer_thread, NULL);
    pthread_join(kernel->scheduler_thread, NULL);
    pthread_join(kernel->process_gen_thread, NULL);

    printf("Kernel detenido correctamente.\n");
}

uint32_t kernel_get_next_pid(Kernel* kernel)
{
    if (!kernel)
        return 0;
    
    pthread_mutex_lock(&kernel->pid_mutex);
    uint32_t pid = kernel->next_pid++;
    pthread_mutex_unlock(&kernel->pid_mutex);
    
    return pid;
}

/* Implementación de los hilos */

static void* clock_thread_func(void* arg)
{
    Kernel* kernel = (Kernel*)arg;
    uint32_t tick_count = 0;
    
    printf("[Clock] Iniciado\n");
    
    while (kernel_is_running(kernel)) {
        usleep(kernel->config.clock_speed_ms * 1000);
        
        clock_pulse();
        tick_count++;
        LOG_TICK("Clock", tick_count);
        
        machine_advance_cycle(kernel->machine);
        churros_timer_tick(kernel->sched_timer);
        churros_timer_tick(kernel->procgen_timer);
        
        if (kernel->config.simulation_duration > 0 && 
            tick_count >= kernel->config.simulation_duration) {
            printf("[Clock] Duración de simulación alcanzada (%u ticks)\n", tick_count);
            kernel_stop(kernel);
            break;
        }
    }
    
    printf("[Clock] Terminado (total de ticks: %u)\n", tick_count);
    return NULL;
}

static void* timer_monitor_thread(void* arg, Timer* timer, const char* name, int silent)
{
    Kernel* kernel = (Kernel*)arg;
    unsigned long last_tick = 0;
    uint32_t interrupt_count = 0;

    while (kernel->running) {
        clock_wait_tick(&last_tick);
        
        uint32_t total = churros_timer_get_generated(timer);
        while (total > interrupt_count) {
            interrupt_count++;
            if (!silent) {
                printf("[%s] Activación #%u por interrupción del timer\n", name, interrupt_count);
            }
        }
    }
    return NULL;
}

static void* sched_timer_thread_func(void* arg)
{
    return timer_monitor_thread(arg, ((Kernel*)arg)->sched_timer, "Scheduler", 1);
}

static void* procgen_timer_thread_func(void* arg)
{
    return timer_monitor_thread(arg, ((Kernel*)arg)->procgen_timer, "ProcessGenerator", 0);
}

static int kernel_is_running(Kernel* kernel)
{
    pthread_mutex_lock(&kernel->running_mutex);
    int running = kernel->running;
    pthread_mutex_unlock(&kernel->running_mutex);
    return running;
}

static void* scheduler_thread_func(void* arg)
{
    Kernel* kernel = (Kernel*)arg;
    uint32_t activation_count = 0;
    
    printf("[Scheduler] Iniciado (periodo: %u ticks, %llu ms)\n",
        kernel->config.timer_period,
        (unsigned long long)kernel->config.timer_period * kernel->config.clock_speed_ms);
    
    while (kernel_is_running(kernel)) {
        churros_timer_wait_interrupt(kernel->sched_timer);
        
        if (!kernel_is_running(kernel))
            break;
        
        activation_count++;
        printf("[Scheduler] Activación #%u por interrupción del timer\n", activation_count);
        
        uint32_t queue_size = process_queue_size(kernel->process_queue);
        if (queue_size > 0) {
            printf("[Scheduler] Procesos en cola: %u\n", queue_size);
        }
    }
    
    printf("[Scheduler] Terminado (activaciones totales: %u)\n", activation_count);
    return NULL;
}

static void* process_generator_thread_func(void* arg)
{
    Kernel* kernel = (Kernel*)arg;

    printf("[ProcessGenerator] Iniciado (periodo: %u ticks, %llu ms)\n",
        kernel->config.process_gen_period,
        (unsigned long long)kernel->config.process_gen_period * kernel->config.clock_speed_ms);

    while (kernel_is_running(kernel)) {
        churros_timer_wait_interrupt(kernel->procgen_timer);

        if (!kernel_is_running(kernel))
            break;

        uint32_t pid = kernel_get_next_pid(kernel);
        PCB* pcb = pcb_create(pid);

        if (pcb) {
            process_queue_enqueue(kernel->process_queue, pcb);
            printf("[ProcessGenerator] Nuevo proceso creado: PID=%u\n", pcb->pid);
        } else {
            fprintf(stderr, "[ProcessGenerator] Error al crear el proceso\n");
        }
    }
    
    printf("[ProcessGenerator] Terminado\n");
    return NULL;
}
