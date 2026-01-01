# Parte 1: Arquitectura Base del Kernel

## Introducción

En esta primera parte he puesto los cimientos del simulador. He implementado el Clock que genera los pulsos de tiempo, un Timer configurable para interrupciones periódicas, y los componentes básicos de gestión de procesos: la cola (ProcessQueue), los PCBs y un generador automático de procesos.

He modelado el hardware con el componente Machine, que representa la jerarquía de CPUs, cores y hardware threads. Todo funciona con múltiples hilos usando primitivas POSIX (`pthread_mutex_t`, `pthread_cond_t`).

Esta parte se centra solo en la infraestructura base —el scheduler llega en la Parte 2 y la memoria virtual en la Parte 3.

## Diseño Global

El sistema sigue una arquitectura productora-consumidora con sincronización mediante mutex y variables de condición. El Clock actúa como maestro del tiempo, generando ticks periódicos que propagan eventos a través de todo el sistema.

```{.mermaid format=pdf}
flowchart TD
    Clock[Clock Thread] -->|Tick| Timer1[Timer Scheduler]
    Clock -->|Tick| Timer2[Timer Process Gen]
    Clock -->|Tick| Machine[Machine Advance]
    
    Timer2 -->|Interrupción| ProcGen[Process Generator]
    ProcGen -->|pcb_create| PCB[Nuevo PCB]
    PCB -->|enqueue| Queue[ProcessQueue]
    
    Timer1 -->|Interrupción| Sched[Scheduler Thread]
    Machine -->|Eventos detectados| Sched
    
    Queue -.->|dequeue| Sched
    Sched -->|dispatch| HWThread[Hardware Threads]
    
    style Clock fill:#e1f5ff
    style Queue fill:#fff4e1
    style Sched fill:#ffe1e1
    style ProcGen fill:#e1ffe1
```

Los componentes se organizan jerárquicamente: el Clock alimenta Timers configurables que generan interrupciones. Estas interrupciones despiertan al Process Generator, que crea PCBs y los encola. Los procesos encolados esperan a ser despachados por el scheduler (Parte 2) a alguno de los hardware threads disponibles en la Machine.

La Machine representa la topología hardware real: CPUs que contienen cores, y cores que contienen hardware threads. Cada thread ejecuta un proceso (o IDLE si no hay trabajo). Esta jerarquía refleja arquitecturas multinúcleo modernas como Intel Core o AMD Ryzen, donde cada núcleo físico puede tener múltiples threads lógicos vía hyperthreading.

La sincronización es el eje central: todos los componentes cooperan mediante primitivas POSIX sin race conditions. El Clock hace broadcast para despertar múltiples consumidores, mientras que las colas usan mutex para acceso exclusivo.

## Implementación de Componentes

### Clock

El Clock es el corazón del sistema, el que produce los ticks de tiempo. Cada tick despierta a los consumidores que están esperando. Lo he implementado completamente thread-safe para que múltiples hilos puedan consumir ticks sin problemas.

La función central `clock_pulse()` incrementa el contador de ticks y despierta a todos los hilos bloqueados en `clock_wait_tick()` mediante broadcast. La variable `last` permite a los consumidores detectar si han perdido algún tick (cuando el sistema está muy cargado), comparándola con el tick actual. Además, `clock_get_tick()` permite consultas no bloqueantes y `clock_shutdown()` finaliza ordenadamente todos los consumidores.

**Sincronización:**

La sincronización del Clock usa el patrón productor-consumidor clásico con un mutex protegiendo el estado compartido y una variable de condición para las notificaciones. Cuando `clock_pulse()` incrementa el contador de ticks, hace broadcast con `pthread_cond_broadcast()` despertando a todos los consumidores bloqueados simultáneamente.

