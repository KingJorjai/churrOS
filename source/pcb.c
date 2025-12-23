/*
 * pcb.c
 * Process Control Block implementation
 */

#include "../include/pcb.h"
#include <stdlib.h>
#include <time.h>

PCB* pcb_create(uint32_t pid)
{
    PCB* pcb = (PCB*)malloc(sizeof(PCB));
    if (!pcb)
        return NULL;
    
    pcb->pid = pid;
    /* Tiempo de vida aleatorio entre 1 y 100 ticks (por ejemplo) */
    pcb->ttl = (rand() % 100) + 1;
    pcb->state = PROCESS_STATE_NEW;
    pcb->cpu_id = -1;
    pcb->core_id = -1;
    pcb->hw_thread_id = -1;
    
    /* Inicializar temperatura en frío */
    pcb->temperature = 0;
    pcb->ticks_since_swap = 0;
    
    /* Initialize memory management fields */
    pcb->mm.code_start = 0;
    pcb->mm.data_start = 0;
    pcb->mm.pgb = 0;
    pcb->mm.code_size = 0;
    pcb->mm.data_size = 0;
    pcb->is_loaded = 0;
    
    return pcb;
}

PCB* pcb_create_idle(void)
{
    PCB* pcb = (PCB*)malloc(sizeof(PCB));
    if (!pcb)
        return NULL;
    
    pcb->pid = 0;
    pcb->ttl = 0; /* 0 representará infinito para IDLE */
    pcb->state = PROCESS_STATE_RUNNING;
    pcb->cpu_id = -1;
    pcb->core_id = -1;
    pcb->hw_thread_id = -1;
    
    /* IDLE siempre está frío */
    pcb->temperature = 0;
    pcb->ticks_since_swap = 0;
    
    /* IDLE doesn't have memory */
    pcb->mm.code_start = 0;
    pcb->mm.data_start = 0;
    pcb->mm.pgb = 0;
    pcb->mm.code_size = 0;
    pcb->mm.data_size = 0;
    pcb->is_loaded = 0;
    
    return pcb;
}

void pcb_destroy(PCB* pcb)
{
    if (pcb) {
        free(pcb);
    }
}
