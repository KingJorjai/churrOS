#include "scheduler.h"
#include "process_queue.h"
#include "pcb.h"
#include "memory.h"
#include "logging.h"
#include <stdio.h>

/* ============================================
 * SCHEDULER ALGORITHMS IMPLEMENTATION
 * ============================================ */

/* --- Chocolate Caliente helpers --- */

/**
 * Calcula el quantum máximo basado en la temperatura del proceso.
 * Cuanto más caliente, menor quantum (sorbitos más pequeños).
 * 
 * quantum_base actúa como multiplicador para ajustar la escala de tiempos.
 * Por defecto (quantum_base=5):
 *   - Frío (<20°C): 10 ticks
 *   - Templado (20-39°C): 6 ticks
 *   - Caliente (40-59°C): 4 ticks
 *   - Muy caliente (60-79°C): 2 ticks
 *   - Ardiendo (≥80°C): 1 tick
 */
static uint32_t get_max_quantum_by_temperature(uint32_t temperature, uint32_t quantum_base)
{
    /* Los factores son proporcionales al quantum_base */
    uint32_t base_multiplier = (quantum_base > 0) ? quantum_base : 5;
    
    if (temperature >= 80) return (base_multiplier * 1) / 5;  /* 20% del base */
    if (temperature >= 60) return (base_multiplier * 2) / 5;  /* 40% del base */
    if (temperature >= 40) return (base_multiplier * 4) / 5;  /* 80% del base */
    if (temperature >= 20) return (base_multiplier * 6) / 5;  /* 120% del base */
    return base_multiplier * 2;                                /* 200% del base */
}

/**
 * Obtiene un emoji representativo de la temperatura del proceso.
 */
static const char* get_temperature_emoji(uint32_t temperature)
{
    if (temperature >= 80) return "🔥";
    if (temperature >= 60) return "🔴";
    if (temperature >= 40) return "🟡";
    if (temperature >= 20) return "🟢";
    return "❄️";
}

/* --- Core scheduler logic --- */

/**
 * Despacha un proceso a un hardware thread específico.
 * Actualiza el estado del PCB y reinicia contadores.
 */
static void scheduler_dispatch(HWThread* thread, PCB* pcb, uint32_t cpu, uint32_t core, uint32_t hw_thread)
{
    pcb->state = PROCESS_STATE_RUNNING;
    pcb->cpu_id = cpu;
    pcb->core_id = core;
    pcb->hw_thread_id = hw_thread;
    pcb->ticks_since_swap = 0; /* Reiniciar contador */
    thread->current_pcb = pcb;
    
    /* Configure MMU for this process if it has a program loaded */
    if (pcb->is_loaded && thread->mmu) {
        mmu_set_ptbr(thread->mmu, pcb->mm.pgb);
        thread->mmu->pc = pcb->mm.code_start;
    }
}

