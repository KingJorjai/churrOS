# Parte 2: Scheduler y Algoritmos de Planificación

## Introducción

Esta segunda parte mete el scheduler, que es donde la cosa se pone interesante. En vez de hacer un scheduler clásico que se ejecuta periódicamente, montamos uno **completamente event-driven** que solo se activa cuando pasa algo importante.

Sobre lo que ya teníamos de la Parte 1, añadimos `scheduler.c/h` y modificamos el kernel para que detecte eventos y avise al scheduler. Implementamos tres algoritmos:

1. **Round Robin** (RR): Quantum fijo con preemption
2. **FIFO**: Sin preemption (run to completion)
3. **Chocolate Caliente** (CH): Quantum adaptativo basado en temperatura

## Arquitectura del Scheduler Event-Driven

Nuestro scheduler solo despierta ante estos eventos:

- **Proceso termina**: El TTL llega a 0 o ejecuta EXIT
- **Quantum expira**: Solo en Round Robin y Chocolate Caliente
- **Nuevo proceso creado**: El generador encola un PCB nuevo

Esto es como funcionan los kernels reales —se activan por interrupciones concretas, no andan mirando cada dos por tres.

```{.mermaid format=pdf}
flowchart TD
    CT[Clock Tick] --> MAC[machine_advance_cycle]
    MAC --> LOOP{Para cada HW Thread}
    LOOP --> EXEC[Ejecutar ciclo]
    EXEC --> DEC[Decrementar TTL]
    DEC --> INC[Incrementar ticks_since_swap]
    INC --> TEMP[Actualizar temperatura]
    TEMP --> EVENT{Detectar eventos}
    EVENT -->|TTL == 0?| SCHED[scheduler_update_thread]
    EVENT -->|Quantum expirado?| SCHED
    EVENT -->|No| LOOP
    SCHED --> LOOP
```

### Detección de Eventos

La función `machine_advance_cycle()` se ejecuta en cada tick del Clock. Recorre todos los hardware threads, decrementando el TTL de los procesos en ejecución e incrementando `ticks_since_swap`:

```c
void machine_advance_cycle(Kernel* kernel) {
    for (cpu in all_cpus) {
        for (core in cpu.cores) {
            for (thread in core.hw_threads) {
                PCB* current = thread->current_pcb;
                
                if (current && current->pid != 0 && current->ttl > 0) {
                    current->ttl--;
                    current->ticks_since_swap++;
                    current->temperature++;
                    
                    // Detectar eventos
                    if (current->ttl == 0) {
                        kernel_signal_scheduler(kernel);
                    } else if (quantum_expired(current, kernel->config)) {
                        kernel_signal_scheduler(kernel);
                    }
                }
            }
        }
    }
}
```

La función `kernel_signal_scheduler()` despierta al hilo del scheduler, que bloquea la máquina (adquiere `machine->mutex`) y procesa todos los hardware threads afectados llamando a `scheduler_update_thread()`.

### Interfaz del Scheduler

```c
void scheduler_update_thread(Kernel* kernel, HWThread* thread,
                            uint32_t cpu, uint32_t core,
                            uint32_t hw_thread);
```

Esta función implementa la lógica de planificación para un hardware thread específico. Según el algoritmo configurado y el estado del proceso actual, decide si:

- Mantener el proceso actual ejecutando
- Desalojar el proceso actual y despachar el siguiente de la cola
- Crear un proceso IDLE si no hay procesos disponibles

## Round Robin

Round Robin es el clásico: quantum fijo y preemption obligatoria. Cada proceso recibe su quantum (por defecto 5 ticks, configurable con `-q`) y cuando se le acaba, fuera. Equitativo al máximo.

### Implementación

```c
case SCHEDULER_ROUND_ROBIN:
    if (current->ticks_since_swap >= kernel->config.rr_quantum) {
        if (!process_queue_is_empty(kernel->process_queue)) {
            // Desalojar proceso actual
            current->state = PROCESS_STATE_READY;
            current->cpu_id = -1;
            current->core_id = -1;
            current->hw_thread_id = -1;
            process_queue_enqueue(kernel->process_queue, current);
            
            // Despachar siguiente proceso
            PCB* next =
                process_queue_dequeue(kernel->process_queue);
            scheduler_dispatch(thread, next, cpu, core, hw_thread);
            
            LOG_INFO(LOG_COMPONENT_SCHEDULER,
                    "(C%u:C%u:T%u) Preemption PID=%u -> PID=%u",
                    cpu, core, hw_thread, current->pid, next->pid);
            
            kernel->context_switches++;
        }
    }
    break;
```

