/*
 * kernel.c
 * Implementación del núcleo del simulador
 */

#include "../include/kernel.h"
#include "../include/pcb.h"
#include "../include/scheduler.h"
#include "../include/loader.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "../include/logging.h"

/* Variable global para acceder al kernel desde los hilos */
static Kernel* g_kernel = NULL;

/* Prototipos de funciones auxiliares */
static int kernel_is_running(Kernel* kernel);

/* Prototipos de funciones de los hilos */
static void* clock_thread_func(void* arg);
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
    
    /* Create physical memory (Parte 3) */
    kernel->physical_memory = physical_memory_create();
    if (!kernel->physical_memory) {
        free(kernel);
        return NULL;
    }
    
    /* Crear la máquina */
    kernel->machine = machine_create(
        config->num_cpus,
        config->num_cores_per_cpu,
        config->num_hw_threads_per_core
    );
    
    if (!kernel->machine) {
        physical_memory_destroy(kernel->physical_memory);
        free(kernel);
        return NULL;
    }
    
    /* Crear la cola de procesos */
    kernel->process_queue = (ProcessQueue*)malloc(sizeof(ProcessQueue));
    if (!kernel->process_queue) {
        machine_destroy(kernel->machine);
        physical_memory_destroy(kernel->physical_memory);
        free(kernel);
        return NULL;
    }
    process_queue_init(kernel->process_queue);
    
    /* Crear timer para el generador de procesos */
    kernel->procgen_timer = churros_timer_create(config->process_gen_period);
    if (!kernel->procgen_timer) {
        process_queue_destroy(kernel->process_queue);
        free(kernel->process_queue);
        machine_destroy(kernel->machine);
        physical_memory_destroy(kernel->physical_memory);
        free(kernel);
        return NULL;
    }
    
    /* Inicializar el reloj */
    clock_init();
    
    /* Inicializar mutexes y estado */
    pthread_mutex_init(&kernel->running_mutex, NULL);
    pthread_mutex_init(&kernel->pid_mutex, NULL);
    pthread_mutex_init(&kernel->scheduler_mutex, NULL);
    pthread_cond_init(&kernel->scheduler_cond, NULL);
    kernel->pending_events = SCHED_EVENT_NONE;
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
    churros_timer_destroy(kernel->procgen_timer);
    process_queue_destroy(kernel->process_queue);
    free(kernel->process_queue);
    machine_destroy(kernel->machine);
    physical_memory_destroy(kernel->physical_memory);
    clock_destroy();
    
    /* Destruir mutexes y condition variables */
    pthread_mutex_destroy(&kernel->running_mutex);
    pthread_mutex_destroy(&kernel->pid_mutex);
    pthread_mutex_destroy(&kernel->scheduler_mutex);
    pthread_cond_destroy(&kernel->scheduler_cond);
    
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
    
    /* Log configuration */
    LOG_NOTICE(LOG_COMPONENT_KERNEL, "=== Iniciando churrOS ===");
    char line[256];
    snprintf(line, sizeof(line), "          │ CPUs: %u, Cores: %u, HW Threads: %u\n",
           kernel->config.num_cpus, kernel->config.num_cores_per_cpu, kernel->config.num_hw_threads_per_core);
    write(STDOUT_FILENO, line, strlen(line));
    
    const char* algo_name = kernel->config.scheduler_algorithm == SCHEDULER_ROUND_ROBIN ? "Round Robin" :
                           kernel->config.scheduler_algorithm == SCHEDULER_FIFO ? "FIFO" : "Chocolate Caliente";
    snprintf(line, sizeof(line), "          │ Algoritmo: %s (quantum=%u ticks)\n", algo_name, kernel->config.rr_quantum);
    write(STDOUT_FILENO, line, strlen(line));
    
    snprintf(line, sizeof(line), "          │ Generación: %u ticks, Clock: %u ms, Duración: %s\n",
           kernel->config.process_gen_period, kernel->config.clock_speed_ms,
           kernel->config.simulation_duration > 0 ? "limitada" : "infinita");
    write(STDOUT_FILENO, line, strlen(line));
    
    /* Crear e iniciar hilos */
    if (pthread_create(&kernel->scheduler_thread, NULL, scheduler_thread_func, kernel) != 0) {
        LOG_CRITICAL(LOG_COMPONENT_KERNEL, "Error al crear hilo Scheduler");
        kernel->running = 0;
        return -1;
    }
    
    if (pthread_create(&kernel->process_gen_thread, NULL, process_generator_thread_func, kernel) != 0) {
        LOG_CRITICAL(LOG_COMPONENT_KERNEL, "Error al crear hilo Process Generator");
        kernel->running = 0;
        return -1;
    }
    
    /* Crear hilo monitor del timer del generador de procesos */
    if (pthread_create(&kernel->procgen_timer_thread, NULL, procgen_timer_thread_func, kernel) != 0) {
        LOG_CRITICAL(LOG_COMPONENT_KERNEL, "Error al crear hilo Timer");
        kernel->running = 0;
        return -1;
    }
    
    if (pthread_create(&kernel->clock_thread, NULL, clock_thread_func, kernel) != 0) {
        LOG_CRITICAL(LOG_COMPONENT_KERNEL, "Error al crear hilo Clock");
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

    LOG_NOTICE(LOG_COMPONENT_KERNEL, "=== Deteniendo Kernel ===");

    churros_timer_wake(kernel->procgen_timer);
    
    /* Despertar al scheduler para que pueda terminar */
    pthread_cond_broadcast(&kernel->scheduler_cond);

    /* Opcional: forzar cancelación de hilos potencialmente bloqueados */
    pthread_cancel(kernel->clock_thread);
    pthread_cancel(kernel->procgen_timer_thread);
    pthread_cancel(kernel->scheduler_thread);
    pthread_cancel(kernel->process_gen_thread);

    pthread_join(kernel->clock_thread, NULL);
    pthread_join(kernel->procgen_timer_thread, NULL);
    pthread_join(kernel->scheduler_thread, NULL);
    pthread_join(kernel->process_gen_thread, NULL);

    LOG_INFO(LOG_COMPONENT_KERNEL, "Kernel detenido correctamente");
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

void kernel_signal_scheduler(Kernel* kernel, SchedulerEventFlags event)
{
    if (!kernel)
        return;
    
    pthread_mutex_lock(&kernel->scheduler_mutex);
    kernel->pending_events |= event; /* Agregar evento a los pendientes */
    pthread_cond_signal(&kernel->scheduler_cond); /* Despertar al scheduler */
    pthread_mutex_unlock(&kernel->scheduler_mutex);
}

/* Implementación de los hilos */

static void* clock_thread_func(void* arg)
{
    Kernel* kernel = (Kernel*)arg;
    uint32_t tick_count = 0;
    
    LOG_INFO(LOG_COMPONENT_CLOCK, "Clock iniciado");
    
    while (kernel_is_running(kernel)) {
        usleep(kernel->config.clock_speed_ms * 1000);
        
        clock_pulse();
        tick_count++;
        LOG_DEBUG(LOG_COMPONENT_CLOCK, "[%u] Tick", tick_count);
        
        machine_advance_cycle(kernel->machine, kernel);
        churros_timer_tick(kernel->procgen_timer);
        
        if (kernel->config.simulation_duration > 0 && 
            tick_count >= kernel->config.simulation_duration) {
            LOG_NOTICE(LOG_COMPONENT_CLOCK, "Duración de simulación alcanzada (%u ticks)", tick_count);
            kernel_stop(kernel);
            break;
        }
    }
    
    LOG_INFO(LOG_COMPONENT_CLOCK, "Terminado (total de ticks: %u)", tick_count);
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
                LOG_DEBUG(LOG_COMPONENT_TIMER, "[%s] Activación #%u por interrupción del timer", name, interrupt_count);
            }
        }
    }
    return NULL;
}

