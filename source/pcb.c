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
    
    return pcb;
}

void pcb_destroy(PCB* pcb)
{
    if (pcb) {
        free(pcb);
    }
}