La función `scheduler_dispatch()` actualiza el estado del PCB y lo asigna al hardware thread:

```c
static void scheduler_dispatch(HWThread* thread, PCB* pcb,
                               uint32_t cpu, uint32_t core,
                               uint32_t hw_thread) {
    pcb->state = PROCESS_STATE_RUNNING;
    pcb->cpu_id = cpu;
    pcb->core_id = core;
    pcb->hw_thread_id = hw_thread;
    pcb->ticks_since_swap = 0;
    thread->current_pcb = pcb;
}
```

### Características

**Ventajas**:
- Equidad garantizada: todos los procesos reciben el mismo quantum
- Buen tiempo de respuesta para procesos interactivos
- Simplicidad conceptual

**Desventajas**:
- Overhead de context switches frecuentes
- Quantum pequeño incrementa overhead, quantum grande deteriora tiempo de respuesta
- No considera prioridades ni comportamiento de procesos

### Resultados de Pruebas

Configuración: 1 CPU, 1 core, 1 thread, quantum=4 ticks, generación cada 5 ticks.

```plaintext
[SCH] (0:0:0) Dispatch PID=1 (Reemplazando IDLE) TTL=0
[SCH] (0:0:0) Preemption PID=1 -> PID=2
[SCH] (0:0:0) Preemption PID=2 -> PID=1
[SCH] (0:0:0) Preemption PID=1 -> PID=2
[SCH] (0:0:0) Preemption PID=2 -> PID=3
[SCH] (0:0:0) Preemption PID=3 -> PID=1
```

Observaciones:
- Preemption ocurre exactamente cada 4 ticks (quantum configurado)
- Los procesos rotan en orden FIFO (cola justa)
- Cada cambio de contexto se contabiliza correctamente

## FIFO (First In First Out)

FIFO es lo más simple que hay: los procesos se ejecutan hasta terminar, sin preemption. El `-q` aquí no hace nada.

### Implementación

```c
case SCHEDULER_FIFO:
    // Solo hay scheduling cuando el proceso termina
    if (current->ttl == 0 ||
        current->state == PROCESS_STATE_TERMINATED) {
        pcb_destroy(current);
        thread->current_pcb = NULL;
        
        // Despachar siguiente proceso
        PCB* next = process_queue_dequeue(kernel->process_queue);
        if (next) {
            scheduler_dispatch(thread, next, cpu, core, hw_thread);
            LOG_INFO(LOG_COMPONENT_SCHEDULER,
                    "(C%u:C%u:T%u) Dispatch PID=%u (TTL=%u)",
                    cpu, core, hw_thread, next->pid, next->ttl);
            kernel->context_switches++;
        } else {
            thread->current_pcb = pcb_create_idle();
            LOG_DEBUG(LOG_COMPONENT_SCHEDULER,
                     "(C%u:C%u:T%u) IDLE",
                     cpu, core, hw_thread);
        }
    }
    break;
```

### Características

**Ventajas**:
- Overhead mínimo (sin context switches salvo terminación)
- Orden predecible (estricto FIFO)
- Máxima utilización de CPU

**Desventajas**:
- Procesos largos bloquean a los cortos (convoy effect)
- Tiempo de respuesta terrible para procesos interactivos
- No hay equidad temporal

### Resultados de Pruebas

Configuración: 1 CPU, 1 core, 1 thread, generación cada 5 ticks.

```plaintext
[SCH] (0:0:0) Dispatch PID=1 (Reemplazando IDLE) TTL=42
[SCH] (0:0:0) Process PID=1 terminated
[SCH] (0:0:0) Dispatch PID=2 (TTL=15)
[SCH] (0:0:0) Process PID=2 terminated
[SCH] (0:0:0) Dispatch PID=3 (TTL=88)
```

Observaciones:
- Cada proceso se ejecuta hasta completar su TTL
- Context switch solo al terminar
- Procesos en cola esperan hasta que el actual termine

## Chocolate Caliente

Este es nuestro algoritmo original, y nos gusta bastante. La idea es simple: imagina un chocolate caliente —si está muy caliente, tienes que dar sorbitos pequeños (quantum bajo). Si está frío, puedes dar tragos más grandes (quantum alto). Aplicamos esta metáfora a los procesos usando su "temperatura".

### Modelo de Temperatura

Cada PCB mantiene un campo `temperature` que se actualiza según estas reglas:

```c
// Mientras el proceso ejecuta, se calienta
current->temperature++;  // +1°C por tick ejecutado

// Cuando un proceso espera en cola, se enfría
if (waiting->temperature >= 5) {
    waiting->temperature -= 5;  // -5°C por cada context switch
}
```

### Cálculo del Quantum

