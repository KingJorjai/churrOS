#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "kernel.h"
#include "machine.h"

/**
 * Actualiza el estado de un hardware thread según el algoritmo de scheduling
 * configurado. Maneja dispatch inicial, finalización de procesos y preempción.
 *
 * @param kernel Puntero al kernel con la configuración y cola de procesos
 * @param thread Hardware thread a actualizar
 * @param cpu ID de la CPU
 * @param core ID del core
 * @param hw_thread ID del hardware thread
 */
void scheduler_update_thread(Kernel* kernel, HWThread* thread, 
                            uint32_t cpu, uint32_t core, uint32_t hw_thread);

#endif /* SCHEDULER_H */
