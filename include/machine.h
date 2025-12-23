/*
 * machine.h
 * Estructura que representa las CPUs, cores e hilos hardware
 */

#ifndef CHURROS_MACHINE_H
#define CHURROS_MACHINE_H

#include <stdint.h>
#include <pthread.h>

#include "pcb.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration para evitar dependencias circulares */
typedef struct Kernel Kernel;

/* Hardware thread (hilo hardware) */
typedef struct {
    uint32_t hw_thread_id;
    PCB* current_pcb;      /* Proceso asignado (NULL = idle) */
    MMU* mmu;              /* Memory Management Unit (Parte 3) */
} HWThread;

/* Core (núcleo) */
typedef struct {
    uint32_t core_id;
    uint32_t num_hw_threads;
    HWThread* hw_threads;
} Core;

/* CPU */
typedef struct {
    uint32_t cpu_id;
    uint32_t num_cores;
    Core* cores;
} CPU;

/* Machine (máquina completa) */
typedef struct {
    uint32_t num_cpus;
    CPU* cpus;
    pthread_mutex_t mutex;
} Machine;

/* Crear e inicializar la máquina */
Machine* machine_create(uint32_t num_cpus, uint32_t num_cores_per_cpu, uint32_t num_hw_threads_per_core);

/* Destruir la máquina y liberar recursos */
void machine_destroy(Machine* machine);

/* Avanzar un ciclo de reloj (decrementar TTL, gestionar quantum, detectar eventos) */
void machine_advance_cycle(Machine* machine, Kernel* kernel);

/* Imprimir el estado de la máquina */
void machine_print_status(Machine* machine);

#ifdef __cplusplus
}
#endif

#endif /* CHURROS_MACHINE_H */
