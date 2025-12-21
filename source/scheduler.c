#include "scheduler.h"
#include "process_queue.h"
#include "pcb.h"
#include <stdio.h>

/* ============================================
 * SCHEDULER ALGORITHMS IMPLEMENTATION
 * ============================================ */

/* --- Chocolate Caliente helpers --- */

/**
 * Calcula el quantum máximo basado en la temperatura del proceso.
 * Cuanto más caliente, menor quantum (sorbitos más pequeños).
 */
static uint32_t get_max_quantum_by_temperature(uint32_t temperature)
{
    if (temperature >= 80) return 1;
    if (temperature >= 60) return 2;
    if (temperature >= 40) return 4;
    if (temperature >= 20) return 6;
    return 10;
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
                printf("[Scheduler] Dispatch PID=%u (Reemplazando IDLE) a CPU %u Core %u Thread %u (TTL=%u)\n",
                       next->pid, cpu, core, hw_thread, next->ttl);
            }
        }
        return;
    }

    /* Caso 2: Proceso Terminado */
    if (current->ttl == 0) {
        printf("[Scheduler] Proceso PID=%u terminado en CPU %u Core %u Thread %u\n",
               current->pid, cpu, core, hw_thread);
        current->state = PROCESS_STATE_TERMINATED;
        pcb_destroy(current);

        if (!process_queue_is_empty(kernel->process_queue)) {
            PCB* next = process_queue_dequeue(kernel->process_queue);
            scheduler_dispatch(thread, next, cpu, core, hw_thread);
            printf("[Scheduler] Dispatch PID=%u a CPU %u Core %u Thread %u (TTL=%u)\n",
                   next->pid, cpu, core, hw_thread, next->ttl);
        } else {
            thread->current_pcb = pcb_create_idle();
            printf("[Scheduler] CPU %u Core %u Thread %u pasa a IDLE\n", cpu, core, hw_thread);
        }
        return;
    }

    /* Caso 3: Depende del algoritmo de scheduling */
    if (kernel->config.scheduler_algorithm == SCHEDULER_ROUND_ROBIN) {
        /* Round Robin: Preempción por quantum */
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
            printf("[Scheduler] Preemption PID=%u -> Dispatch PID=%u en CPU %u Core %u Thread %u\n",
                   current->pid, next->pid, cpu, core, hw_thread);
        }
    } else if (kernel->config.scheduler_algorithm == SCHEDULER_CHOCOLATE_CALIENTE) {
        /* Chocolate Caliente: Quantum adaptativo basado en temperatura */
        
        /* El proceso ejecutándose se calienta */
        current->ticks_since_swap++;
        current->temperature += 8;
        if (current->temperature > 100) current->temperature = 100;
        
        uint32_t max_quantum = get_max_quantum_by_temperature(current->temperature);
        
        printf("[Scheduler] PID=%u Temp=%u°C %s Quantum=%u Ticks=%u/%u TTL=%u\n",
               current->pid, current->temperature, get_temperature_emoji(current->temperature),
               max_quantum, current->ticks_since_swap, max_quantum, current->ttl);
        
        /* Verificar si el quantum se ha agotado */
        if (current->ticks_since_swap >= max_quantum && !process_queue_is_empty(kernel->process_queue)) {
            /* Cambio de contexto */
            current->state = PROCESS_STATE_READY;
            current->cpu_id = -1;
            current->core_id = -1;
            current->hw_thread_id = -1;
            
            process_queue_enqueue(kernel->process_queue, current);
            
            /* Despachar siguiente proceso */
            PCB* next = process_queue_dequeue(kernel->process_queue);
            scheduler_dispatch(thread, next, cpu, core, hw_thread);
            
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
            
            printf("[Scheduler] Context switch PID=%u (enfriándose) -> PID=%u (quantum=%u)\n",
                   current->pid, next->pid, get_max_quantum_by_temperature(next->temperature));
        }
    }
    /* FIFO: No hay preemption por tiempo, el proceso continúa ejecutando */
}
