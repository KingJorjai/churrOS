/*
 * machine.c
 * Implementación de la estructura Machine
 */

#include "../include/machine.h"
#include <stdlib.h>
#include <stdio.h>

static void init_hw_threads(HWThread* threads, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        threads[i].hw_thread_id = i;
        /* Inicializar con proceso IDLE */
        threads[i].current_pcb = pcb_create_idle();
    }
}

static int init_core(Core* core, uint32_t core_id, uint32_t num_threads)
{
    core->core_id = core_id;
    core->num_hw_threads = num_threads;
    core->hw_threads = (HWThread*)malloc(sizeof(HWThread) * num_threads);
    
    if (!core->hw_threads)
        return -1;
    
    init_hw_threads(core->hw_threads, num_threads);
    return 0;
}

static void cleanup_cpu(CPU* cpu, uint32_t num_cores)
{
    for (uint32_t i = 0; i < num_cores; i++) {
        /* Liberar PCBs (incluyendo IDLEs) antes de liberar el array de hilos */
        for (uint32_t k = 0; k < cpu->cores[i].num_hw_threads; k++) {
            if (cpu->cores[i].hw_threads[k].current_pcb) {
                pcb_destroy(cpu->cores[i].hw_threads[k].current_pcb);
            }
        }
        free(cpu->cores[i].hw_threads);
    }
    free(cpu->cores);
}

static int init_cpu(CPU* cpu, uint32_t cpu_id, uint32_t num_cores, uint32_t num_threads)
{
    cpu->cpu_id = cpu_id;
    cpu->num_cores = num_cores;
    cpu->cores = (Core*)malloc(sizeof(Core) * num_cores);
    
    if (!cpu->cores)
        return -1;
    
    for (uint32_t i = 0; i < num_cores; i++) {
        if (init_core(&cpu->cores[i], i, num_threads) != 0) {
            for (uint32_t j = 0; j < i; j++) {
                free(cpu->cores[j].hw_threads);
            }
            free(cpu->cores);
            return -1;
        }
    }
    return 0;
}

Machine* machine_create(uint32_t num_cpus, uint32_t num_cores_per_cpu, uint32_t num_hw_threads_per_core)
{
    if (num_cpus == 0 || num_cores_per_cpu == 0 || num_hw_threads_per_core == 0)
        return NULL;
    
    Machine* machine = (Machine*)malloc(sizeof(Machine));
    if (!machine)
        return NULL;
    
    machine->num_cpus = num_cpus;
    machine->cpus = (CPU*)malloc(sizeof(CPU) * num_cpus);
    
    if (!machine->cpus) {
        free(machine);
        return NULL;
    }
    
    for (uint32_t i = 0; i < num_cpus; i++) {
        if (init_cpu(&machine->cpus[i], i, num_cores_per_cpu, num_hw_threads_per_core) != 0) {
            for (uint32_t j = 0; j < i; j++) {
                cleanup_cpu(&machine->cpus[j], num_cores_per_cpu);
            }
            free(machine->cpus);
            free(machine);
            return NULL;
        }
    }
    
    pthread_mutex_init(&machine->mutex, NULL);
    return machine;
}

void machine_destroy(Machine* machine)
{
    if (!machine)
        return;
    
    for (uint32_t i = 0; i < machine->num_cpus; i++) {
        cleanup_cpu(&machine->cpus[i], machine->cpus[i].num_cores);
    }
    
    free(machine->cpus);
    pthread_mutex_destroy(&machine->mutex);
    free(machine);
}

void machine_advance_cycle(Machine* machine)
{
    if (!machine)
        return;
        
    pthread_mutex_lock(&machine->mutex);
    
    for (uint32_t i = 0; i < machine->num_cpus; i++) {
        for (uint32_t j = 0; j < machine->cpus[i].num_cores; j++) {
            for (uint32_t k = 0; k < machine->cpus[i].cores[j].num_hw_threads; k++) {
                HWThread* thread = &machine->cpus[i].cores[j].hw_threads[k];
                if (thread->current_pcb) {
                    /* Consumir tiempo del proceso SOLO si no es IDLE (PID 0) */
                    if (thread->current_pcb->pid != 0 && thread->current_pcb->ttl > 0) {
                        thread->current_pcb->ttl--;
                    }
                }
            }
        }
    }
    
    pthread_mutex_unlock(&machine->mutex);
}

void machine_print_status(Machine* machine)
{
    if (!machine)
        return;
    
    pthread_mutex_lock(&machine->mutex);
    
    printf("=== Machine Status ===\n");
    printf("CPUs: %u\n", machine->num_cpus);
    
    for (uint32_t i = 0; i < machine->num_cpus; i++) {
        printf("  CPU %u: %u cores\n", i, machine->cpus[i].num_cores);
        for (uint32_t j = 0; j < machine->cpus[i].num_cores; j++) {
            printf("    Core %u: %u hw_threads\n", j, machine->cpus[i].cores[j].num_hw_threads);
            for (uint32_t k = 0; k < machine->cpus[i].cores[j].num_hw_threads; k++) {
                HWThread* t = &machine->cpus[i].cores[j].hw_threads[k];
                if (t->current_pcb) {
                    if (t->current_pcb->pid == 0) {
                        printf("      HW Thread %u: IDLE (PID=0)\n", k);
                    } else {
                        printf("      HW Thread %u: PID=%u (TTL=%u)\n", 
                            k, t->current_pcb->pid, t->current_pcb->ttl);
                    }
                } else {
                    printf("      HW Thread %u: NULL (Error: No Idle PCB)\n", k);
                }
            }
        }
    }
    
    pthread_mutex_unlock(&machine->mutex);
}
