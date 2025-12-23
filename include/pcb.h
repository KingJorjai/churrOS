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

typedef enum {
    PROCESS_STATE_NEW,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_TERMINATED
} ProcessState;

/* Memory management information */
typedef struct {
    uint32_t code_start;    /* Virtual address of code segment start */
    uint32_t data_start;    /* Virtual address of data segment start */
    uint32_t pgb;           /* Physical address of page table (Page Table Base) */
    uint32_t code_size;     /* Size of code segment in bytes */
    uint32_t data_size;     /* Size of data segment in bytes */
} ProcessMemoryMap;

typedef struct {
    uint32_t pid;          /* Process ID */
    uint32_t ttl;          /* Time To Live del proceso (en ticks) */
    ProcessState state;    /* Estado actual del proceso */
    
    /* Información de ubicación (si está en ejecución) */
    int cpu_id;
    int core_id;
    int hw_thread_id;
    
    /* Información para Chocolate Caliente */
    uint32_t temperature;        /* Temperatura actual (0-100) */
    uint32_t ticks_since_swap;   /* Ticks ejecutando desde último swap */
    
    /* Gestión de memoria (Parte 3) */
    ProcessMemoryMap mm;         /* Memory map and page table info */
    int is_loaded;               /* Flag: 1 if program loaded, 0 otherwise */
} PCB;

/* Crear un nuevo PCB con un PID dado y tiempo de vida aleatorio */
PCB* pcb_create(uint32_t pid);

/* Crear un proceso IDLE (PID=0, TTL infinito) */
PCB* pcb_create_idle(void);

/* Destruir un PCB y liberar sus recursos */
void pcb_destroy(PCB* pcb);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_PCB_H */
