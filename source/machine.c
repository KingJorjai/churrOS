/*
 * machine.c
 * Implementación de la estructura Machine
 */

#include "../include/machine.h"
#include "../include/kernel.h"
#include "../include/instruction.h"
#include "../include/logging.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

static void init_hw_threads(HWThread* threads, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        threads[i].hw_thread_id = i;
        /* Inicializar con proceso IDLE */
        threads[i].current_pcb = pcb_create_idle();
        /* Create MMU for this hardware thread */
        threads[i].mmu = mmu_create();
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
            if (cpu->cores[i].hw_threads[k].mmu) {
                mmu_destroy(cpu->cores[i].hw_threads[k].mmu);
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

void machine_advance_cycle(Machine* machine, Kernel* kernel)
{
    if (!machine || !kernel)
        return;
        
    pthread_mutex_lock(&machine->mutex);
    
    int quantum_expired_detected = 0;
    int process_terminated_detected = 0;
    
    for (uint32_t i = 0; i < machine->num_cpus; i++) {
        for (uint32_t j = 0; j < machine->cpus[i].num_cores; j++) {
            for (uint32_t k = 0; k < machine->cpus[i].cores[j].num_hw_threads; k++) {
                HWThread* thread = &machine->cpus[i].cores[j].hw_threads[k];
                if (!thread->current_pcb)
                    continue;
                
                PCB* pcb = thread->current_pcb;
                
                /* Solo procesar procesos reales (no IDLE) */
                if (pcb->pid == 0)
                    continue;
                
                /* Execute one instruction if program is loaded */
                if (pcb->is_loaded && thread->mmu) {
                    /* Ensure MMU is configured for this process */
                    if (thread->mmu->ptbr != pcb->mm.pgb) {
                        /* MMU not yet configured, skip this cycle */
                        continue;
                    }
                    
                    /* Fetch instruction */
                    uint32_t vaddr = thread->mmu->pc;
                    uint32_t paddr = mmu_translate(thread->mmu, kernel->physical_memory, vaddr, 0);
                    
                    if (paddr != 0xFFFFFFFF) {
                        Instruction instr = physical_memory_read_word(kernel->physical_memory, paddr);
                        thread->mmu->ir = instr;
                        
                        /* Log instruction fetch */
                        LOG_AT_DEBUG(LOG_COMPONENT_MACHINE, i, j, k,
                               "PID=%u PC=0x%06X [%08X]", pcb->pid, vaddr, instr);
                        
                        /* Execute instruction */
                        int continue_exec = instruction_execute(instr, thread->mmu, kernel->physical_memory);
                        
                        if (continue_exec) {
                            /* Move to next instruction */
                            thread->mmu->pc += WORD_SIZE;
                        } else {
                            /* EXIT instruction - program terminated */
                            LOG_AT_NOTICE(LOG_COMPONENT_MACHINE, i, j, k,
                                   "PID=%u executed EXIT instruction", pcb->pid);
                            
                            /* Dump data segment after execution */
                            if (pcb->is_loaded && pcb->mm.data_size > 0) {
                                LOG_AT_DEBUG(LOG_COMPONENT_LOADER, i, j, k, "Data segment after execution:");
                                uint32_t data_vpn = GET_VPN(pcb->mm.data_start);
                                
                                /* Access the page table to get physical address */
                                uint32_t pte_addr = pcb->mm.pgb + data_vpn * sizeof(PageTableEntry);
                                uint32_t pte_data = physical_memory_read_word(kernel->physical_memory, pte_addr);
                                PageTableEntry pte;
                                memcpy(&pte, &pte_data, sizeof(PageTableEntry));
                                
                                if (pte.valid && pte.present) {
                                    uint32_t data_start_phys = (pte.pfn << PAGE_BITS) | GET_OFFSET(pcb->mm.data_start);
                                    physical_memory_dump(kernel->physical_memory, data_start_phys, 
                                                        pcb->mm.data_size / WORD_SIZE);
                                }
                            }
                            
                            pcb->state = PROCESS_STATE_TERMINATED;
                            process_terminated_detected = 1;
                        }
                    }
                }
                
                /* 1. Decrementar TTL (for old-style random processes) */
                if (pcb->ttl > 0 && !pcb->is_loaded) {
                    pcb->ttl--;
                    
                    /* Detectar terminación */
                    if (pcb->ttl == 0) {
                        process_terminated_detected = 1;
                    }
                }
                
                /* Check if TTL-based process (not loaded from file) has terminated */
                if (!pcb->is_loaded && pcb->ttl == 0) {
                    process_terminated_detected = 1;
                }
                
                /* 2. Incrementar tiempo de ejecución desde último swap */
                pcb->ticks_since_swap++;
                
                /* 3. Gestionar temperatura y quantum según el algoritmo */
                if (kernel->config.scheduler_algorithm == SCHEDULER_CHOCOLATE_CALIENTE) {
                    /* Actualizar temperatura cada tick */
                    pcb->temperature += 8;
                    if (pcb->temperature > 100)
                        pcb->temperature = 100;
                    
                    /* Calcular quantum adaptativo basado en temperatura */
                    uint32_t max_quantum;
                    if (pcb->temperature >= 80) max_quantum = 1;
                    else if (pcb->temperature >= 60) max_quantum = 2;
                    else if (pcb->temperature >= 40) max_quantum = 4;
                    else if (pcb->temperature >= 20) max_quantum = 6;
                    else max_quantum = 10;
                    
                    /* Detectar expiración de quantum adaptativo */
                    if (pcb->ticks_since_swap >= max_quantum) {
                        quantum_expired_detected = 1;
                    }
                } else if (kernel->config.scheduler_algorithm == SCHEDULER_ROUND_ROBIN) {
                    /* Detectar expiración de quantum fijo */
                    if (pcb->ticks_since_swap >= kernel->config.rr_quantum) {
                        quantum_expired_detected = 1;
                    }
                }
                /* FIFO: No hay gestión de quantum */
            }
        }
    }
    
    pthread_mutex_unlock(&machine->mutex);
    
    /* Señalizar eventos detectados al scheduler */
    if (process_terminated_detected) {
        kernel_signal_scheduler(kernel, SCHED_EVENT_PROCESS_TERMINATED);
    }
    if (quantum_expired_detected) {
        kernel_signal_scheduler(kernel, SCHED_EVENT_QUANTUM_EXPIRED);
    }
}

void machine_print_status(Machine* machine)
{
    if (!machine)
        return;
    
    pthread_mutex_lock(&machine->mutex);
    
    LOG_INFO(LOG_COMPONENT_MACHINE, "=== Machine Status ===");
    char line[256];
    snprintf(line, sizeof(line), "          │ CPUs: %u\n", machine->num_cpus);
    write(STDOUT_FILENO, line, strlen(line));
    
    for (uint32_t i = 0; i < machine->num_cpus; i++) {
        snprintf(line, sizeof(line), "          │   CPU %u: %u cores\n", i, machine->cpus[i].num_cores);
        write(STDOUT_FILENO, line, strlen(line));
        for (uint32_t j = 0; j < machine->cpus[i].num_cores; j++) {
            snprintf(line, sizeof(line), "          │     Core %u: %u hw_threads\n", j, machine->cpus[i].cores[j].num_hw_threads);
            write(STDOUT_FILENO, line, strlen(line));
            for (uint32_t k = 0; k < machine->cpus[i].cores[j].num_hw_threads; k++) {
                HWThread* t = &machine->cpus[i].cores[j].hw_threads[k];
                if (t->current_pcb) {
                    if (t->current_pcb->pid == 0) {
                        snprintf(line, sizeof(line), "          │       HW Thread %u: IDLE (PID=0)\n", k);
                    } else {
                        snprintf(line, sizeof(line), "          │       HW Thread %u: PID=%u (TTL=%u)\n", 
                                 k, t->current_pcb->pid, t->current_pcb->ttl);
                    }
                } else {
                    snprintf(line, sizeof(line), "          │       HW Thread %u: NULL (Error: No Idle PCB)\n", k);
                }
                write(STDOUT_FILENO, line, strlen(line));
            }
        }
    }
    
    pthread_mutex_unlock(&machine->mutex);
}
