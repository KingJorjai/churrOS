# Parte 1: Arquitectura Base del Kernel

## Introducción

En esta primera parte pusimos los cimientos del simulador. Implementamos el Clock que genera los pulsos de tiempo, un Timer configurable para interrupciones periódicas, y los componentes básicos de gestión de procesos: la cola (ProcessQueue), los PCBs y un generador automático de procesos.

Modelamos el hardware con el componente Machine, que representa la jerarquía de CPUs, cores y hardware threads. Todo funciona con múltiples hilos usando primitivas POSIX (`pthread_mutex_t`, `pthread_cond_t`).

Esta parte se centra solo en la infraestructura base —el scheduler llega en la Parte 2 y la memoria virtual en la Parte 3.

## Diseño Global

El diseño sigue un patrón event-driven con componentes bien delimitados. La siguiente figura muestra la arquitectura de la Parte 1:

```{.mermaid format=pdf}
graph TD
    CLK[Clock] --> TMR[Timer]
    TMR --> PG[Process Generator]
    PG --> MQ[Process Queue]
    MQ --> MCH[Machine]
    MCH --> CPU[CPU]
    CPU --> CORE[Core]
    CORE --> HWT["HW Thread (PCB)"]
```

El **Clock** genera pulsos de tiempo (ticks) a intervalos regulares. El **Timer** consume estos ticks y genera interrupciones cuando se alcanza su periodo configurado. El **Process Generator** se despierta con las interrupciones del Timer y crea nuevos procesos, asignándoles un PID único y un TTL (time-to-live) aleatorio. Los procesos creados se encolan en la **ProcessQueue**, donde esperan a ser despachados a algún hardware thread disponible.

La **Machine** modela la jerarquía hardware: cada CPU contiene cores, y cada core contiene hardware threads. Cada hardware thread puede ejecutar un proceso (PCB) o estar en estado IDLE.

## Implementación de Componentes

### Clock

El Clock es el corazón del sistema, el que produce los ticks de tiempo. Cada tick despierta a los consumidores que están esperando. Lo implementamos completamente thread-safe para que múltiples hilos puedan consumir ticks sin problemas.

#### Interfaz

```c
void clock_init(void);
void clock_destroy(void);
void clock_pulse(void);
unsigned long clock_wait_tick(unsigned long *last);
unsigned long clock_get_tick(void);
void clock_shutdown(void);
```

La función `clock_pulse()` incrementa el contador de ticks y despierta a todos los hilos bloqueados en `clock_wait_tick()`. La variable `last` permite a los consumidores detectar si han perdido algún tick (cuando el sistema está muy cargado).

#### Sincronización

Usamos un mutex global y una variable de condición —nada del otro mundo, pero efectivo:

```c
static unsigned long current_tick = 0;
static pthread_mutex_t tick_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tick_cond = PTHREAD_COND_INITIALIZER;
static int shutdown_flag = 0;
```

Cuando `clock_pulse()` se ejecuta, se adquiere el mutex, se incrementa `current_tick` y se hace broadcast a todos los hilos esperando en `tick_cond`. Los consumidores que llaman a `clock_wait_tick()` se bloquean en la variable de condición hasta recibir la señal.

### Timer

El Timer cuenta ticks del Clock y genera interrupciones cuando llega al periodo configurado. Bastante simple: mantiene un contador interno que va incrementándose con cada tick, y al alcanzar el límite señaliza la interrupción y se reinicia.

#### Estructura

```c
typedef struct {
    uint32_t period;
    uint32_t tick_count;
    uint32_t interrupts_generated;
    pthread_mutex_t mutex;
    pthread_cond_t interrupt_cond;
    int interrupt_pending;
} Timer;
```

#### Interfaz

```c
Timer* churros_timer_create(uint32_t period);
void churros_timer_destroy(Timer* timer);
void churros_timer_tick(Timer* timer);
void churros_timer_wait_interrupt(Timer* timer);
int churros_timer_check_interrupt(Timer* timer);
uint32_t churros_timer_get_generated(Timer* timer);
void churros_timer_wake(Timer* timer);
```

El hilo del Clock invoca `churros_timer_tick()` en cada pulso. Los consumidores bloquean en `churros_timer_wait_interrupt()` y se despiertan cuando `tick_count` alcanza `period`.

#### Funcionamiento

```c
void churros_timer_tick(Timer* timer) {
    pthread_mutex_lock(&timer->mutex);
    timer->tick_count++;
    
    if (timer->tick_count >= timer->period) {
        timer->tick_count = 0;
        timer->interrupt_pending = 1;
        timer->interrupts_generated++;
        pthread_cond_broadcast(&timer->interrupt_cond);
    }
    
    pthread_mutex_unlock(&timer->mutex);
}
```