void scheduler_update_thread(Kernel* kernel, HWThread* thread, uint32_t cpu, uint32_t core, uint32_t hw_thread)
{
    if (!thread->current_pcb) return;

    PCB* current = thread->current_pcb;

    /* Caso 1: Proceso IDLE */
    if (current->pid == 0) {
        if (!process_queue_is_empty(kernel->process_queue)) {
            PCB* next = process_queue_dequeue(kernel->process_queue);
            if (next) {
                pcb_destroy(current); /* Destruir IDLE */
                scheduler_dispatch(thread, next, cpu, core, hw_thread);
                
                pthread_mutex_lock(&kernel->stats_mutex);
                kernel->context_switches++;
                pthread_mutex_unlock(&kernel->stats_mutex);
                
                LOG_AT_INFO(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
                       "Dispatch PID=%u (Reemplazando IDLE) TTL=%u",
                       next->pid, next->ttl);
            }
        }
        return;
    }

    /* Caso 2: Proceso Terminado */
    /* For loaded programs, termination is signaled by PROCESS_STATE_TERMINATED */
    /* For random processes, termination occurs when TTL reaches 0 */
    if (current->state == PROCESS_STATE_TERMINATED || (!current->is_loaded && current->ttl == 0)) {
        LOG_AT_NOTICE(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
               "Proceso PID=%u terminado", current->pid);
        current->state = PROCESS_STATE_TERMINATED;
        
        /* Increment completed processes counter */
        pthread_mutex_lock(&kernel->stats_mutex);
        kernel->processes_completed++;
        pthread_mutex_unlock(&kernel->stats_mutex);
        
        pcb_destroy(current);

        if (!process_queue_is_empty(kernel->process_queue)) {
            PCB* next = process_queue_dequeue(kernel->process_queue);
            scheduler_dispatch(thread, next, cpu, core, hw_thread);
            
            pthread_mutex_lock(&kernel->stats_mutex);
            kernel->context_switches++;
            pthread_mutex_unlock(&kernel->stats_mutex);
            
            LOG_AT_INFO(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
                   "Dispatch PID=%u TTL=%u", next->pid, next->ttl);
        } else {
            thread->current_pcb = pcb_create_idle();
            LOG_AT_DEBUG(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread, "Pasa a IDLE");
        }
        return;
    }

    /* Caso 3: Preempción por quantum (solo RR y Chocolate Caliente) */
    if (kernel->config.scheduler_algorithm == SCHEDULER_ROUND_ROBIN) {
        /* Round Robin: Preempción cuando quantum expira */
        if (current->ticks_since_swap >= kernel->config.rr_quantum) {
            if (!process_queue_is_empty(kernel->process_queue)) {
                /* Desalojar actual */
                current->state = PROCESS_STATE_READY;
                current->cpu_id = -1;
                current->core_id = -1;
                current->hw_thread_id = -1;
                process_queue_enqueue(kernel->process_queue, current);

                /* Despachar siguiente */
                PCB* next = process_queue_dequeue(kernel->process_queue);
                scheduler_dispatch(thread, next, cpu, core, hw_thread);
                
                pthread_mutex_lock(&kernel->stats_mutex);
                kernel->context_switches++;
                pthread_mutex_unlock(&kernel->stats_mutex);
                
                LOG_AT_INFO(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
                       "Preemption PID=%u -> PID=%u", current->pid, next->pid);
            }
        }
    } else if (kernel->config.scheduler_algorithm == SCHEDULER_CHOCOLATE_CALIENTE) {
        /* Chocolate Caliente: Quantum adaptativo basado en temperatura */
        uint32_t max_quantum = get_max_quantum_by_temperature(current->temperature, kernel->config.rr_quantum);
        
        LOG_AT_DEBUG(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
               "PID=%u Temp=%u°C %s Quantum=%u Ticks=%u/%u TTL=%u",
               current->pid, current->temperature, get_temperature_emoji(current->temperature),
               max_quantum, current->ticks_since_swap, max_quantum, current->ttl);
        
        /* Verificar si el quantum se ha agotado */
        if (current->ticks_since_swap >= max_quantum) {
            if (!process_queue_is_empty(kernel->process_queue)) {
                /* Cambio de contexto */
                current->state = PROCESS_STATE_READY;
                current->cpu_id = -1;
                current->core_id = -1;
                current->hw_thread_id = -1;
                
                process_queue_enqueue(kernel->process_queue, current);
                
                /* Despachar siguiente proceso */
                PCB* next = process_queue_dequeue(kernel->process_queue);
                scheduler_dispatch(thread, next, cpu, core, hw_thread);
                
                pthread_mutex_lock(&kernel->stats_mutex);
                kernel->context_switches++;
                pthread_mutex_unlock(&kernel->stats_mutex);
                
                /* Los procesos en espera se enfrían */
                uint32_t queue_size = process_queue_size(kernel->process_queue);
                for (uint32_t i = 0; i < queue_size; i++) {
                    PCB* waiting = process_queue_dequeue(kernel->process_queue);
                    if (waiting->temperature >= 5) {
                        waiting->temperature -= 5;
                    } else {
                        waiting->temperature = 0;
                    }
                    process_queue_enqueue(kernel->process_queue, waiting);
                }
                
                LOG_AT_INFO(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
                       "Context switch PID=%u (enfriándose) -> PID=%u (quantum=%u)",
                       current->pid, next->pid, get_max_quantum_by_temperature(next->temperature, kernel->config.rr_quantum));
            }
        }
    }
    /* FIFO: No hay preemption por tiempo, el proceso continúa ejecutando */
}
