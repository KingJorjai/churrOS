/*
 * process_queue.h
 * Cola de procesos (PCBs)
 */

#ifndef CHURROS_PROCESS_QUEUE_H
#define CHURROS_PROCESS_QUEUE_H

#include "pcb.h"
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct process_node {
    PCB* pcb;
    struct process_node* next;
} ProcessNode;

typedef struct {
    ProcessNode* head;
    ProcessNode* tail;
    uint32_t count;
    pthread_mutex_t mutex;
} ProcessQueue;

/* Inicializar la cola de procesos */
void process_queue_init(ProcessQueue* queue);

/* Destruir la cola de procesos */
void process_queue_destroy(ProcessQueue* queue);

/* Añadir un PCB a la cola */
int process_queue_enqueue(ProcessQueue* queue, PCB* pcb);

/* Extraer un PCB de la cola */
PCB* process_queue_dequeue(ProcessQueue* queue);

/* Obtener el tamaño de la cola */
uint32_t process_queue_size(ProcessQueue* queue);

/* Verificar si la cola está vacía */
int process_queue_is_empty(ProcessQueue* queue);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_PROCESS_QUEUE_H */
