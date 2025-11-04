/*
 * process_queue.c
 * Implementación de la cola de procesos
 */

#include "../include/process_queue.h"
#include <stdlib.h>
#include <string.h>

void process_queue_init(ProcessQueue* queue)
{
    if (!queue)
        return;
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
}

void process_queue_destroy(ProcessQueue* queue)
{
    if (!queue)
        return;
    
    pthread_mutex_lock(&queue->mutex);
    
    ProcessNode* current = queue->head;
    while (current) {
        ProcessNode* next = current->next;
        pcb_destroy(current->pcb);
        free(current);
        current = next;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
}

int process_queue_enqueue(ProcessQueue* queue, PCB* pcb)
{
    if (!queue || !pcb)
        return -1;
    
    ProcessNode* node = (ProcessNode*)malloc(sizeof(ProcessNode));
    if (!node)
        return -1;
    
    node->pcb = pcb;
    node->next = NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->tail) {
        queue->tail->next = node;
        queue->tail = node;
    } else {
        queue->head = node;
        queue->tail = node;
    }
    
    queue->count++;
    
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

PCB* process_queue_dequeue(ProcessQueue* queue)
{
    if (!queue)
        return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (!queue->head) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    
    ProcessNode* node = queue->head;
    PCB* pcb = node->pcb;
    
    queue->head = node->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    
    queue->count--;
    
    pthread_mutex_unlock(&queue->mutex);
    
    free(node);
    
    return pcb;
}

uint32_t process_queue_size(ProcessQueue* queue)
{
    if (!queue)
        return 0;
    
    pthread_mutex_lock(&queue->mutex);
    uint32_t size = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    
    return size;
}

int process_queue_is_empty(ProcessQueue* queue)
{
    return process_queue_size(queue) == 0;
}