### Process Control Block (PCB)

Cada proceso tiene su PCB, donde guardamos su estado, identificadores, TTL y métricas de ejecución. Nada fancy, pero contiene todo lo necesario.

#### Estructura

```c
typedef enum {
    PROCESS_STATE_NEW,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_TERMINATED
} ProcessState;

typedef struct PCB {
    uint32_t pid;
    ProcessState state;
    uint32_t ttl;
    int32_t cpu_id;
    int32_t core_id;
    int32_t hw_thread_id;
    uint32_t ticks_since_swap;
    uint32_t temperature;
} PCB;
```

El campo `ttl` (time-to-live) representa los ticks de CPU restantes antes de que el proceso termine. Los campos `cpu_id`, `core_id` y `hw_thread_id` indican dónde está ejecutando el proceso (-1 si no está asignado). El campo `ticks_since_swap` cuenta los ticks desde el último context switch, y `temperature` se utiliza en la Parte 2 para el algoritmo Chocolate Caliente.

#### Interfaz

```c
PCB* pcb_create(uint32_t pid, uint32_t ttl);
PCB* pcb_create_idle(void);
void pcb_destroy(PCB* pcb);
```

La función `pcb_create_idle()` crea un proceso especial con PID=0 que se ejecuta cuando no hay procesos reales disponibles.

### Process Queue

La cola de procesos implementa una cola FIFO thread-safe para almacenar procesos en estado READY.

#### Estructura

```c
typedef struct ProcessQueueNode {
    PCB* pcb;
    struct ProcessQueueNode* next;
} ProcessQueueNode;

typedef struct {
    ProcessQueueNode* head;
    ProcessQueueNode* tail;
    uint32_t size;
    pthread_mutex_t mutex;
} ProcessQueue;
```

#### Interfaz

```c
void process_queue_init(ProcessQueue* queue);
void process_queue_destroy(ProcessQueue* queue);
void process_queue_enqueue(ProcessQueue* queue, PCB* pcb);
PCB* process_queue_dequeue(ProcessQueue* queue);
int process_queue_is_empty(ProcessQueue* queue);
uint32_t process_queue_size(ProcessQueue* queue);
```

Todas las operaciones adquieren el mutex antes de modificar la estructura, garantizando thread-safety para múltiples productores y consumidores concurrentes.

### Machine

El componente Machine modela la arquitectura hardware jerárquica: CPUs, cores y hardware threads.

#### Estructuras

