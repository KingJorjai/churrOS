# churrOS
Kernel simulator based on chocolate con churros

## Descripción

churrOS es un simulador multihilo de un kernel de sistema operativo implementado en C utilizando pthread.h. El proyecto simula los componentes fundamentales de un sistema operativo, incluyendo:

- **Clock**: Motor del simulador que genera ciclos de ejecución
- **Timer**: Temporizador para el generador de procesos
- **Scheduler/Dispatcher**: Planificador event-driven con 3 algoritmos implementados
  - **Round Robin**: Preemption por quantum fijo
  - **FIFO**: Ejecución hasta completar (sin preemption por tiempo)
  - **Chocolate Caliente**: Quantum adaptativo basado en temperatura del proceso
- **Process Generator**: Generador aleatorio de procesos
- **Machine**: Simulación de CPUs, cores e hilos hardware que detecta eventos
- **Process Queue**: Cola de procesos (PCBs)

## Arquitectura

El sistema está organizado en una arquitectura modular con sincronización basada en mutex y variables de condición:

```
┌──────────────────────────────────────────────────────┐
│                     Kernel                           │
├──────────────────────────────────────────────────────┤
│  Clock → Machine → Detección de Eventos              │
│    ↓        ↓                   ↓                    │
│  Timer → Process Gen     Scheduler (event-driven)    │
│                                                      │
│  Machine (CPUs → Cores → HW Threads)                 │
│  Process Queue (PCBs)                                │
│                                                      │
│  Eventos: Quantum expirado, Proceso termina,         │
│           Nuevo proceso creado                       │
└──────────────────────────────────────────────────────┘
```

## Compilación

```bash
# Compilar el kernel completo
make

# Compilar prometheus (generador de archivos .elf)
make prometheus

# Generar archivos .elf de prueba
make elfs

# Compilar tests
make test

# Limpiar archivos generados
make clean

# Limpiar archivos .elf generados
make clean-elfs
```

## Ejecución

```bash
# Ejecutar con configuración por defecto
./build/churros

# Ejecutar con parámetros personalizados
./build/churros -a rr -c 2 -o 4 -t 2 -q 5 -g 15 -s 50 -d 200
```

### Opciones de línea de comandos

- `-a ALG`: Algoritmo de scheduling (default: rr)
  - `rr`: Round Robin (quantum fijo)
  - `fifo`: FIFO (sin preemption por tiempo)
  - `ch`: Chocolate Caliente (quantum adaptativo por temperatura)
- `-c NUM`: Número de CPUs (default: 1)
- `-o NUM`: Número de cores por CPU (default: 2)
- `-t NUM`: Número de HW threads por core (default: 2)
- `-q NUM`: Quantum en ticks (default: 5)
  - Round Robin: quantum fijo
  - Chocolate Caliente: quantum base para cálculo adaptativo
- `-g NUM`: Periodo de generación de procesos en ticks (default: 10)
- `-s NUM`: Velocidad del reloj en milisegundos (default: 100)
- `-d NUM`: Duración de la simulación en ticks (0=infinito, default: 100)
- `-h`: Mostrar ayuda

### Ejemplos

```bash
# Round Robin básico
./build/churros -a rr -c 1 -o 1 -t 1 -q 5 -g 10 -s 100 -d 100

# FIFO sin preemption
./build/churros -a fifo -c 1 -o 1 -t 1 -g 10 -s 100 -d 100

# Chocolate Caliente con quantum adaptativo
./build/churros -a ch -c 1 -o 1 -t 1 -g 5 -s 80 -d 50

# Sistema multicore con Round Robin
./build/churros -a rr -c 2 -o 4 -t 2 -q 5 -g 15 -s 50 -d 200
```

## Estructura del Proyecto

```
churrOS/
├── include/          # Archivos de cabecera
│   ├── clock.h
│   ├── timer.h
│   ├── pcb.h
│   ├── process_queue.h
│   ├── machine.h
│   ├── scheduler.h   # ← Nuevo: Interfaz del scheduler
│   ├── logging.h
│   └── kernel.h
├── source/           # Implementaciones
│   ├── clock.c
│   ├── timer.c
│   ├── pcb.c
│   ├── process_queue.c
│   ├── machine.c
│   ├── scheduler.c   # ← Nuevo: Algoritmos de scheduling
│   ├── logging.c
│   ├── kernel.c
│   └── main.c
├── prometheus/       # Generador de programas .elf
│   ├── defines.h
│   ├── prometheus.c
│   └── Makefile
├── build/            # Archivos compilados
├── elfs/             # Archivos .elf generados (gitignored)
├── run_tests.sh      # ← Nuevo: Suite de tests automatizados
├── Makefile
├── LICENSE
└── README.md
```