```{.mermaid format=pdf}
sequenceDiagram
    participant CT as Clock Thread
    participant M as Mutex + Condvar
    participant T1 as Timer 1
    participant T2 as Timer 2
    participant PG as Process Gen
    
    T1->>M: clock_wait_tick(last_tick)
    Note over T1: Bloquea en condvar
    T2->>M: clock_wait_tick(last_tick)
    Note over T2: Bloquea en condvar
    PG->>M: clock_wait_tick(last_tick)
    Note over PG: Bloquea en condvar
    
    CT->>M: pthread_mutex_lock()
    CT->>M: tick_count++
    CT->>M: pthread_cond_broadcast()
    Note over M: Despertar TODOS los consumidores
    CT->>M: pthread_mutex_unlock()
    
    M-->>T1: Despertar
    M-->>T2: Despertar
    M-->>PG: Despertar
    
    T1->>T1: Procesar tick
    T2->>T2: Procesar tick
    PG->>PG: Procesar tick
```

Los consumidores llaman a `clock_wait_tick()` pasando su último tick conocido. La función detecta automáticamente si se han perdido ticks (por sobrecarga del sistema o latencia de scheduling) comparando el tick actual con el último procesado. Esto es importante para saber si el sistema se ha retrasado.

El shutdown usa una flag `is_running` que despierta a todos los hilos en espera sin incrementar el tick, dejándoles terminar ordenadamente.

### Timer

El Timer cuenta ticks del Clock y genera interrupciones cuando llega al periodo configurado. Mantiene un contador interno (`tick_count`) que se incrementa con cada llamada a `churros_timer_tick()` desde el Clock. Al alcanzar el `period` configurado, señaliza la interrupción mediante `pthread_cond_broadcast()` y reinicia el contador a cero.

Los consumidores bloquean en `churros_timer_wait_interrupt()` hasta que el timer alcanza su periodo. Internamente usa mutex + condvar para sincronización thread-safe, igual que el Clock. La flag `interrupt_pending` evita interrupciones perdidas si el consumidor tarda en procesar. Además, mantiene estadísticas con `interrupts_generated` para debugging.

### Process Control Block (PCB)

Cada proceso tiene su PCB, donde guardamos su estado, identificadores, TTL y métricas de ejecución. Nada fancy, pero contiene todo lo necesario.

#### Estructura

```c
typedef struct {
    uint32_t pid;          /* Process ID */
    uint32_t ttl;          /* Time To Live en ticks */
} PCB;
```

En esta primera parte, el PCB es minimalista: solo contiene el PID único que identifica al proceso y el TTL (Time To Live) que marca cuántos ticks le quedan de vida. Cada tick que el proceso ejecuta decrementa su TTL en 1, y cuando llega a 0, el proceso termina.

**Nota**: En la Parte 2 se extiende el PCB añadiendo estado (`ProcessState` con valores NEW, READY, RUNNING, TERMINATED), campos de ubicación (`cpu_id`, `core_id`, `hw_thread_id` inicializados a -1 si no asignado), contador de ticks desde último swap (`ticks_since_swap`), y temperatura para el algoritmo Chocolate Caliente.

```{.mermaid format=pdf}
stateDiagram-v2
    [*] --> NEW: pcb_create()
    
    NEW --> TERMINATED: TTL=0 (v1)
    NEW --> READY: enqueue (v2+)
    
    READY --> RUNNING: dispatch (v2+)
    RUNNING --> READY: preemption (v2+)
    RUNNING --> TERMINATED: TTL=0 (v2+), EXIT (v3)
    
    TERMINATED --> [*]: pcb_destroy()
    
    note right of NEW
        v1: Solo PID y TTL
        Sin ProcessState enum
    end note
    
    note right of RUNNING
        v2+: Añade ProcessState
        + cpu/core/thread IDs
        + ticks_since_swap
        + temperature
    end note
```

#### Interfaz

```c
PCB* pcb_create(uint32_t pid);
void pcb_destroy(PCB* pcb);
```

La función `pcb_create()` toma el PID y genera internamente un TTL aleatorio entre 1 y 100 ticks. La función `pcb_create_idle()` crea procesos IDLE (PID=0, TTL=0 que se trata como especial sin decrementar) que se usan cuando no hay trabajo disponible.

### Process Queue

La cola implementa FIFO thread-safe clásico con lista enlazada: punteros `head` y `tail` para enqueue/dequeue O(1), contador `size`, y mutex protegiendo todo. Las operaciones `process_queue_enqueue()` inserta al final, `process_queue_dequeue()` extrae del principio, y ambas adquieren el mutex garantizando thread-safety para múltiples productores (Process Generator) y consumidores (Scheduler) concurrentes. `process_queue_is_empty()` y `process_queue_size()` permiten consultas rápidas del estado.

