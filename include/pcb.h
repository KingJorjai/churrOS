/*
 * pcb.h
 * Process Control Block definition
 */

#ifndef CHURROS_PCB_H
#define CHURROS_PCB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t pid;          /* Process ID */
} PCB;

/* Crear un nuevo PCB con un PID dado y tiempo de vida aleatorio */
PCB* pcb_create(uint32_t pid);

/* Destruir un PCB y liberar sus recursos */
void pcb_destroy(PCB* pcb);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_PCB_H */
