# Parte 2: Scheduler y Algoritmos de Scheduling

## Introducción

La segunda parte del proyecto implementa el corazón del sistema operativo: el scheduler. A diferencia de un enfoque clásico con scheduler periódico, churrOS implementa un **scheduler completamente event-driven** que solo se activa cuando ocurren eventos específicos.

Se han implementado tres algoritmos de scheduling:

1. **Round Robin** (RR): Quantum fijo con preemption
2. **FIFO**: Sin preemption (run to completion)
3. **Chocolate Caliente** (CH): Quantum adaptativo basado en temperatura (innovación propia)

## Arquitectura del Scheduler

El scheduler de churrOS implementa un diseño completamente event-driven que solo se activa cuando ocurren eventos específicos: proceso termina (TTL == 0 o EXIT ejecutado), quantum expira (solo en RR y CH), o se crea un nuevo proceso. Este enfoque refleja el comportamiento de kernels reales donde el scheduler es invocado por interrupciones específicas.

```{.mermaid format=pdf}
flowchart TD
    CT[Clock Tick] --> MAC[machine_advance_cycle]
    MAC --> LOOP{Para cada HW Thread}
    LOOP --> EXEC[Ejecutar instrucción]
    EXEC --> DEC[Decrementar TTL]
    DEC --> INC[Incrementar ticks_since_swap]
    INC --> TEMP[Actualizar temperatura]
    TEMP --> EVENT{Detectar eventos}
    EVENT -->|TTL == 0?| SCHED[scheduler_update_thread]
    EVENT -->|Quantum expirado?| SCHED
    EVENT -->|No| LOOP
    SCHED --> LOOP
```

```c
/**
 * Actualiza el estado de un hardware thread según el algoritmo 
 * de scheduling configurado.
 */
void scheduler_update_thread(Kernel* kernel, HWThread* thread, 
                            uint32_t cpu, uint32_t core, 
                            uint32_t hw_thread);
```

`machine_advance_cycle()` detecta eventos y los señaliza al kernel (`kernel_signal_scheduler()`); el hilo del scheduler despierta, bloquea la máquina y entonces invoca `scheduler_update_thread()` para cada HW thread afectado.

## Round Robin (RR)

Round Robin es el algoritmo clásico de scheduling con quantum fijo (configurado por `-q`, default 5 ticks) y preemption obligatoria. El proceso es desalojado al expirar su quantum, garantizando equidad entre todos los procesos aunque con overhead de un context switch por quantum. La detección de quantum expirado ocurre en `machine_advance_cycle()`, que señaliza al scheduler; el hilo del scheduler aplica `scheduler_update_thread()` donde procesos terminados se destruyen, los preemptados vuelven a READY y se encolan, y se despacha el siguiente proceso o se crea IDLE si la cola está vacía.

### Características Clave

Round Robin destaca por su simplicidad conceptual y buen tiempo de respuesta para procesos interactivos, garantizando equidad entre procesos. Sin embargo, sufre overhead de context switches frecuentes, donde quantums pequeños incrementan el overhead mientras quantums grandes deterioran el tiempo de respuesta. Los tests validan que la preemption ocurre exactamente al expirar el quantum, los procesos rotan equitativamente y los context switches se contabilizan correctamente.

### Ejemplo de salida (v2)

```plaintext
Algoritmo: Round Robin (quantum=8 ticks)
(0:0:0) Dispatch PID=1 (Reemplazando IDLE) TTL=0
(0:0:1) Dispatch PID=2 (Reemplazando IDLE) TTL=0
(0:0:1) Preemption PID=2 -> PID=4
(0:0:0) Dispatch PID=5 (Reemplazando IDLE) TTL=0
(0:0:0) Preemption PID=5 -> PID=7
(0:1:0) Preemption PID=16 -> PID=17
```

## FIFO (First In First Out)