## Componentes

### Clock
El Clock es el motor del simulador. Genera pulsos periódicos (ticks) que:
- Llaman a `machine_advance_cycle()` para avanzar el estado de todos los procesos
- Notifican al Timer del Process Generator
- Detectan eventos que requieren scheduling (quantum expirado, proceso terminado)
- Controlan el tiempo del sistema

### Timer
El Timer recibe pulsos del Clock y genera interrupciones periódicas para el Process Generator:
- Notifica al generador de procesos cuando debe crear un nuevo proceso
- Es configurable en periodo con `-g`
- **Nota**: El scheduler ya no usa timer periódico, es completamente event-driven

### Process Generator
Genera procesos (PCBs) aleatoriamente con:
- PID único
- Tiempo de vida aleatorio (10-100 ticks)
- Frecuencia configurable

### Scheduler/Dispatcher
Scheduler **event-driven** que se activa solo cuando ocurren eventos específicos:
- ✅ Proceso termina (TTL == 0)
- ✅ Quantum expira (solo RR y CH)
- ✅ Nuevo proceso creado

Implementa tres algoritmos de scheduling completamente funcionales:

#### Round Robin (RR)
- Quantum fijo configurado por el periodo del timer
- Preemption automática cuando se agota el quantum
- Distribución equitativa del tiempo de CPU
- Ideal para sistemas interactivos

#### FIFO (First In First Out)
- Ejecución hasta completar (run to completion)
- Sin preemption por tiempo
- Cambio de contexto solo al terminar proceso
- Mínimo overhead, máxima latencia para procesos cortos

#### Chocolate Caliente (CH)
- **Quantum adaptativo basado en temperatura** 🔥
- Los procesos se "calientan" (+8°C/tick) mientras ejecutan
- Los procesos se "enfrían" (-5°C/tick) mientras esperan
- **Quantum base configurable con `-q`** (escala todos los quantums proporcionalmente)
- Quantum según temperatura (con quantum_base=5 por defecto):
  - ❄️ Frío (<20°C): 10 ticks (200% del base)
  - 🟢 Templado (20-39°C): 6 ticks (120% del base)
  - 🟡 Caliente (40-59°C): 4 ticks (80% del base)
  - 🔴 Muy caliente (60-79°C): 2 ticks (40% del base)
  - 🔥 Ardiendo (≥80°C): 1 tick (20% del base)
- Metáfora: "Como el chocolate caliente, hay que dar sorbitos más pequeños cuando quema"
- Favorece procesos que han esperado (están fríos) con quantums más largos
- Penaliza procesos que han acaparado CPU (están calientes) con quantums cortos

### Machine
Representa la arquitectura hardware del sistema:
- Múltiples CPUs
- Múltiples cores por CPU
- Múltiples hilos hardware por core
- Completamente configurable
- **Función `machine_advance_cycle()`**: Cada tick del reloj:
  - Decrementa TTL de procesos en ejecución
  - Incrementa `ticks_since_swap` (tiempo de ejecución)
  - Actualiza temperatura (Chocolate Caliente)
  - Detecta eventos y señaliza al scheduler

### Process Queue
Cola thread-safe de procesos que:
- Almacena todos los PCBs creados
- Protegida por mutex
- Operaciones enqueue/dequeue

## Sincronización

El sistema utiliza **mutex con variables de condición** para toda la sincronización:
- **Event-driven scheduling**: El scheduler espera en `pthread_cond_wait()` hasta recibir señales
- **Señalización de eventos**: `kernel_signal_scheduler()` despierta al scheduler cuando:
  - Un proceso termina
  - Un quantum expira
  - Se crea un nuevo proceso
- Timer → Process Generator: variables de condición
- Acceso a estructuras compartidas: mutex

Esto refleja el comportamiento de un SO real donde el scheduler no se ejecuta periódicamente, sino que es invocado por eventos específicos (interrupciones).

## Controlar la ejecución

- **Ctrl+C**: Detener la simulación de forma ordenada
- El simulador se detendrá automáticamente al alcanzar la duración especificada