El quantum máximo se calcula en función de la temperatura actual:

```c
static uint32_t get_max_quantum_by_temperature(
    uint32_t temperature,
    uint32_t quantum_base) {
    uint32_t base = (quantum_base > 0) ? quantum_base : 5;
    
    if (temperature >= 80)
        return (base * 1) / 5;  // 20% (ardiendo)
    if (temperature >= 60)
        return (base * 2) / 5;  // 40% (muy caliente)
    if (temperature >= 40)
        return (base * 4) / 5;  // 80% (caliente)
    if (temperature >= 20)
        return (base * 6) / 5;  // 120% (templado)
    return base * 2;            // 200% (frío)
}
```

Con `quantum_base=5` (por defecto):

| Temperatura | Quantum | Estado |
|-------------|---------|--------|
| < 20°C      | 10 ticks | Frío |
| 20-39°C     | 6 ticks | Templado |
| 40-59°C     | 4 ticks | Caliente |
| 60-79°C     | 2 ticks | Muy caliente |
| ≥ 80°C      | 1 tick  | Ardiendo |

### Implementación

```c
case SCHEDULER_CHOCOLATE_CALIENTE:
    uint32_t max_quantum = get_max_quantum_by_temperature(
        current->temperature, kernel->config.rr_quantum);
    
    LOG_DEBUG(LOG_COMPONENT_SCHEDULER,
             "PID=%u Temp=%u°C %s Quantum=%u "
             "Ticks=%u/%u TTL=%u",
             current->pid, current->temperature,
             get_temperature_emoji(current->temperature),
             max_quantum, current->ticks_since_swap,
             max_quantum, current->ttl);
    
    if (current->ticks_since_swap >= max_quantum) {
        if (!process_queue_is_empty(kernel->process_queue)) {
            // Desalojar actual
            current->state = PROCESS_STATE_READY;
            current->cpu_id = -1;
            current->core_id = -1;
            current->hw_thread_id = -1;
            process_queue_enqueue(kernel->process_queue, current);
            
            // Despachar siguiente
            PCB* next =
                process_queue_dequeue(kernel->process_queue);
            scheduler_dispatch(thread, next, cpu, core, hw_thread);
            
            LOG_INFO(LOG_COMPONENT_SCHEDULER,
                    "(C%u:C%u:T%u) Preemption "
                    "PID=%u (Temp=%u°C) -> PID=%u (Temp=%u°C)",
                    cpu, core, hw_thread,
                    current->pid, current->temperature,
                    next->pid, next->temperature);
            
            kernel->context_switches++;
            
            // Enfriar procesos en cola
            uint32_t queue_size =
                process_queue_size(kernel->process_queue);
            for (uint32_t i = 0; i < queue_size; i++) {
                PCB* waiting =
                    process_queue_dequeue(kernel->process_queue);
                if (waiting->temperature >= 5) {
                    waiting->temperature -= 5;
                }
                process_queue_enqueue(kernel->process_queue,
                                      waiting);
            }
        }
    }
    break;
```

### Comportamiento

Cuando un proceso empieza a ejecutar (temperatura baja), recibe quantums largos. A medida que consume CPU (temperatura sube), sus quantums se acortan progresivamente. Si es desalojado y espera en cola, se enfría, recuperando quantums más largos al volver a ejecutar.

Este mecanismo favorece a:
- Procesos nuevos o que han esperado mucho (están fríos)
- Procesos interactivos que hacen ráfagas cortas de CPU

Y penaliza a:
- Procesos que monopolizan la CPU (se calientan rápidamente)
- Procesos CPU-bound que raramente se bloquean

### Resultados de Pruebas

Configuración: 1 CPU, 1 core, 1 thread, quantum_base=5, generación cada 5 ticks.

```plaintext
[SCH] PID=1 Temp=4°C Quantum=10 Ticks=4/10 TTL=46
[SCH] PID=1 Temp=10°C Quantum=10 Ticks=10/10 TTL=40
[SCH] (0:0:0) Preemption PID=1 (Temp=10°C) -> PID=2 (Temp=0°C)
[SCH] PID=2 Temp=6°C Quantum=10 Ticks=6/10 TTL=12
[SCH] PID=2 Temp=10°C Quantum=10 Ticks=10/10 TTL=6
[SCH] (0:0:0) Preemption PID=2 (Temp=10°C) -> PID=3 (Temp=0°C)
[SCH] PID=3 Temp=8°C Quantum=10 Ticks=8/10 TTL=80
[SCH] PID=3 Temp=16°C Quantum=10 Ticks=10/10 TTL=72
[SCH] (0:0:0) Preemption PID=3 (Temp=16°C) -> PID=1 (Temp=5°C)
[SCH] PID=1 Temp=6°C Quantum=10 Ticks=1/10 TTL=39
```