FIFO representa el algoritmo más simple posible, ejecutando procesos sin preemption por tiempo hasta su terminación. El parámetro `-q` no tiene efecto, y solo ocurre context switch cuando un proceso termina, minimizando completamente el overhead. Su orden estricto FIFO hace que el primer proceso en llegar sea el primero en ejecutar.

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
            kernel->context_switches++;
        } else {
            thread->current_pcb = pcb_create_idle();
        }
    }
    // No hay preemption por tiempo en FIFO
    break;
```

### Características

**Ventajas:**
- **Mínimo overhead**: Solo context switch al terminar proceso
- **Simple**: Sin gestión de quantum ni preemption
- **Predecible**: Orden de ejecución determinista

**Desventajas:**
- **Convoy effect**: Procesos cortos esperan tras procesos largos
- **Inanición posible**: Proceso largo bloquea a todos los demás
- **Mala interactividad**: No hay time-sharing

### Testing

```bash
# Test básico de FIFO
./build/churros -a fifo -c 1 -o 1 -t 1 -g 10 -s 100 -d 100

# Test con generación rápida (evidencia convoy effect)
./build/churros -a fifo -g 5 -s 50 -d 80

# Test multicore (FIFO independiente por core)
./build/churros -a fifo -c 2 -o 2 -t 1 -g 8 -d 150
```

Validaciones realizadas:
- No hay preemption por tiempo (solo al terminar)
- Orden FIFO respetado
- Context switches mínimos
- Convoy effect observable en logs

### Ejemplo de salida (v2)

```plaintext
Algoritmo: FIFO (quantum=5 ticks)
(0:0:0) Dispatch PID=1 (Reemplazando IDLE) TTL=0
(0:1:0) Dispatch PID=2 (Reemplazando IDLE) TTL=0
(0:0:0) Proceso PID=1 terminado
(0:0:0) Dispatch PID=3 TTL=0
(0:1:0) Proceso PID=2 terminado
(0:1:0) Dispatch PID=5 TTL=0
```

## Chocolate Caliente (CH) - Innovación Propia

### Descripción

**Chocolate Caliente** es un algoritmo de scheduling original que implementa quantum adaptativo basado en temperatura. La metáfora es: *"Como el chocolate caliente, hay que dar sorbitos más pequeños cuando quema"*.

### Concepto de Temperatura

Cada proceso tiene una temperatura que refleja cuánto ha usado la CPU recientemente:

- **Se calienta** mientras ejecuta: +8°C por tick
- **Se enfría** mientras espera en cola: -5°C por tick
- **Rango**: 0°C (frío) a 100°C (ardiendo)

```c
// En machine_advance_cycle() - Calentar proceso ejecutando
if (kernel->config.scheduler_algorithm == SCHEDULER_CHOCOLATE_CALIENTE) {
    pcb->temperature = (pcb->temperature < 100) ? 
                      pcb->temperature + 8 : 100;
}

// Enfriamiento durante decisiones de scheduling (cola enlazada)
ProcessNode* node = queue->head;
while (node) {
    PCB* pcb = node->pcb;
    if (pcb->temperature > 0) {
        pcb->temperature = (pcb->temperature >= 5) ? pcb->temperature - 5 : 0;
    }
    node = node->next;
}
```

### Quantum Adaptativo

El quantum máximo depende de la temperatura del proceso:

```c
static uint32_t get_max_quantum_by_temperature(uint32_t temperature, 
                                               uint32_t quantum_base)
{
    uint32_t base = (quantum_base > 0) ? quantum_base : 5;
    
    if (temperature >= 80) return (base * 1) / 5;  // 20% - Ardiendo
    if (temperature >= 60) return (base * 2) / 5;  // 40% - Muy caliente
    if (temperature >= 40) return (base * 4) / 5;  // 80% - Caliente
    if (temperature >= 20) return (base * 6) / 5;  // 120% - Templado
    return base * 2;                                // 200% - Frío
}
```

Con `quantum_base=5` (default):

| Temperatura | Estado | Quantum | Significado |
|------------|--------|---------|-------------|
| < 20°C | Frío | 10 ticks | Ha esperado mucho, merece tiempo |
| 20-39°C | Templado | 6 ticks | Balance normal |
| 40-59°C | Caliente | 4 ticks | Ha usado bastante CPU |
| 60-79°C | Muy caliente | 2 ticks | Ha acaparado CPU |
| ≥ 80°C | Ardiendo | 1 tick | Penalización máxima |

### Parámetro `quantum_base` Configurable

El parámetro `-q` en Chocolate Caliente no fija el quantum, sino que escala todos los quantums proporcionalmente:

```bash
# quantum_base = 5 (default)
# Quantums: 1, 2, 4, 6, 10
./build/churros -a ch -q 5