static void* procgen_timer_thread_func(void* arg)
{
    return timer_monitor_thread(arg, ((Kernel*)arg)->procgen_timer, "ProcessGenerator", 0);
}

static int kernel_is_running(Kernel* kernel)
{
    if (!kernel)
        return 0;

    return kernel->running;
}

static void* scheduler_thread_func(void* arg)
{
    Kernel* kernel = (Kernel*)arg;
    uint32_t activation_count = 0;
    
    LOG_INFO(LOG_COMPONENT_SCHEDULER, "Scheduler iniciado (event-driven)");
    
    while (kernel_is_running(kernel)) {
        /* Esperar a que haya eventos pendientes */
        pthread_mutex_lock(&kernel->scheduler_mutex);
        while (kernel->pending_events == SCHED_EVENT_NONE && kernel_is_running(kernel)) {
            pthread_cond_wait(&kernel->scheduler_cond, &kernel->scheduler_mutex);
        }
        
        if (!kernel_is_running(kernel)) {
            pthread_mutex_unlock(&kernel->scheduler_mutex);
            break;
        }
        
        /* Copiar eventos pendientes y limpiar */
        kernel->pending_events = SCHED_EVENT_NONE;
        pthread_mutex_unlock(&kernel->scheduler_mutex);
        
        activation_count++;
        
        /* Bloquear la máquina para realizar cambios de contexto de forma segura */
        pthread_mutex_lock(&kernel->machine->mutex);
        
        Machine* m = kernel->machine;
        for (uint32_t i = 0; i < m->num_cpus; i++) {
            for (uint32_t j = 0; j < m->cpus[i].num_cores; j++) {
                for (uint32_t k = 0; k < m->cpus[i].cores[j].num_hw_threads; k++) {
                    HWThread* thread = &m->cpus[i].cores[j].hw_threads[k];
                    scheduler_update_thread(kernel, thread, i, j, k);
                }
            }
        }
        
        pthread_mutex_unlock(&kernel->machine->mutex);
    }
    
    LOG_INFO(LOG_COMPONENT_SCHEDULER, "Scheduler terminado (activaciones totales: %u)", activation_count);
    return NULL;
}

