# Parte 2: Scheduler y Algoritmos de Planificación

## Introducción

Esta segunda parte mete el scheduler, que es donde la cosa se pone interesante. En vez de hacer un scheduler clásico que se ejecuta periódicamente, he montado uno **completamente event-driven** que solo se activa cuando pasa algo importante.

Sobre lo que ya tenía de la Parte 1, he añadido `scheduler.c/h` y he modificado el kernel para que detecte eventos y avise al scheduler. He implementado tres algoritmos:

1. **Round Robin** (RR): Quantum fijo con preemption
2. **FIFO**: Sin preemption (run to completion)
3. **Chocolate Caliente** (CH): Quantum adaptativo basado en temperatura

## Arquitectura del Scheduler Event-Driven

El scheduler solo despierta ante eventos específicos, no corre periódicamente desperdiciando CPU. Los tres eventos que lo activan son: (1) un proceso agota su TTL o ejecuta EXIT, liberando su hardware thread; (2) el quantum de un proceso expira en algoritmos con preemption (RR y Chocolate Caliente), requiriendo cambio de contexto; (3) el generador crea un proceso nuevo y lo encola, notificando que hay trabajo disponible.

```{.mermaid format=pdf}
flowchart TD
    Start([Cada Tick]) --> MAC[machine_advance_cycle]
    
    MAC --> CheckThreads{Recorrer HW Threads}
    CheckThreads --> E1{TTL = 0?}
    CheckThreads --> E2{Quantum expirado?}
    
    E1 -->|Sí| SetFlag1[event_flag = true]
    E2 -->|Sí| SetFlag2[event_flag = true]
    E1 -->|No| Continue
    E2 -->|No| Continue
    
    SetFlag1 --> Continue
    SetFlag2 --> Continue
    Continue --> MoreThreads{Más threads?}
    MoreThreads -->|Sí| CheckThreads
    MoreThreads -->|No| CheckFlag{event_flag?}
    
    ProcGen[Nuevo Proceso] --> Enqueue[process_queue_enqueue]
    Enqueue --> SetFlag3[event_flag = true]
    SetFlag3 --> CheckFlag
    
    CheckFlag -->|true| Signal[kernel_signal_scheduler]
    CheckFlag -->|false| End([Continuar])
    
    Signal --> Wake[pthread_cond_broadcast]
    Wake --> SchedWake[Scheduler despierta]
    SchedWake --> Update[scheduler_update_thread]
    Update --> End
    
    style E1 fill:#ffe1e1
    style E2 fill:#ffe1e1
    style ProcGen fill:#e1ffe1
    style Wake fill:#e1f5ff
```

Esta arquitectura es como funcionan los kernels reales. El scheduler no anda constantemente preguntando «pasó algo?» sino que duerme hasta que una interrupción concreta lo despierta: timer interrupt (quantum expirado), I/O completion (proceso desbloqueado), o fork/exec (proceso nuevo creado).

La detección ocurre en `machine_advance_cycle()`: al recorrer todos los threads, comprobamos `TTL` $=0$ y `ticks_since_swap` $\geq$ `quantum`. Detectado un evento, seteamos una flag booleana. Al final del ciclo, si alguna flag está activa, llamamos a `kernel_signal_scheduler()` que hace broadcast a la variable de condición del scheduler, despertándolo instantáneamente.

### Detección de Eventos

En cada tick, `machine_advance_cycle()` recorre todos los hardware threads, decrementa TTL e incrementa `ticks_since_swap`. Si detecta `TTL` $=0$ o quantum expirado, llama a `kernel_signal_scheduler()` que despierta al scheduler.

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

Round Robin es el algoritmo más simple con preemption: todos los procesos reciben un quantum idéntico (configurable con `-q`, por defecto 5 ticks) y cuando se les acaba, obligatoriamente van al final de la cola. Equidad total.

### Implementación