# quantum_base = 10 (sistema más lento)
# Quantums: 2, 4, 8, 12, 20
./build/churros -a ch -q 10

# quantum_base = 3 (sistema más rápido)
# Quantums: 0, 1, 2, 3, 6  (nota: mínimo 0 puede ser 1 en práctica)
./build/churros -a ch -q 3
```

### Implementación

```c
case SCHEDULER_CHOCOLATE_CALIENTE:
    uint32_t max_quantum = get_max_quantum_by_temperature(
        current->temperature, kernel->config.rr_quantum);
    
    if (current->ttl == 0 || 
        current->state == PROCESS_STATE_TERMINATED) {
        // Proceso terminó
        pcb_destroy(current);
        thread->current_pcb = NULL;
    } else if (current->ticks_since_swap >= max_quantum) {
        // Quantum expirado → preemption
        current->state = PROCESS_STATE_READY;
        process_queue_enqueue(kernel->process_queue, current);
        
        LOG_AT_NOTICE(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
               "%s PID=%u Preempted temp=%u°C quantum=%u→%u ticks",
               get_temperature_emoji(current->temperature),
               current->pid, current->temperature,
               current->ticks_since_swap, max_quantum);
    }
    
    // Despachar siguiente proceso
    if (!thread->current_pcb) {
        PCB* next = process_queue_dequeue(kernel->process_queue);
        if (next) {
            scheduler_dispatch(thread, next, cpu, core, hw_thread);
            kernel->context_switches++;
            
            uint32_t next_quantum = get_max_quantum_by_temperature(
                next->temperature, kernel->config.rr_quantum);
            LOG_AT_INFO(LOG_COMPONENT_SCHEDULER, cpu, core, hw_thread,
                   "%s Dispatch PID=%u temp=%u°C quantum=%u TTL=%u",
                   get_temperature_emoji(next->temperature),
                   next->pid, next->temperature, next_quantum, next->ttl);
        } else {
            thread->current_pcb = pcb_create_idle();
        }
    }
    break;
```

### Características

**Ventajas:**
- **Fairness mejorado**: Procesos que esperan reciben más tiempo
- **Penaliza acaparadores**: Procesos que monopolizan CPU tienen quantums cortos
- **Auto-balanceo**: El sistema encuentra equilibrio naturalmente
- **Visualmente intuitivo**: Los emojis ayudan a entender el comportamiento

**Desventajas:**
- **Complejidad**: Más difícil de razonar que RR o FIFO
- **Overhead**: Cálculo de temperatura en cada tick
- **Parámetros**: Requiere ajuste de quantum_base según workload

### Propiedades Emergentes

El algoritmo tiene comportamientos interesantes:

1. **Procesos I/O-bound favorecidos**: Si un proceso espera mucho (simulando I/O), se enfría y recibe quantums largos
2. **Procesos CPU-bound penalizados**: Si un proceso usa CPU continuamente, se calienta y recibe quantums cortos
3. **Oscilación natural**: Los procesos oscilan entre frío/caliente, creando balance
4. **Prioridad dinámica**: La temperatura actúa como prioridad dinámica

### Testing

```bash
# Test básico de Chocolate Caliente
./build/churros -a ch -c 1 -o 1 -t 1 -q 5 -g 5 -s 80 -d 50

