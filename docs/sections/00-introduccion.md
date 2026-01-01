# Introducción

## Presentación del Proyecto

**churrOS** es un simulador multihilo de kernel que hemos desarrollado en C con la biblioteca `pthread.h` para la asignatura de Sistemas Operativos. Simula los componentes esenciales de un sistema operativo: gestión de procesos, algoritmos de planificación y memoria virtual con paginación.

## Objetivos del Trabajo

Nuestro objetivo principal era entender a fondo cómo funciona internamente un kernel, pero no desde la teoría pura, sino implementándolo. Con churrOS pudimos experimentar directamente:

- Sincronización multihilo usando primitivas POSIX
- Algoritmos de scheduling con métricas medibles
- Gestión de memoria virtual con traducción de direcciones
- Arquitectura modular que refleja la separación de responsabilidades en sistemas operativos reales

## Metodología Incremental

Desarrollamos el proyecto en tres fases, cada una más compleja que la anterior:

**Parte 1 - Arquitectura Base**: Montamos el motor de tiempo (Clock), el temporizador (Timer) y todo lo necesario para generar procesos y sincronizar hilos. Modelamos la máquina con CPUs, cores y hardware threads, además de implementar la cola de procesos.

**Parte 2 - Scheduler**: Aquí viene lo interesante: implementamos un planificador event-driven que solo se activa cuando pasa algo relevante (se acaba el quantum, termina un proceso o llega uno nuevo). Desarrollamos tres algoritmos: Round Robin clásico, FIFO sin preemption y uno propio que llamamos Chocolate Caliente, con quantum adaptativo según la "temperatura" del proceso.

**Parte 3 - Memoria Virtual**: La guinda del pastel. Implementamos MMU, TLB, paginación de 4KB y traducción completa de direcciones. También creamos un loader de ELF y un set básico de instrucciones para validar que todo funcionaba bien.

## Estructura del Documento

Hemos organizado esta memoria siguiendo el desarrollo del proyecto:

- **Sección 1**: Arquitectura Base (Parte 1)
- **Sección 2**: Scheduler y Algoritmos (Parte 2)
- **Sección 3**: Memoria Virtual (Parte 3)
- **Sección 4**: Conclusiones y Reflexiones

En cada sección explicamos el diseño, cómo lo implementamos, qué decisiones tomamos y los resultados de las pruebas. Documentamos solo lo que añadimos en cada fase para que se vea claramente la evolución del proyecto.

\newpage