El scheduler comprueba `ticks_since_swap` $\geq$ `quantum` en cada evento. Si se cumple y hay procesos en cola, ejecuta preemption: cambia el estado del proceso actual a READY, resetea sus IDs de ubicación a -1, lo encola al final con `process_queue_enqueue()`, y despacha el primero de la cola con `process_queue_dequeue()`.

```{.mermaid format=pdf}
sequenceDiagram
    participant MAC as machine_advance_cycle
    participant PCB as Proceso Actual
    participant S as Scheduler
    participant Q as ProcessQueue
    participant Next as Siguiente Proceso
    participant T as HW Thread
    
    MAC->>PCB: ticks_since_swap++
    MAC->>MAC: Verificar quantum
    Note over MAC: ticks_since_swap >= quantum
    MAC->>S: kernel_signal_scheduler()
    
    S->>S: scheduler_update_thread()
    S->>Q: is_empty()?
    Q-->>S: false (hay procesos)
    
    rect rgb(255, 240, 240)
        Note over S,PCB: PREEMPTION
        S->>PCB: state = READY
        S->>PCB: cpu_id = -1
        S->>PCB: core_id = -1  
        S->>PCB: hw_thread_id = -1
        S->>Q: enqueue(PCB)
    end
    
    rect rgb(240, 255, 240)
        Note over S,Next: DISPATCH
        S->>Q: dequeue()
        Q-->>S: Next
        S->>Next: state = RUNNING
        S->>Next: cpu_id, core_id, hw_thread_id
        S->>Next: ticks_since_swap = 0
        S->>T: current_pcb = Next
        
        alt Proceso con programa cargado
            S->>T: mmu_set_ptbr(Next->mm.pgb)
            S->>T: pc = Next->mm.code_start
        end
    end
```

La función `scheduler_dispatch()` hace el trabajo pesado del context switch: setea el estado del PCB a RUNNING, asigna los IDs de ubicación (cpu_id, core_id, hw_thread_id), resetea `ticks_since_swap` a 0, y si el proceso tiene programa cargado, configura la MMU del thread actualizando el PTBR y el PC. Todo atómico, protegido por el mutex de la máquina.

Si la cola está vacía al expirar el quantum, el proceso continúa ejecutando. Esta decisión evita overhead innecesario: ¿para qué hacer preemption si no hay nadie esperando? El proceso sigue hasta que termine o llegue alguien nuevo.

### Características

**Ventajas:**

- Equidad absoluta: todos reciben el mismo quantum
- Buen tiempo de respuesta para procesos interactivos
- Simplicidad conceptual

**Desventajas:**

- Overhead elevado por context switches frecuentes
- Dilema del quantum: valores pequeños mejoran respuesta pero aumentan overhead; valores grandes reducen overhead pero deterioran interactividad
- No considera prioridades ni tipo de proceso (I/O-bound vs CPU-bound)

**Resultados:** El quantum configurado (5 ticks por defecto) produce preemption exacta cuando expira, los procesos rotan estrictamente en orden FIFO, y los context switches se contabilizan correctamente.

## FIFO (First In First Out)

FIFO es lo más simple que hay: los procesos se ejecutan hasta terminar, sin preemption. El `-q` aquí no hace nada.

### Implementación

Solo planifica cuando el proceso termina (TTL=0). Destruye el PCB actual, despacha el siguiente de la cola, o crea un IDLE si no hay procesos.

### Características

**Ventajas:**

- Overhead mínimo (sin context switches salvo terminación)
- Orden totalmente predecible (estricto first-come-first-served)
- Maximiza utilización de CPU

**Desventajas:**

- Convoy effect severo: procesos largos bloquean indefinidamente a los cortos
- Tiempo de respuesta desastroso para procesos interactivos
- Equidad temporal inexistente

**Resultados:** Cada proceso ejecuta hasta agotar su TTL completo, context switch solo al terminar. Los procesos en cola esperan hasta que el actual termine, convoy effect claramente visible en las trazas.

## Chocolate Caliente