Observaciones:
- Procesos nuevos empiezan con temperatura 0°C (quantum máximo)
- La temperatura sube 1°C por tick ejecutado
- Procesos desalojados se enfrían -5°C al esperar en cola
- El quantum se recalcula dinámicamente según temperatura actual

### Análisis Comparativo

| Algoritmo | Context Switches | Tiempo Respuesta | Overhead | Equidad |
|-----------|-----------------|-----------------|----------|---------|
| **FIFO** | Mínimo | Malo (convoy) | Mínimo | Nula |
| **Round Robin** | Alto | Bueno | Alto | Total |
| **Chocolate Caliente** | Adaptativo | Muy bueno | Medio | Proporcional |

Chocolate Caliente encontró un buen balance:
- Menos cambios de contexto que RR cuando hay procesos largos (no fuerza preemption innecesariamente)
- Mejor respuesta que FIFO para procesos cortos (gracias al quantum adaptativo)
- Equidad proporcional al comportamiento —los procesos interactivos salen ganando

## Integración con el Kernel

El scheduler se ejecuta en su propio hilo que se despierta solo cuando se señaliza un evento:

```c
static void* scheduler_thread_func(void* arg) {
    Kernel* kernel = (Kernel*)arg;
    
    while (kernel_is_running(kernel)) {
        // Bloquearse hasta que haya un evento
        kernel_wait_scheduler_signal(kernel);
        
        if (!kernel_is_running(kernel)) break;
        
        // Bloquear la máquina durante scheduling
        machine_lock(kernel->machine);
        
        // Procesar todos los hardware threads
        for (uint32_t cpu = 0; cpu < kernel->config.num_cpus; cpu++) {
            for (uint32_t core = 0;
                 core < kernel->config.num_cores_per_cpu; core++) {
                for (uint32_t thread = 0;
                     thread < kernel->config.num_hw_threads_per_core;
                     thread++) {
                    HWThread* hw_thread = machine_get_thread(
                        kernel->machine, cpu, core, thread);
                    
                    scheduler_update_thread(kernel, hw_thread,
                                           cpu, core, thread);
                }
            }
        }
        
        // Desbloquear la máquina
        machine_unlock(kernel->machine);
    }
    
    return NULL;
}
```

La señalización se hace mediante mutex + condvar:

```c
void kernel_signal_scheduler(Kernel* kernel) {
    pthread_mutex_lock(&kernel->scheduler_mutex);
    kernel->scheduler_signal = 1;
    pthread_cond_signal(&kernel->scheduler_cond);
    pthread_mutex_unlock(&kernel->scheduler_mutex);
}

void kernel_wait_scheduler_signal(Kernel* kernel) {
    pthread_mutex_lock(&kernel->scheduler_mutex);
    while (!kernel->scheduler_signal &&
           kernel_is_running(kernel)) {
        pthread_cond_wait(&kernel->scheduler_cond,
                         &kernel->scheduler_mutex);
    }
    kernel->scheduler_signal = 0;
    pthread_mutex_unlock(&kernel->scheduler_mutex);
}
```

## Decisiones de Diseño

### Event-Driven vs Periódico

Se ha optado por un scheduler event-driven en lugar de uno que se ejecute periódicamente. Esta decisión reduce el overhead de CPU (el scheduler solo trabaja cuando es necesario) y refleja mejor el comportamiento de kernels reales donde el scheduler es invocado por interrupciones específicas (timer interrupt, I/O completion, etc.).

### Temperatura como Métrica

La elección de temperatura como métrica para Chocolate Caliente (en lugar de prioridades estáticas o aging clásico) se ha tomado por su simplicidad conceptual y efectividad práctica. La temperatura captura implícitamente el comportamiento del proceso: procesos CPU-bound se calientan rápido, procesos I/O-bound se mantienen fríos.

### Enfriamiento Proporcional

Los procesos se enfrían -5°C por cada context switch (no por tick en cola). Esta decisión evita que procesos que esperan mucho tiempo recuperen temperatura demasiado rápido, manteniendo un balance entre procesos nuevos y procesos que han esperado.

### Bloqueo Global de Máquina

Durante el scheduling se bloquea toda la máquina (`machine_lock()`). Aunque esto serializa el scheduling y reduce el paralelismo, simplifica enormemente la sincronización y evita race conditions complejas. En un kernel real esto se evitaría con estructuras lock-free o per-CPU runqueues, pero para un simulador didáctico la claridad prima sobre la performance.

\newpage