```c
typedef struct {
    PCB* current_pcb;
} HWThread;

typedef struct {
    HWThread* hw_threads;
    uint32_t num_hw_threads;
} Core;

typedef struct {
    Core* cores;
    uint32_t num_cores;
} CPU;

typedef struct {
    CPU* cpus;
    uint32_t num_cpus;
    uint32_t total_cores;
    uint32_t total_hw_threads;
    pthread_mutex_t mutex;
} Machine;
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

#### Inicialización

```c
Machine* machine_create(uint32_t num_cpus,
                        uint32_t num_cores_per_cpu,
                        uint32_t num_hw_threads_per_core) {
    Machine* machine = malloc(sizeof(Machine));
    machine->num_cpus = num_cpus;
    machine->total_cores = num_cpus * num_cores_per_cpu;
    machine->total_hw_threads =
        machine->total_cores * num_hw_threads_per_core;
    
    machine->cpus = malloc(num_cpus * sizeof(CPU));
    for (uint32_t i = 0; i < num_cpus; i++) {
        machine->cpus[i].num_cores = num_cores_per_cpu;
        machine->cpus[i].cores =
            malloc(num_cores_per_cpu * sizeof(Core));
        
        for (uint32_t j = 0; j < num_cores_per_cpu; j++) {
            machine->cpus[i].cores[j].num_hw_threads =
                num_hw_threads_per_core;
            machine->cpus[i].cores[j].hw_threads =
                malloc(num_hw_threads_per_core * sizeof(HWThread));
            
            for (uint32_t k = 0; k < num_hw_threads_per_core; k++) {
                machine->cpus[i].cores[j].hw_threads[k].current_pcb =
                    pcb_create_idle();
            }
        }
    }
    
    pthread_mutex_init(&machine->mutex, NULL);
    return machine;
}
```

### Process Generator

El Process Generator es un hilo que se despierta periódicamente (según la interrupción del Timer) y crea nuevos procesos con PIDs únicos y TTL aleatorios.

#### Funcionamiento

```c
static void* process_generator_thread_func(void* arg) {
    Kernel* kernel = (Kernel*)arg;
    unsigned long last_tick = 0;
    
    while (kernel_is_running(kernel)) {
        churros_timer_wait_interrupt(kernel->procgen_timer);
        
        if (!kernel_is_running(kernel)) break;
        
        // Generar PID único
        pthread_mutex_lock(&kernel->pid_mutex);
        uint32_t pid = kernel->next_pid++;
        pthread_mutex_unlock(&kernel->pid_mutex);
        
        // TTL aleatorio entre 10 y 100 ticks
        uint32_t ttl = (rand() % 91) + 10;
        
        PCB* pcb = pcb_create(pid, ttl);
        pcb->state = PROCESS_STATE_READY;
        
        process_queue_enqueue(kernel->process_queue, pcb);
        
        printf("[ProcessGenerator] Nuevo proceso creado: "
               "PID=%u, TTL=%u\n", pid, ttl);
    }
    
    return NULL;
}
```

### Kernel

El componente Kernel orquesta todos los hilos del sistema: Clock, Scheduler (placeholder en v1), Process Generator.

#### Estructura

```c
typedef struct {
    Machine* machine;
    ProcessQueue* process_queue;
    Timer* sched_timer;
    Timer* procgen_timer;
    
    pthread_t clock_thread;
    pthread_t sched_timer_thread;
    pthread_t procgen_timer_thread;
    pthread_t scheduler_thread;
    pthread_t process_gen_thread;
    
    pthread_mutex_t running_mutex;
    pthread_mutex_t pid_mutex;
    
    int running;
    uint32_t next_pid;
    
    KernelConfig config;
} Kernel;
```

Cada componente tiene su propio hilo:

- **clock_thread**: Ejecuta `clock_pulse()` cada `config.clock_speed_ms` milisegundos
- **sched_timer_thread**: Notifica ticks al timer del scheduler
- **procgen_timer_thread**: Notifica ticks al timer del generador de procesos
- **scheduler_thread**: En v1 solo despierta con las interrupciones del timer (no hace scheduling real)
- **process_gen_thread**: Genera procesos nuevos al recibir interrupciones

## Resultados de Pruebas

La batería de tests de la Parte 1 valida el funcionamiento correcto del motor de tiempo, los timers y la generación de procesos.

### Salida de Ejemplo

```plaintext
[Clock] Tick 1
[Clock] Tick 2
...
=== Iniciando Kernel de churrOS ===
Configuración:
  CPUs: 2
  Cores por CPU: 2
  HW Threads por Core: 2
  Periodo del Timer: 2 ticks
  Periodo de generación de procesos: 5 ticks
  Velocidad del reloj: 10 ms
  Duración de la simulación: 100 ticks
===================================

[Scheduler] Iniciado (periodo: 2 ticks, 20 ms)
[ProcessGenerator] Iniciado (periodo: 5 ticks, 50 ms)
[Clock] Iniciado
[ProcessGenerator] Activación #1 por interrupción del timer
[ProcessGenerator] Nuevo proceso creado: PID=1, TTL=50
[Scheduler] Procesos en cola: 1
[ProcessGenerator] Activación #2 por interrupción del timer
[ProcessGenerator] Nuevo proceso creado: PID=2, TTL=18
[Scheduler] Procesos en cola: 2
```

Los logs confirman que:

1. El Clock genera ticks a 10ms de intervalo
2. Los Timers generan interrupciones según su periodo configurado
3. El Process Generator crea procesos con PIDs secuenciales y TTLs aleatorios
4. Los procesos se encolan correctamente en la ProcessQueue
5. La sincronización entre hilos funciona correctamente (no hay race conditions)

## Decisiones de Diseño

### Separación de Timers

Se han utilizado dos timers independientes (uno para el scheduler, otro para el generador de procesos) en lugar de un único timer compartido. Esta decisión permite configurar periodos diferentes para cada componente, facilitando la experimentación con distintas configuraciones de carga del sistema.

### Procesos IDLE

Cada hardware thread se inicializa con un proceso IDLE (PID=0). Esta decisión simplifica la lógica del scheduler (que se introduce en la Parte 2), ya que siempre hay un PCB válido en cada thread, eliminando la necesidad de comprobar punteros nulos constantemente.

### Sincronización Mutex + Condvar

Se ha optado por el patrón clásico de mutex + variable de condición en lugar de alternativas como semáforos o spin-locks. Este patrón es más eficiente en términos de CPU (los hilos duermen en lugar de hacer busy-waiting) y es el estándar POSIX más portable.

### TTL Aleatorio

Los procesos se generan con TTL aleatorio entre 10 y 100 ticks. Esta decisión permite simular cargas de trabajo heterogéneas, donde algunos procesos son cortos (interactivos) y otros son largos (batch), validando mejor el comportamiento de los algoritmos de scheduling en la Parte 2.

\newpage