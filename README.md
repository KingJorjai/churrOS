# churrOS
Kernel simulator based on chocolate con churros

## Descripción

churrOS es un simulador multihilo de un kernel de sistema operativo implementado en C utilizando pthread.h. El proyecto simula los componentes fundamentales de un sistema operativo, incluyendo:

- **Clock**: Motor del simulador que genera ciclos de ejecución
- **Timer**: Temporizador que genera interrupciones periódicas
- **Scheduler/Dispatcher**: Planificador de procesos con 3 algoritmos implementados
  - **Round Robin**: Preemption por quantum fijo
  - **FIFO**: Ejecución hasta completar (sin preemption por tiempo)
  - **Chocolate Caliente**: Quantum adaptativo basado en temperatura del proceso
- **Process Generator**: Generador aleatorio de procesos
- **Machine**: Simulación de CPUs, cores e hilos hardware
- **Process Queue**: Cola de procesos (PCBs)

## Arquitectura

El sistema está organizado en una arquitectura modular con sincronización basada en mutex y variables de condición:

```
┌─────────────────────────────────────────────────┐
│                    Kernel                       │
├─────────────────────────────────────────────────┤
│  Clock → Timer → Scheduler/Dispatcher           │
│            ↓                                    │
│      Process Generator                          │
│                                                 │
│  Machine (CPUs → Cores → HW Threads)            │
│  Process Queue (PCBs)                           │
└─────────────────────────────────────────────────┘
```

## Compilación

```bash
# Compilar el kernel completo
make

# Compilar tests
make test

# Limpiar archivos generados
make clean
```

## Ejecución

```bash
# Ejecutar con configuración por defecto
./build/churros

# Ejecutar con parámetros personalizados
./build/churros -c 2 -o 4 -t 2 -p 5 -g 15 -s 50 -d 200
```

### Opciones de línea de comandos

- `-a ALG`: Algoritmo de scheduling (default: rr)
  - `rr`: Round Robin (quantum fijo)
  - `fifo`: FIFO (sin preemption por tiempo)
  - `ch`: Chocolate Caliente (quantum adaptativo por temperatura)
- `-c NUM`: Número de CPUs (default: 1)
- `-o NUM`: Número de cores por CPU (default: 2)
- `-t NUM`: Número de HW threads por core (default: 2)
- `-p NUM`: Periodo del timer en ticks (default: 5)
- `-g NUM`: Periodo de generación de procesos en ticks (default: 10)
- `-s NUM`: Velocidad del reloj en milisegundos (default: 100)
- `-d NUM`: Duración de la simulación en ticks (0=infinito, default: 100)
- `-h`: Mostrar ayuda

### Ejemplos

```bash
# Round Robin básico
./build/churros -a rr -c 1 -o 1 -t 1 -p 5 -g 10 -s 100 -d 100

# FIFO sin preemption
./build/churros -a fifo -c 1 -o 1 -t 1 -p 5 -g 10 -s 100 -d 100

# Chocolate Caliente con quantum adaptativo
./build/churros -a ch -c 1 -o 1 -t 1 -p 2 -g 5 -s 80 -d 50

# Sistema multicore con Round Robin
./build/churros -a rr -c 2 -o 4 -t 2 -p 5 -g 15 -s 50 -d 200
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
├── build/            # Archivos compilados
├── run_tests.sh      # ← Nuevo: Suite de tests automatizados
├── Makefile
├── LICENSE
└── README.md
```

## Componentes

### Clock
El Clock es el motor del simulador. Genera pulsos periódicos que:
- Avanzan todos los ciclos de la máquina (CPUs, cores, hilos)
- Notifican al Timer para generar interrupciones
- Controlan el tiempo del sistema

### Timer
El Timer recibe pulsos del Clock y genera interrupciones periódicas (ticks) que:
- Despiertan al Scheduler/Dispatcher
- Permiten la multitarea cooperativa
- Son configurables en periodo

### Process Generator
Genera procesos (PCBs) aleatoriamente con:
- PID único
- Tiempo de vida aleatorio (10-100 ticks)
- Frecuencia configurable

### Scheduler/Dispatcher
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
- Quantum según temperatura:
  - ❄️ Frío (<20°C): 10 ticks
  - 🟢 Templado (20-39°C): 6 ticks
  - 🟡 Caliente (40-59°C): 4 ticks
  - 🔴 Muy caliente (60-79°C): 2 ticks
  - 🔥 Ardiendo (≥80°C): 1 tick
- Metáfora: "Como el chocolate caliente, hay que dar sorbitos más pequeños cuando quema"
- Favorece procesos que han esperado (están fríos) con quantums más largos
- Penaliza procesos que han acaparado CPU (están calientes) con quantums cortos

### Machine
Representa la arquitectura hardware del sistema:
- Múltiples CPUs
- Múltiples cores por CPU
- Múltiples hilos hardware por core
- Completamente configurable

### Process Queue
Cola thread-safe de procesos que:
- Almacena todos los PCBs creados
- Protegida por mutex
- Operaciones enqueue/dequeue

## Sincronización

El sistema utiliza **mutex con variables de condición** para toda la sincronización:
- Clock → Timer: variables de condición
- Timer → Scheduler: variables de condición
- Clock → Process Generator: variables de condición
- Acceso a estructuras compartidas: mutex

## Controlar la ejecución

- **Ctrl+C**: Detener la simulación de forma ordenada
- El simulador se detendrá automáticamente al alcanzar la duración especificada

## Testing

El proyecto incluye una suite completa de tests automatizados:

```bash
# Ejecutar todos los tests (18 tests)
./run_tests.sh
```

### Cobertura de Tests
- **Sección 1**: Round Robin (3 tests) - Preemption, quantum corto, multicore
- **Sección 2**: FIFO (3 tests) - Sin preemption, timer rápido, multicore
- **Sección 3**: Comparativas (4 tests) - Fairness, overhead, convoy effect
- **Sección 4**: Estrés (3 tests) - Saturación de cola, escenarios realistas
- **Sección 5**: Chocolate Caliente (5 tests) - Temperatura, quantum adaptativo

## Estado Actual

✅ Clock funcionando
✅ Timer con interrupciones periódicas
✅ Process Generator creando PCBs
✅ **3 Algoritmos de Scheduling implementados y validados**
  - ✅ Round Robin con quantum configurable
  - ✅ FIFO sin preemption por tiempo
  - ✅ Chocolate Caliente con quantum adaptativo por temperatura
✅ Machine con arquitectura multicore configurable
✅ Process Queue thread-safe
✅ Sincronización con mutex y variables de condición
✅ Parámetros configurables por línea de comandos
✅ **Suite de 18 tests automatizados con 100% de éxito**
✅ **Arquitectura modular** (scheduler separado del kernel)

## Futuras Mejoras

- Parte 4: Implementar memoria virtual
- Parte 5: Añadir sistema de archivos simulado

## Licencia

Ver archivo LICENSE