Este es mi algoritmo original, y me gusta bastante. La idea es simple: imagina un chocolate caliente —si está muy caliente, tienes que dar sorbitos pequeños (quantum bajo). Si está frío, puedes dar tragos más grandes (quantum alto). He aplicado esta metáfora a los procesos usando su "temperatura".

### Modelo de Temperatura

Cada proceso tiene una temperatura que evoluciona dinámicamente según dos reglas:

**Calentamiento (+8°C por tick):**

- Ocurre mientras el proceso ejecuta en un hardware thread
- Es lineal y acumulativo, con límite máximo de 100°C
- Captura cuánto CPU ha consumido recientemente

**Enfriamiento (-5°C por context switch):**

- Ocurre **solo cuando el proceso es desalojado**, NO mientras espera en cola
- Es un evento discreto: -5°C en el momento exacto del context switch
- Implementación: tras cada preemption, el scheduler recorre toda la ProcessQueue desencolando y reencolando cada PCB, restándole 5°C a su temperatura (con mínimo 0°C)
- Este recorrido O(n) es caro pero esencial para que procesos esperando recuperen quantums largos al ser redespachados

**Traducción temperatura → quantum:**

La temperatura se mapea a quantum mediante cinco umbrales:

- **< 20°C** (frío): 200% del quantum base
- **20-39°C** (templado): 120% del quantum base
- **40-59°C** (caliente): 80% del quantum base
- **60-79°C** (muy caliente): 40% del quantum base
- **≥ 80°C** (ardiendo): 20% del quantum base

```{.mermaid format=pdf}
flowchart TD
    Start([Dispatch temp=0]) --> Frio
    
    Frio["Frio: temp < 20\nQuantum = 10 ticks"]
    Templado["Templado: 20-39\nQuantum = 6 ticks"]
    Caliente["Caliente: 40-59\nQuantum = 4 ticks"]
    MuyCaliente["Muy Caliente: 60-79\nQuantum = 2 ticks"]
    Ardiendo["Ardiendo: temp >= 80\nQuantum = 1 tick"]
    
    Frio -->|+8C por tick| Templado
    Templado -->|+8C por tick| Caliente
    Caliente -->|+8C por tick| MuyCaliente
    MuyCaliente -->|+8C por tick| Ardiendo
    
    Frio -->|Preemption -5C| Cola
    Templado -->|Preemption -5C| Cola
    Caliente -->|Preemption -5C| Cola
    MuyCaliente -->|Preemption -5C| Cola
    Ardiendo -->|Preemption -5C| Cola
    
    Cola["En Cola\n(sin cambio temp)"]
    
    Cola -->|Re-dispatch| Frio
    Cola -->|Re-dispatch| Templado
    Cola -->|Re-dispatch| Caliente
    Cola -->|Re-dispatch| MuyCaliente
    Cola -->|Re-dispatch| Ardiendo
    
    style Frio fill:#e1f5ff
    style Templado fill:#e1ffe1
    style Caliente fill:#fff4e1
    style MuyCaliente fill:#ffe1e1
    style Ardiendo fill:#ff9999
    style Cola fill:#f0f0f0
```

**Efecto neto:** Procesos nuevos o que esperaron mucho (fríos) reciben quantums generosos, mientras que procesos que monopolizan CPU (calientes) reciben quantums cortos. Este comportamiento favorece automáticamente procesos interactivos (I/O-bound) sobre procesos CPU-bound.

### Cálculo del Quantum

Con `quantum_base=5` (por defecto):

| Temperatura | Quantum | Factor | Estado |
|-------------|---------|--------|---------|
| $< 20°C$    | 10 ticks | 200%  | Frío |
| $20-39°C$   | 6 ticks  | 120%  | Templado |
| $40-59°C$   | 4 ticks  | 80%   | Caliente |
| $60-79°C$   | 2 ticks  | 40%   | Muy caliente |
| $\geq 80°C$ | 1 tick   | 20%   | Ardiendo |

**Ejemplo de evolución**