# Test con quantum_base largo
./build/churros -a ch -q 10 -g 8 -s 50 -d 100

# Test multicore (observar balance entre cores)
./build/churros -a ch -c 2 -o 2 -t 1 -q 5 -g 5 -d 150
```

Validaciones realizadas:
- Temperatura aumenta mientras ejecuta
- Temperatura disminuye mientras espera
- Quantum se ajusta según temperatura
- Procesos fríos reciben más tiempo
- Procesos calientes son preemptados rápido
- quantum_base escala correctamente todos los quantums

### Ejemplo de Salida
```plaintext
Algoritmo: Chocolate Caliente (Quantum base: 5 ticks)
(0:0:0) PID=1 Temp=40°C 🟡 Quantum=4 Ticks=5/4 TTL=0
(0:0:0) Context switch PID=1 (enfriándose) -> PID=2 (quantum=10)
(0:0:0) PID=2 Temp=43°C 🟡 Quantum=4 Ticks=1/4 TTL=0
(0:0:0) PID=2 Temp=67°C 🔴 Quantum=2 Ticks=4/2 TTL=0
(0:0:0) Context switch PID=2 (enfriándose) -> PID=3 (quantum=10)
(0:0:0) PID=3 Temp=16°C ❄️ Quantum=10 Ticks=2/10 TTL=0
```

Se observa claramente cómo el proceso ejecutando se calienta y reduce su quantum, mientras los procesos en cola se enfrían y reciben quantums más largos.

## Comparativa de Algoritmos

### Suite de Tests Comparativos

El script `run_tests.sh` incluye tests que comparan los tres algoritmos:

```bash
./run_tests.sh
```

#### Test 1: Fairness (Equidad)

Compara cuántos context switches genera cada algoritmo:

- **Round Robin**: ~40-50 context switches (quantum = 5)
- **FIFO**: ~10-15 context switches (solo al terminar)
- **Chocolate Caliente**: ~30-40 context switches (adaptativo)

**Conclusión**: RR es más equitativo pero con más overhead. FIFO minimiza overhead pero sacrifica equidad.

#### Test 2: Overhead

Mide tiempo total de simulación:

- **FIFO**: Más rápido (menos context switches)
- **Round Robin**: Más lento (context switches frecuentes)
- **Chocolate Caliente**: Intermedio (ajusta según workload)

#### Test 3: Convoy Effect

Genera un proceso muy largo (TTL=200) seguido de varios cortos (TTL=10):

- **FIFO**: Los procesos cortos esperan todo el TTL del largo → **convoy effect claro**
- **Round Robin**: Los procesos cortos van progresando → **sin convoy effect**
- **Chocolate Caliente**: Similar a RR, pero el proceso largo se calienta y es penalizado

### Tabla Comparativa

| Característica | Round Robin | FIFO | Chocolate Caliente |
|---------------|-------------|------|-------------------|
| Preemption | Sí (quantum fijo) | No | Sí (quantum variable) |
| Equidad | Alta | Baja | Media-Alta |
| Overhead | Alto | Mínimo | Medio |
| Convoy Effect | No | Sí | No |
| Interactividad | Buena | Mala | Excelente |
| Complejidad | Baja | Muy baja | Media |
| Parámetros | 1 (quantum) | 0 | 1 (quantum_base) |

### Casos de Uso Recomendados

- **Round Robin**: Sistemas interactivos, workload mixto, requisito de equidad
- **FIFO**: Batch processing, procesos similares, overhead crítico
- **Chocolate Caliente**: Workload mixto (CPU/IO), auto-balanceo deseado, sistema experimental

## Estadísticas y Logging

### Métricas Recolectadas

El kernel mantiene estadísticas globales:

```c
typedef struct {
    // ... otros campos ...
    
    // Estadísticas
    uint32_t context_switches;
    pthread_mutex_t stats_mutex;
} Kernel;
```

### Sistema de Logging Multi-Nivel

churrOS implementa un sistema de logging sofisticado:

```c
typedef enum {
    LOG_LEVEL_DEBUG,    // Debugging detallado
    LOG_LEVEL_INFO,     // Información general
    LOG_LEVEL_NOTICE,   // Eventos notables
    LOG_LEVEL_WARN,     // Advertencias
    LOG_LEVEL_ERROR,    // Errores recuperables
    LOG_LEVEL_CRITICAL  // Errores fatales
} LogLevel;
```

#### Uso

```bash
# Logging normal
./build/churros -l info