static void* process_generator_thread_func(void* arg)
{
    Kernel* kernel = (Kernel*)arg;
    uint32_t prog_count = 0;

    LOG_INFO(LOG_COMPONENT_LOADER,
           "Loader iniciado (periodo: %u ticks, %llu ms)",
        kernel->config.process_gen_period,
        (unsigned long long)kernel->config.process_gen_period * kernel->config.clock_speed_ms);

    while (kernel_is_running(kernel)) {
        churros_timer_wait_interrupt(kernel->procgen_timer);

        if (!kernel_is_running(kernel))
            break;

        /* Try to load a program from file */
        char filename[256];
        snprintf(filename, sizeof(filename), "elfs/prog%03u.elf", prog_count);
        
        PCB* pcb = loader_load_program(filename, kernel->physical_memory, &kernel->next_pid);
        
        if (pcb) {
            /* Program loaded successfully */
            pcb->state = PROCESS_STATE_READY;
            process_queue_enqueue(kernel->process_queue, pcb);
            LOG_NOTICE(LOG_COMPONENT_LOADER, "Programa %s cargado: PID=%u", filename, pcb->pid);
            
            /* Dump data segment before execution */
            LOG_DEBUG(LOG_COMPONENT_LOADER, "Data segment before execution:");
            uint32_t data_start_phys = 0;
            uint32_t data_vpn = GET_VPN(pcb->mm.data_start);
            
            /* We need to access the page table to get physical address */
            uint32_t pte_addr = pcb->mm.pgb + data_vpn * sizeof(PageTableEntry);
            uint32_t pte_data = physical_memory_read_word(kernel->physical_memory, pte_addr);
            PageTableEntry pte;
            memcpy(&pte, &pte_data, sizeof(PageTableEntry));
            
            if (pte.valid && pte.present) {
                data_start_phys = (pte.pfn << PAGE_BITS) | GET_OFFSET(pcb->mm.data_start);
                physical_memory_dump(kernel->physical_memory, data_start_phys, 
                                    pcb->mm.data_size / WORD_SIZE);
            }
            
            /* Señalizar al scheduler que hay un nuevo proceso */
            kernel_signal_scheduler(kernel, SCHED_EVENT_PROCESS_CREATED);
            prog_count++;
        } else {
            /* No more program files, fall back to random process generation */
            LOG_WARN(LOG_COMPONENT_LOADER, "No se encontró %s, generando proceso aleatorio", filename);
            
            uint32_t pid = kernel_get_next_pid(kernel);
            pcb = pcb_create(pid);

            if (pcb) {
                pcb->state = PROCESS_STATE_READY;
                process_queue_enqueue(kernel->process_queue, pcb);
                LOG_DEBUG(LOG_COMPONENT_PROCESS, 
                       "Nuevo proceso creado: PID=%u, TTL=%u", 
                       pcb->pid, pcb->ttl);
                
                /* Señalizar al scheduler que hay un nuevo proceso */
                kernel_signal_scheduler(kernel, SCHED_EVENT_PROCESS_CREATED);
            } else {
                LOG_ERROR(LOG_COMPONENT_PROCESS, "Error al crear el proceso");
            }
        }
    }
    
    LOG_INFO(LOG_COMPONENT_LOADER, "Loader terminado");
    return NULL;
}