```plaintext
Tick  3  SCH (0:0:0)  Dispatch PID=1 (Reemplazando IDLE) TTL=0
Tick  6  SCH (0:0:0)  PID=1 Temp=24°C Quantum=6 Ticks=3/6 TTL=0
Tick  8  SCH (0:0:0)  PID=1 Temp=40°C Quantum=4 Ticks=5/4 TTL=0
Tick  8  SCH (0:0:0)  Context switch PID=1 (enfriándose) -> PID=2 (quantum=10)
Tick  9  SCH (0:0:0)  PID=2 Temp=8°C Quantum=10 Ticks=1/10 TTL=0
Tick 12  SCH (0:0:0)  PID=2 Temp=32°C Quantum=6 Ticks=4/6 TTL=0
Tick 13  SCH (0:0:0)  PID=2 Temp=40°C Quantum=4 Ticks=5/4 TTL=0
Tick 13  SCH (0:0:0)  Context switch PID=2 (enfriándose) -> PID=1 (quantum=6)
Tick 15  SCH (0:0:0)  PID=1 Temp=51°C Quantum=4 Ticks=2/4 TTL=0
Tick 17  SCH (0:0:0)  PID=1 Temp=67°C Quantum=2 Ticks=4/2 TTL=0
Tick 17  SCH (0:0:0)  Context switch PID=1 (enfriándose) -> PID=3 (quantum=10)
Tick 18  SCH (0:0:0)  PID=3 Temp=8°C Quantum=10 Ticks=1/10 TTL=0
```

Se observa claramente: PID=1 arranca a 0°C con quantum=10, ejecuta 3 ticks y sube a 24°C (quantum→6), otros 2 ticks más llega a 40°C (quantum→4) y hace preemption. Al volver después del context switch, tiene 35°C (-5°C enfriamiento) con quantum=6, vuelve a ejecutar y sube a 51°C, luego 67°C con quantum=2.

La función `get_max_quantum_by_temperature()` implementa la tabla:

```c
if (temp >= 80) return (base * 1) / 5;  // 20%
if (temp >= 60) return (base * 2) / 5;  // 40%
if (temp >= 40) return (base * 4) / 5;  // 80%
if (temp >= 20) return (base * 6) / 5;  // 120%
return base * 2;                         // 200%
```

### Implementación

Chocolate Caliente extiende Round Robin añadiendo cálculo dinámico del quantum. En cada evento, calcula el quantum máximo permitido para el proceso actual según su temperatura usando `get_max_quantum_by_temperature()`. Luego compara `ticks_since_swap` con ese quantum calculado.

Si se agota y hay cola no vacía, ejecuta preemption igual que RR: desaloja actual, encola, despacha siguiente. La diferencia está en el paso adicional de enfriamiento: tras despachar el siguiente proceso, recorre toda la ProcessQueue desencolando y reencolando cada PCB, restándole 5°C a su temperatura (con mínimo 0).

Este enfriamiento global tras cada context switch es caro (O(n) donde n=tamaño de cola), pero crítico para el comportamiento del algoritmo. Sin él, procesos esperando no recuperarían quantums largos. La implementación usa dequeue/enqueue para recorrer, manteniendo el orden FIFO.

La temperatura también se incrementa en `machine_advance_cycle()` para procesos en RUNNING: +8°C por tick. Esto significa que el calentamiento es automático (no requiere lógica del scheduler), mientras que el enfriamiento es explícito (requiere acción del scheduler).

### Comportamiento

Cuando un proceso empieza a ejecutar (temperatura baja), recibe quantums largos. A medida que consume CPU (temperatura sube), sus quantums se acortan progresivamente. Si es desalojado y espera en cola, se enfría, recuperando quantums más largos al volver a ejecutar.

Este mecanismo favorece a:
- Procesos nuevos o que han esperado mucho (están fríos)
- Procesos interactivos que hacen ráfagas cortas de CPU