# Debugging detallado
./build/churros -l debug

# Solo errores
./build/churros -l error

# Sin colores (para redirección)
./build/churros --no-color

# Sin ubicación (CPU:Core:Thread)
./build/churros --no-loc
```

#### Macros de Logging

```c
// Logging sin ubicación
LOG_INFO(component, "mensaje", ...);

// Logging con ubicación (CPU:Core:Thread)
LOG_AT_INFO(component, cpu, core, thread, "mensaje", ...);
```

Componentes disponibles:
- `LOG_COMPONENT_KERNEL`: Kernel general
- `LOG_COMPONENT_CLOCK`: Clock
- `LOG_COMPONENT_TIMER`: Timer
- `LOG_COMPONENT_SCHEDULER`: Scheduler
- `LOG_COMPONENT_MACHINE`: Machine
- `LOG_COMPONENT_MEMORY`: Memory

#### Ejemplo de Salida

```plaintext
[INFO] [Kernel] Starting simulation...
[INFO] [Clock] (0:0:0) Tick 1
[INFO] [Sched] (0:0:0) Dispatch PID=1 temp=0°C quantum=10 TTL=45
[NOTICE] [Sched] (0:0:0) PID=1 Preempted temp=24°C quantum=6→6 ticks
[DEBUG] [Memory] (0:0:0) TLB hit for VPN=0x123
```

## Testing Automatizado

### Suite de 34 Tests

El script `run_tests.sh` ejecuta 34 tests organizados en 6 secciones:

1. **Round Robin** (3 tests): Preemption, quantum corto, multicore
2. **FIFO** (3 tests): Sin preemption, generación rápida, multicore
3. **Comparativas** (4 tests): Fairness, overhead, convoy effect
4. **Estrés** (3 tests): Saturación de cola, escenarios realistas
5. **Chocolate Caliente** (6 tests): Temperatura, quantum adaptativo, quantum_base

### Ejecución

```bash
# Ejecutar todos los tests
./run_tests.sh

# Ejecutar un test específico (editar script)
./build/churros -a rr -c 1 -o 1 -t 1 -q 5 -g 10 -s 100 -d 100 -l notice
```

### Validación Automática

Cada test verifica condiciones específicas usando `grep`:

```bash
# Ejemplo: Validar que ocurre preemption en RR
./build/churros -a rr -q 5 -d 50 2>&1 | grep -i "preempt" > /dev/null
if [ $? -eq 0 ]; then
    echo "[OK] Preemption detectada"
else
    echo "[ERROR] No hay preemption"
fi
```

## Conclusiones de la Parte 2

La implementación del scheduler demuestra:

**Arquitectura event-driven funcional**: El scheduler solo se activa por eventos  
**Tres algoritmos completos**: RR, FIFO y CH totalmente funcionales  
**Quantum adaptativo innovador**: Chocolate Caliente es una contribución original  
**Testing exhaustivo**: 30 tests automatizados validan comportamiento  
**Logging sofisticado**: Sistema multi-nivel con colores y ubicación  
**Comparativas empíricas**: Datos reales de fairness, overhead y convoy effect  

El sistema cumple todos los requisitos de un scheduler de SO educativo y añade innovaciones (CH) que lo distinguen.

\newpage
