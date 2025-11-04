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
    
    return pcb;
}

void pcb_destroy(PCB* pcb)
{
    if (pcb) {
        free(pcb);
    }
}