Y penaliza a:
- Procesos que monopolizan la CPU (se calientan rápidamente)
- Procesos CPU-bound que raramente se bloquean

### Resultados de Pruebas

Quantum adaptativo funciona: procesos fríos reciben 10 ticks, procesos ardiendo solo 1 tick. La temperatura incrementa durante ejecución y disminuye (-5°C) al esperar en cola. Balance entre equidad y eficiencia visible en las trazas.

### Análisis Comparativo

| Algoritmo | Context Switches | Tiempo Respuesta | Overhead | Equidad |
|-----------|-----------------|-----------------|----------|---------|
| **FIFO** | Mínimo | Malo (convoy) | Mínimo | Nula |
| **Round Robin** | Alto | Bueno | Alto | Total |
| **Chocolate Caliente** | Adaptativo | Muy bueno | Medio | Proporcional |

Chocolate Caliente ha encontrado un buen balance:
- Menos cambios de contexto que RR cuando hay procesos largos (no fuerza preemption innecesariamente)
- Mejor respuesta que FIFO para procesos cortos (gracias al quantum adaptativo)
- Equidad proporcional al comportamiento —los procesos interactivos salen ganando

## Integración con el Kernel

### Hilo del Scheduler

El scheduler implementa el patrón productor-consumidor clásico. El hilo `scheduler_thread` arranca en `kernel_start()` y entra inmediatamente en un bucle donde se bloquea en `pthread_cond_wait()` sobre `scheduler_cond`. Ahí duerme sin consumir CPU hasta que alguien lo despierte.

Los productores son `machine_advance_cycle()` (detecta quantum expirado o `TTL` $=0$) y `process_gen_thread` (crea proceso nuevo). Ambos llaman a `kernel_signal_scheduler()` que hace `pthread_cond_signal()` o `pthread_cond_broadcast()`, despertando al scheduler instantáneamente.

Al despertar, el scheduler adquiere el mutex de la máquina con `machine_lock()`, bloqueando toda modificación concurrente. Recorre todos los hardware threads llamando a `scheduler_update_thread()` para cada uno, que aplica la lógica del algoritmo configurado (FIFO/RR/CH). Tras procesar todos los threads, libera el mutex con `machine_unlock()` y vuelve a bloquearse esperando el próximo evento.

Este diseño serializa completamente el scheduling: solo un scheduler puede correr a la vez, y mientras corre, nadie puede modificar la máquina. Esto simplifica enormemente la sincronización eliminando race conditions complejas. En kernels reales se usan estructuras lock-free y per-CPU runqueues, pero para un simulador didáctico la claridad prima sobre la performance.

## Decisiones de Diseño

### Event-Driven vs Periódico

He optado por un scheduler event-driven en lugar de uno que se ejecute periódicamente. Esto reduce el overhead de CPU (el scheduler solo trabaja cuando hace falta) y refleja mejor el comportamiento de kernels reales donde el scheduler es invocado por interrupciones específicas (timer interrupt, I/O completion, etc.).

### Temperatura como Métrica

He elegido temperatura como métrica para Chocolate Caliente (en lugar de prioridades estáticas o aging clásico) por su simplicidad conceptual y efectividad práctica. La temperatura captura implícitamente el comportamiento del proceso: procesos CPU-bound se calientan rápido, procesos I/O-bound se mantienen fríos.

### Enfriamiento Proporcional

Los procesos se enfrían -5°C por cada context switch (no por tick en cola). Esta decisión evita que procesos que esperan mucho tiempo recuperen temperatura demasiado rápido, manteniendo un balance entre procesos nuevos y procesos que han esperado.

### Bloqueo Global de Máquina

Durante el scheduling se bloquea toda la máquina (`machine_lock()`). Aunque esto serializa el scheduling y reduce el paralelismo, simplifica enormemente la sincronización y evita race conditions complejas. En un kernel real esto se evitaría con estructuras lock-free o per-CPU runqueues, pero para un simulador didáctico la claridad prima sobre la performance.

\newpage