### Process Generator

El Process Generator crea procesos automáticamente usando un Timer dedicado. Cada vez que el Timer genera una interrupción (configurable con `-g`, por defecto cada 10 ticks), el generador crea un PCB nuevo con:

1. **PID único**: Obtenido de `kernel_get_next_pid()` que usa un contador atómico protegido por mutex
2. **TTL aleatorio**: Tiempo de vida entre 1 y 100 ticks generado con `rand()`
3. **Estado inicial**: NEW, listo para ser encolado

El generador ejecuta en su propio hilo (`process_generator_thread_func`) bloqueando en `churros_timer_wait_interrupt()` hasta que el Timer lo despierta. Al despertar, crea el PCB con `pcb_create()`, lo encola con `process_queue_enqueue()` y registra el evento en los logs. Este diseño desacopla completamente la generación de procesos del Clock principal.

#### Sincronización

El Process Generator usa el patrón productor clásico: genera PCBs y los deposita en la cola compartida. El Timer actúa como regulador temporal, convirtiendo los pulsos del Clock en interrupciones espaciadas según el periodo configurado. Esto permite controlar la tasa de llegada de procesos independientemente de la velocidad del reloj.

La sincronización con la cola es automática gracias al mutex interno de ProcessQueue —el generador simplemente llama a `enqueue()` sin preocuparse de otros productores o consumidores concurrentes.

### Machine

El componente Machine modela la arquitectura hardware jerárquica: CPUs, cores y hardware threads.

#### Estructuras

```{.mermaid format=pdf}
classDiagram
    class Machine {
        +CPU* cpus
        +uint32_t num_cpus
        +uint32_t total_cores
        +uint32_t total_hw_threads
        +pthread_mutex_t mutex
    }
    
    class CPU {
        +Core* cores
        +uint32_t num_cores
    }
    
    class Core {
        +HWThread* hw_threads
        +uint32_t num_hw_threads
    }
    
    class HWThread {
        +PCB* current_pcb
    }
    
    Machine "1" *-- "n" CPU
    CPU "1" *-- "n" Core
    Core "1" *-- "n" HWThread
    HWThread "1" --> "0..1" PCB
```

#### Interfaz

```c
Machine* machine_create(uint32_t num_cpus, uint32_t num_cores_per_cpu,
                        uint32_t num_hw_threads_per_core);
void machine_destroy(Machine* machine);
HWThread* machine_get_thread(Machine* machine, uint32_t cpu,
                             uint32_t core, uint32_t thread);
void machine_lock(Machine* machine);
void machine_unlock(Machine* machine);
```

La función `machine_create()` reserva memoria para toda la jerarquía y inicializa cada hardware thread con un proceso IDLE. Los métodos `machine_lock()` y `machine_unlock()` se utilizan para garantizar acceso exclusivo durante operaciones críticas (como el scheduling).

### Process Generator

Un hilo que despierta con interrupciones del Timer, genera PIDs únicos y crea procesos con TTL aleatorio (1-100 ticks). El ciclo de operación:

1. Bloquea en `timer_wait_interrupt()` hasta que el timer alcanza su periodo
2. Obtiene PID único con `kernel_allocate_pid()` (contador global protegido por mutex)
3. Genera TTL aleatorio con `rand()` (rango 1-100 ticks)
4. Crea el PCB con `pcb_create()`
5. Lo encola con `process_queue_enqueue()`
6. Señaliza al scheduler con `kernel_signal_scheduler()`

El periodo del timer es configurable: periodos cortos generan alta carga, periodos largos baja carga. Así podemos experimentar con diferentes escenarios sin recompilar.

### Kernel

El kernel orquesta cuatro hilos principales que colaboran mediante sincronización explícita:

- **clock_thread**: Genera ticks periódicos (`usleep()`) y despierta consumidores con broadcast
- **procgen_timer_thread**: Cuenta ticks e interrumpe al generador de procesos
- **process_gen_thread**: Crea PCBs cuando es interrumpido por su timer
- **scheduler_thread**: Bloquea en variable de condición hasta que eventos lo despiertan (Parte 2)

