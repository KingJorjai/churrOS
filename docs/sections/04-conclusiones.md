# Conclusiones

## Logros del Proyecto

He conseguido montar un simulador funcional de kernel que integra lo esencial de un SO moderno. He cumplido los objetivos de las tres partes:

**Parte 1 - Arquitectura Base**: He montado un motor de tiempo robusto con sincronización multihilo usando POSIX. La jerarquía de Machine (CPUs $\rightarrow$ Cores $\rightarrow$ HW Threads) modela bien el hardware actual, y la cola de procesos con PCBs me ha dado la base necesaria. El Process Generator con Timer dedicado permite controlar la tasa de llegada de procesos independientemente del Clock.

**Parte 2 - Scheduler**: Aquí he implementado un scheduler event-driven que solo se activa ante eventos concretos, como en kernels reales. Los tres algoritmos (Round Robin, FIFO y Chocolate Caliente) funcionan bien y permiten comparar estrategias. Chocolate Caliente, mi algoritmo con temperatura adaptativa y quantum dinámico, ha resultado ser una idea bastante efectiva.

**Parte 3 - Memoria Virtual**: He completado el subsistema de memoria con MMU, TLB de 16 entradas con reemplazo LRU, paginación de 4KB y demand paging. El loader de archivos de texto plano (formato custom, no ELF binario) y el ISA de 4 instrucciones me han permitido ejecutar programas sintéticos para validar el sistema. Las tasas de acierto del TLB ($85\%-95\%$) confirman que la localidad funciona y el reemplazo LRU hace su trabajo.

## Aprendizajes Clave

### Sincronización Multihilo

Lo más duro fue garantizar thread-safety en todos lados. Mutex + variables de condición fueron clave para evitar race conditions, sobre todo en:

- Clock: coordinar el productor (pulsos) con múltiples consumidores (Timers)
- ProcessQueue: acceso concurrente entre generador (productor) y scheduler (consumidor)
- Machine: bloqueo global durante scheduling para mantener consistencia

Bloquear toda la máquina durante scheduling reduce paralelismo, sí, pero ha simplificado muchísimo la sincronización y me ha permitido centrarme en los algoritmos.

### Diseño Event-Driven

La arquitectura event-driven del scheduler ha funcionado mucho mejor que un enfoque periódico. Ventajas que he visto:

- Menos overhead (el scheduler solo trabaja cuando hace falta)
- Respuesta más rápida a eventos (terminación de procesos, expiración de quantum)
- Más claro conceptualmente (eventos explícitos vs polling constante)

Este patrón refleja cómo funcionan los kernels de verdad —el scheduler despierta por interrupciones concretas (timer, I/O, system call).

### Gestión de Memoria Virtual

Implementar memoria virtual me ha ayudado a entender conceptos que en teoría parecen abstractos:

- **Traducción de direcciones**: Ver cómo VPN y offset se combinan para formar direcciones físicas ha quedado mucho más claro al implementarlo
- **TLB como caché**: El TLB reduce drásticamente los accesos a Page Table (entre $6\times$ y $10\times$ según mis trazas), lo que confirma su importancia
- **Demand paging**: Asignar páginas bajo demanda optimiza memoria sin afectar funcionalidad, como esperaba de la teoría
- **Localidad**: He podido comprobar empíricamente que programas reales exhiben localidad espacial y temporal

Las trazas detalladas de MMU fueron especialmente útiles para debugging y para entender el flujo de traducción.

## Comparación de Algoritmos

Las pruebas automatizadas me han permitido comparar los tres algoritmos de scheduling:

| Métrica | FIFO | Round Robin | Chocolate Caliente |
|---------|------|-------------|-------------------|
| **Context Switches** | Mínimo | Alto | Medio-Alto |
| **Tiempo de Respuesta** | Malo | Bueno | Muy bueno |
| **Overhead de CPU** | Mínimo | Alto | Medio |
| **Equidad** | Nula | Total | Proporcional |
| **Complejidad** | Trivial | Simple | Moderada |

**FIFO** es óptimo solo cuando todos los procesos tienen duración similar. En cargas heterogéneas sufre convoy effect severo.

**Round Robin** proporciona equidad total pero a costa de context switches frecuentes. Quantum pequeño mejora tiempo de respuesta pero incrementa overhead; quantum grande reduce overhead pero deteriora interactividad.

**Chocolate Caliente** logra el mejor balance: favorece procesos interactivos (se mantienen fríos) sin penalizar excesivamente procesos CPU-bound. El quantum adaptativo reduce context switches innecesarios cuando hay procesos largos, manteniendo buena respuesta para procesos cortos.

## Validación y Pruebas

La batería de 34 pruebas automatizadas valida el funcionamiento completo: sincronización correcta del Clock y Timers, generación de procesos con PIDs únicos, preemption exacta al expirar quantum (RR), run-to-completion sin preemption (FIFO), temperatura y quantum adaptativo (Chocolate Caliente), traducción de direcciones virtuales $\rightarrow$ físicas, TLB hit/miss con reemplazo LRU, demand paging con asignación de frames, y ejecución correcta de instrucciones LOAD/STORE/ADD/EXIT. Los tests con múltiples CPUs, cores y threads confirman que la sincronización funciona correctamente incluso bajo alta concurrencia.

## Dificultades Encontradas

### Debugging de Race Conditions

Los bugs más difíciles han sido las race conditions en accesos concurrentes. Sin sincronización adecuada, aparecían comportamientos erráticos difíciles de reproducir. Logs detallados y revisar cuidadosamente el uso de mutex/condvars me ha sacado del bache.

### Coordinación entre Componentes

Orquestar el ciclo de vida de múltiples hilos (Clock, Scheduler, Process Generator, Loader) ha requerido cuidado especial en la secuencia de inicio y parada. Los deadlocks durante shutdown han sido particularmente duros de resolver.

## Reflexión Final

Desarrollar churrOS en tres fases ha sido una buena idea. Cada parte ha sumado funcionalidad sobre una base sólida:

- **Parte 1** ha puesto la infraestructura de tiempo y sincronización
- **Parte 2** ha añadido la inteligencia (scheduling)
- **Parte 3** ha completado el sistema con memoria virtual

Esta estrategia me ha permitido validar cada componente antes de añadir el siguiente, simplificando el debugging y manteniendo todo claro.

He cumplido el objetivo: **entender a fondo cómo funciona un kernel implementándolo**. Conceptos como sincronización multihilo, scheduling y memoria virtual, que en teoría parecen abstractos, se han vuelto concretos al codificarlos.

churrOS no es un kernel de producción (ni de lejos), pero sí es un simulador didáctico completo que implementa correctamente los componentes fundamentales de un SO. Me ha dado una base sólida para entender cómo funcionan los kernels reales.