## Testing

El proyecto incluye una suite completa de tests automatizados:

```bash
# Ejecutar todos los tests (19 tests)
./run_tests.sh
```

### Cobertura de Tests
- **Sección 1**: Round Robin (3 tests) - Preemption, quantum corto, multicore
- **Sección 2**: FIFO (3 tests) - Sin preemption, timer rápido, multicore
- **Sección 3**: Comparativas (4 tests) - Fairness, overhead, convoy effect
- **Sección 4**: Estrés (3 tests) - Saturación de cola, escenarios realistas
- **Sección 5**: Chocolate Caliente (6 tests) - Temperatura, quantum adaptativo, quantum base configurable

## Estado Actual

✅ Clock funcionando
✅ Timer para Process Generator
✅ Process Generator creando PCBs
✅ **Scheduler event-driven** (se activa solo por eventos, no periódicamente)
✅ **3 Algoritmos de Scheduling implementados y validados**
  - ✅ Round Robin con quantum configurable (`-q`)
  - ✅ FIFO sin preemption por tiempo
  - ✅ Chocolate Caliente con quantum adaptativo por temperatura
✅ Machine con arquitectura multicore configurable
✅ **Gestión de quantum tick a tick** (como en SO reales)
✅ **Detección de eventos**: quantum expirado, proceso terminado, proceso creado
✅ Process Queue thread-safe
✅ Sincronización con mutex y variables de condición
✅ Parámetros configurables por línea de comandos
✅ **Suite de 18 tests automatizados**
✅ **Arquitectura modular y realista** (refleja comportamiento de SO real)
✅ **Parte 3: Gestor de Memoria Virtual** (NUEVO)
  - ✅ Physical Memory con 16MB, paginación de 4KB
  - ✅ MMU con TLB de 16 entradas por HW Thread
  - ✅ Tablas de páginas en espacio del kernel
  - ✅ Loader que carga programas desde archivos .elf
  - ✅ Instruction set (LD, ST, ADD, EXIT) implementado
  - ✅ Motor de ejecución con traducción virtual→física
  - ✅ Programas de ejemplo funcionando
  - ✅ **Prometheus**: Generador de archivos .elf de prueba

## Prometheus - Generador de Programas

Prometheus es una herramienta integrada que genera archivos `.elf` de prueba para simular programas en churrOS. Crea programas aleatorios con instrucciones LD, ST, ADD y EXIT.

### Uso de Prometheus

```bash
# Generar archivos .elf con configuración por defecto
# (60 archivos: prog000.elf a prog059.elf en el directorio elfs/)
# Nota: Usa semilla fija (42) para garantizar reproducibilidad
make elfs

# Compilar prometheus sin generar archivos
make prometheus

# Ejecutar prometheus manualmente con opciones personalizadas
./build/prometheus -s 0 -nprog -f0 -l20 -p60
```

### Opciones de Prometheus

- `-s, --seed=N`: Semilla para números aleatorios (default: 0)
- `-n, --name=SSS`: Prefijo del nombre de los programas (default: "prog")
- `-f, --first=NNN`: Primer número del archivo (default: 0)
- `-l, --lines=NNN`: Número aproximado de líneas de código (default: 20)
- `-p, --programs=NNN`: Cantidad de programas a generar (default: 50)
- `-h, --help`: Mostrar ayuda

### Ejemplos de Uso

```bash
# Generar 100 programas pequeños
./build/prometheus -s 3 -ntest -f0 -l10 -p100

# Generar un programa grande para testing
./build/prometheus -s 9 -nbig -f0 -l1000 -p1

# Generar programas con diferente semilla
./build/prometheus -s 42 -nprog -f100 -l50 -p20
```

### Formato de Archivos .elf

Los archivos generados contienen:
- Sección `.text` con código ejecutable (instrucciones)
- Sección `.data` con datos inicializados
- Instrucciones: LD (load), ST (store), ADD (suma), EXIT (finalizar)

**Nota sobre reproducibilidad**: El comando `make elfs` usa una semilla fija (42) para garantizar que todos los desarrolladores generen los mismos archivos .elf, facilitando la depuración y pruebas colaborativas.

## Futuras Mejoras

- Parte 4: Ampliar instruction set (branches, más operaciones)
- Parte 5: Implementar page faults y swapping
- Parte 6: Añadir sistema de archivos simulado

## Licencia

Ver archivo LICENSE