La secuencia de inicio es crítica: crear estructuras (Machine, ProcessQueue, Timers) $\rightarrow$ lanzar hilos (`pthread_create()`) en orden de dependencias $\rightarrow$ esperar inicialización. El shutdown también: setear `is_running=false` $\rightarrow$ despertar hilos bloqueados (broadcast/interrupciones) $\rightarrow$ `pthread_join()` para cada hilo. Este protocolo evita deadlocks.

### Ciclo de Máquina

La función `machine_advance_cycle()` es el corazón del simulador, invocada en cada tick del Clock. Recorre todos los hardware threads de la jerarquía (CPUs $\rightarrow$ Cores $\rightarrow$ Threads) en orden, procesando cada PCB activo.

**Procesamiento por tipo de proceso:**

- **Sin programa** (`is_loaded=false`): Decrementa TTL, aumenta temperatura y `ticks_since_swap`
- **Con programa** (`is_loaded=true`): Ejecuta ciclo fetch-decode-execute completo (Parte 3)

**Detección de eventos:**

Durante el recorrido, detecta dos eventos críticos:

1. **Terminación**: Procesos con TTL=0 (o que ejecutaron EXIT en v3)
2. **Quantum expirado**: Procesos que alcanzaron su quantum (solo RR y Chocolate Caliente)

Al detectar eventos, setea flags booleanas (`process_terminated_detected`, `quantum_expired_detected`). Al final del ciclo, si alguna flag está activa, señaliza al scheduler con `kernel_signal_scheduler()`.

**Optimización batch:** Este diseño minimiza señalizaciones. En lugar de despertar al scheduler por cada thread que termina o expira quantum, acumulamos todos los eventos de un tick y señalizamos una sola vez al final, reduciendo overhead.

## Resultados de Pruebas

Los tests validan el funcionamiento completo de la infraestructura base. El Clock genera ticks consistentemente según el intervalo configurado (100ms por defecto), los Timers interrumpen exactamente según su periodo configurado, y el Process Generator crea PIDs secuenciales con TTLs aleatorios entre 1-100 ticks. La ProcessQueue funciona correctamente sin race conditions bajo alta concurrencia (múltiples productores/consumidores). Toda la sincronización multihilo (mutex + condvar) opera sin deadlocks ni condiciones de carrera.

## Limitaciones de la Parte 1

En esta primera entrega, el **scheduler no implementa planificación real**. Cuando un Timer activa el hilo del scheduler, este simplemente registra el evento en los logs pero no asigna procesos a hardware threads. Los procesos se generan y encolan correctamente, pero no se despachan.

Esto es intencional: la Parte 1 se centra exclusivamente en la infraestructura base (reloj, timers, generación de procesos, sincronización). La planificación propiamente dicha (Round Robin, FIFO, Chocolate Caliente) llega en la Parte 2, donde añadimos `scheduler.c` con lógica event-driven completa.

## Decisiones de Diseño

### Separación de Timers

Usamos dos timers independientes (uno para el scheduler, otro para el generador de procesos) en lugar de uno compartido. Esto permite configurar periodos diferentes para cada componente, lo que facilita experimentar con distintas cargas del sistema.

### Procesos IDLE

Cada hardware thread se inicializa con un proceso IDLE (PID=0, TTL=0). Los procesos IDLE actúan como placeholder cuando no hay procesos reales disponibles para ejecutar, evitando que los threads queden sin trabajo asignado. El TTL=0 se trata de forma especial: no se decrementa, permitiendo que el IDLE permanezca indefinidamente.

### Sincronización Mutex + Condvar

Optamos por el patrón clásico de mutex + variable de condición en lugar de alternativas como semáforos o spin-locks. Es más eficiente en términos de CPU (los hilos duermen en lugar de hacer busy-waiting) y es el estándar POSIX más portable.

### TTL Aleatorio

Los procesos se generan con TTL aleatorio entre 1 y 100 ticks. Así simulamos cargas de trabajo heterogéneas, donde algunos procesos son cortos (interactivos) y otros son largos (batch), validando mejor el comportamiento de los algoritmos de scheduling en la Parte 2.

\newpage