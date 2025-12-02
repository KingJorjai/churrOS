# churrOS
Kernel simulator based on chocolate con churros

## Descripción

churrOS es un simulador multihilo de un kernel de sistema operativo implementado en C utilizando pthread.h. El proyecto simula los componentes fundamentales de un sistema operativo, incluyendo:

- **Clock**: Motor del simulador que genera ciclos de ejecución
- **Timer**: Temporizador que genera interrupciones periódicas
- **Scheduler/Dispatcher**: Planificador de procesos
- **Process Generator**: Generador aleatorio de procesos
- **Machine**: Simulación de CPUs, cores e hilos hardware
- **Process Queue**: Cola de procesos (PCBs)

## Arquitectura

El sistema está organizado en una arquitectura modular con sincronización basada en mutex y variables de condición:

```
┌─────────────────────────────────────────────────┐
│                    Kernel                        │
├─────────────────────────────────────────────────┤
│  Clock → Timer → Scheduler/Dispatcher           │
│            ↓                                     │
│      Process Generator                           │
│                                                  │
│  Machine (CPUs → Cores → HW Threads)            │
│  Process Queue (PCBs)                            │
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

- `-c NUM`: Número de CPUs (default: 1)
- `-o NUM`: Número de cores por CPU (default: 2)
- `-t NUM`: Número de HW threads por core (default: 2)
- `-p NUM`: Periodo del timer en ticks (default: 5)
- `-g NUM`: Periodo de generación de procesos en ticks (default: 10)
- `-s NUM`: Velocidad del reloj en milisegundos (default: 100)
- `-d NUM`: Duración de la simulación en ticks (0=infinito, default: 100)
- `-h`: Mostrar ayuda

### Ejemplo

```bash
# Simular un sistema con 2 CPUs, 4 cores por CPU, 2 hilos por core
# Timer cada 5 ticks, generar procesos cada 15 ticks
# Velocidad de 50ms por tick, duración de 200 ticks
./build/churros -c 2 -o 4 -t 2 -p 5 -g 15 -s 50 -d 200
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
│   └── kernel.h
├── source/           # Implementaciones
│   ├── clock.c
│   ├── timer.c
│   ├── pcb.c
│   ├── process_queue.c
│   ├── machine.c
│   ├── kernel.c
│   └── main.c
├── tests/            # Tests unitarios
│   └── test_clock.c
├── build/            # Archivos compilados
├── Makefile
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
En esta primera parte, simplemente se activa con cada tick del Timer y reporta su estado. En futuras partes implementará:
- Planificación de procesos
- Cambios de contexto
- Asignación de recursos

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

## Parte 1 - Estado Actual

✅ Clock funcionando
✅ Timer con interrupciones periódicas
✅ Process Generator creando PCBs
✅ Scheduler recibiendo interrupciones
✅ Machine con arquitectura configurable
✅ Process Queue thread-safe
✅ Sincronización con mutex y variables de condición
✅ Parámetros configurables por línea de comandos

## Futuras Mejoras

- Parte 2: Implementar planificación real de procesos
- Parte 3: Añadir estados de procesos y cambios de contexto
- Parte 4: Implementar memoria virtual
- Parte 5: Añadir sistema de archivos simulado

## Licencia

Ver archivo LICENSE
